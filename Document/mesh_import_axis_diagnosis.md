# Mesh Import 좌표계 변환 누락 진단

## 0. 증상 요약 및 확정된 관찰

### 증상
- `Ahri.fbx`, `Galio.fbx` (LoL 에셋, Maya 출처 추정) 를 import 했을 때 mesh 가 틀어진다.
- 런타임에서 **skeletal root bone 을 Y -90° / Z +90°** 회전시키면 외형상 정상으로 보인다.
- static mesh 또한 동일하게 틀어진다 (skeletal 만의 문제가 아님).

### 엔진 좌표계 규약 (전제)
- **Left-handed**
- **X = forward, Y = left, Z = up**

### 진단 전제 (확정된 관찰)
- static / skeletal 양쪽이 동일 방향으로 틀어진다 → 두 경로가 **공유하는 import 시점 좌표계 변환** 이 원인이다.
- skeleton 을 회전시키면 skinned mesh 가 자연스럽게 따라온다 → skinning 바인딩은 건강하다. root 회전 패치는 **근본 수정이 아니라 외형 보정**이다.
- 순수 회전 행렬(det = +1)은 handedness 를 바꿀 수 없다. RH → LH 변환에는 미러링(det = -1) 성분이 필수이다.

### 범위
- 주 대상: `KraftonEngine/Source/Engine/Asset/Import/FBX/Core/` 및 `Builder/`, `Parser/`, `Core/FBXUtil` 등 FBX import 경로.
- 배제 확인용: `KraftonEngine/Source/Editor/Viewport/SkeletalMeshViewerViewportClient.cpp`.
- 본 문서는 **진단 전용**이다. 어떤 코드도 수정하지 않았다.

---

## 1. Import 좌표계 변환 경로 추적

### 1-A. FBX `GlobalSettings` 미독취

`UpAxis`, `FrontAxis`, `CoordAxis`, `OriginalUpAxis` 를 읽는 코드는 **codebase 전체에 존재하지 않음**.

- `Scene->GetGlobalSettings()` 호출, `GetAxisSystem()` 호출, `GetOriginalUpAxis()` 호출 모두 부재.
- 즉, 입력 FBX 가 실제로 어떤 축 시스템(Maya Y-up RH, Max Z-up RH, OpenGL, DirectX, ...)을 갖고 있는지 importer 는 **알지도, 묻지도 않는다**.

### 1-B. 유일한 좌표계 변환 지점

`KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXImporter.cpp:340-372` 의 `PreprocessScene()`:

```cpp
// FBXImporter.cpp:347-354
FbxAxisSystem EngineAxisSystem;
if (!FbxAxisSystem::ParseAxisSystem("yzx", EngineAxisSystem))
{
    UE_LOG("[FBXImporter] Failed to parse engine axis system.");
    return;
}
EngineAxisSystem.DeepConvertScene(Scene);
```

- import 흐름에서 좌표계가 처리되는 **유일한 지점**.
- 호출 순서: `ImportFbxAsset()` → `LoadScene()` (line 128) → `PreprocessScene()` (line 327) → `DeepConvertScene` (line 354). 모든 mesh/skeleton 파싱은 이 변환 **이후**에 일어난다.
- target 축 문자열이 **하드코딩** 되어 있고 (`"yzx"`), source FBX 의 실제 축 정보와 무관하게 모든 FBX 에 동일하게 적용된다.

### 1-C. Static / Skeletal mesh 경로의 공유 지점

| 단계 | 함수 / 위치 | 비고 |
|---|---|---|
| 전처리 | `FBXImporter::PreprocessScene()` (FBXImporter.cpp:340) | 공유. 모든 FBX 대상 |
| FbxAMatrix → FMatrix 변환 | `FBXUtil::ConvertFbxMatrix` (FBXUtil.cpp:136-148) | 공유. **단순 element copy, 축 재배치/미러링 없음** |
| GeometricTransform 빌드 | `FbxMeshGeometryBuilder::BuildGeometricTransform` (FbxMeshGeometryBuilder.cpp:413-425) | 공유. node 의 GeometricTranslation/Rotation/Scaling 을 그대로 가져옴 |
| Mesh→Asset bind matrix | `FbxMeshGeometryBuilder::BuildMeshToAssetBindMatrix` (FbxMeshGeometryBuilder.cpp:430-434) | **공유**. `GeometricTransform * MeshBindGlobal` |
| Vertex 변환 (분기) | `TransformSkeletalVertexToAssetSpace` (367-381) / `TransformStaticVertexToAssetSpace` (383-397) | 함수만 다르고, 행렬·로직은 **동일**. 동일 `MeshToAssetBindMatrix` 로 `TransformPositionWithW` + `TransformVector` 적용 |

→ static mesh 와 skeletal mesh 가 **완벽히 동일한 좌표계 변환 행렬** 을 통과한다. "static 도 똑같이 틀어진다" 라는 관찰과 코드 구조가 정확히 일치한다.

### 1-D. 미러링(핸드네스 반전) 처리 — 부분적으로만 존재

`FbxMeshGeometryBuilder.cpp:40-69` 의 mirroring 안전망:

```cpp
// line 40-46
float GetUpper3x3Determinant(const FMatrix &Matrix) { ... }

// line 49-53
bool HasMirroredHandedness(const FMatrix &Matrix)
{
    constexpr float DeterminantEpsilon = 1.e-6f;
    return GetUpper3x3Determinant(Matrix) < -DeterminantEpsilon;
}

// line 55-69
void AppendTriangleIndices(TArray<uint32> &OutIndices, uint32 I0, uint32 I1, uint32 I2,
                           bool bFlipWinding)
{
    OutIndices.push_back(I0);
    if (bFlipWinding) { OutIndices.push_back(I2); OutIndices.push_back(I1); }
    else              { OutIndices.push_back(I1); OutIndices.push_back(I2); }
}
```

사용처: `FbxMeshGeometryBuilder.cpp:462-468`
```cpp
const bool bFlipWinding = HasMirroredHandedness(MeshToAssetBindMatrix);
if (bFlipWinding) { UE_LOG("[FBXImporter] Mirrored skeletal mesh transform detected; flipping winding. ..."); }
```

Tangent.W 부호 반전: `FbxMeshGeometryBuilder.cpp:380` (skeletal), `:396` (static)
```cpp
Vertex.tangent = FVector4(TangentDirection, bFlipHandedness ? -Vertex.tangent.W : Vertex.tangent.W);
```

**결정적 사실:**
- mirroring 이 검출되면 triangle winding 과 tangent.W 만 반전된다.
- **position / normal / tangent 의 좌표 자체에는 어떤 축 부호 반전도 없다.** (`pos.x *= -1` 등의 패턴은 importer 전체에 부재.)
- 이 안전망은 `MeshToAssetBindMatrix` 의 det < 0 일 때만 작동한다. 만약 `MeshToAssetBindMatrix` 가 (잘못된 변환 때문에) det > 0 으로 계산되어 들어오면 winding 반전조차 발동하지 않는다.

### 1-E. Y -90° / Z +90° 하드코딩 회전 — importer 내부에는 없음

importer 경로 (`Source/Engine/Asset/Import/FBX/**`) 전체에서:
- `XMMatrixRotationY/Z/X`, `XM_PIDIV2`, `1.5707`, `-1.5707`, `90.0f`, `-90.0f`, `HALF_PI`, "Maya", "Y-up" 등의 키워드 grep — 모두 부재.
- 따라서 사용자가 적용하는 Y -90° / Z +90° 패치는 **importer 외부**에 존재한다.
- 정확한 위치는 본 진단의 범위를 벗어나며, [§7 불확실 항목] 에 기재한다.

---

## 2. Viewport 배제 확인

`KraftonEngine/Source/Editor/Viewport/SkeletalMeshViewerViewportClient.cpp`:

- View matrix: `FMatrix::MakeViewMatrix(GetRightVector(), GetUpVector(), GetForwardVector(), GetWorldLocation())` (line 178)
- Projection:
  - Perspective: `FMatrix::PerspectiveFovLH(...)` (line 184)
  - Orthographic: `FMatrix::OrthoLH(...)` (line 189)
- Camera 초기화: `Camera->SetWorldLocation(FVector(5.0f, 0.0f, 2.0f)); Camera->LookAt(FVector::ZeroVector);` (line 505-506) → +X 방향에서 원점을 향함, Z-up 가정.
- 모든 매트릭스가 **LH** 이며, X-forward / Z-up 가정과 일치한다.

**결론: viewport 는 엔진 규약(LH, X-fwd, Z-up)과 일치 → 원인 아님, 확정 배제.**

---

## 3. 수학적 유도

### 3-A. Source / Target 좌표계 정의

- **Source (Maya / LoL FBX, 추정):** Right-handed, Y-up, X-right, Z-front(viewer-toward-model 의 +Z). 이는 Maya 의 기본 출력 컨벤션이다.
- **Engine (확정):** Left-handed, X-forward, Y-left, Z-up.

### 3-B. 정답 변환 행렬 (Maya RH → Engine LH)

```
X_eng (forward) =  Z_src         ┌  0   0   1 ┐
Y_eng (left)    = -X_src    M  = │ -1   0   0 │
Z_eng (up)      =  Y_src         └  0   1   0 ┘

det(M) = 0·(0·0 − 0·1) − 0·(−1·0 − 0·0) + 1·(−1·1 − 0·0) = −1
```

- `det(M) = −1` → **handedness 반전(미러링)이 필수 성분으로 포함됨**.
- 이 변환은 회전+미러링의 합성이며, 순수 회전(det = +1)으로는 절대 재현할 수 없다.

### 3-C. 관측된 런타임 패치의 행렬 분해

패치: skeletal root bone 에 **Y -90°, Z +90°** 회전 적용. 두 회전의 합성:

```
                 ┌  0  -1   0 ┐   ┌  0   0  -1 ┐   ┌  0  -1   0 ┐
R = R_z(+90) · R_y(-90) = │  1   0   0 │ · │  0   1   0 │ = │  0   0  -1 │
                 └  0   0   1 ┘   └  1   0   0 ┘   └  1   0   0 ┘

det(R) = +1
```

- 두 회전 모두 det = +1 이므로, **합성 R 도 항상 det = +1**.
- 따라서 R 은 정답 행렬 M (det = −1) 과 **본질적으로 다른 클래스의 변환**이다. R 은 회전 성분만 흉내 내고, 미러링 성분은 절대 포함할 수 없다.

### 3-D. `ParseAxisSystem("yzx", ...)` 의 해석 (불확실 명시)

FBX SDK 의 `ParseAxisSystem(const char* pStr, FbxAxisSystem& Out)` 은 3-character 축 시스템 문자열을 받는다.

- 통상적인 해석: `[Up, Front, Coord]` 또는 `[Up, Front, Right]`.
- `"yzx"` 의 가장 자연스러운 해석은 **Up = Y, Front = Z, Coord = X (또는 Right = X)** → Maya Y-up 과 사실상 동일한 RH 좌표계.
- 이 해석이 맞다면, source(Maya Y-up RH) → target("yzx" = Y-up RH) 변환은 **사실상 no-op** 이다.
- target 이 engine 규약(Z-up LH)과 일치하지 않는다는 점은 확실하다.

**불확실:** FBX SDK 버전·구현에 따라 `"yzx"` 가 다르게 파싱될 수 있다. 정확한 파싱 결과는 런타임 로깅으로 검증해야 한다([§6 검증 항목] 참조).

### 3-E. `ConvertFbxMatrix` 의 동작 (확인됨)

`FBXUtil.cpp:136-148` 의 `ConvertFbxMatrix(const FbxAMatrix&)`:

```cpp
for (int32 Row = 0; Row < 4; ++Row)
    for (int32 Col = 0; Col < 4; ++Col)
        Result.M[Row][Col] = static_cast<float>(M.Get(Row, Col));
return Result;
```

- 단순 4×4 element-by-element 복사. 행/열 swap, 부호 반전, transpose 모두 부재.
- 즉 `FbxAMatrix` 의 좌표계가 무엇이든 그대로 `FMatrix` 로 들어온다. 이 함수에서 추가적인 좌표계 변환은 발생하지 않는다.

---

## 4. 근본 원인 결론

다음 결함들이 **동시에** 작동하여 현재 증상을 만든다:

### (a) Source `GlobalSettings` 무시
`PreprocessScene` 는 입력 FBX 의 실제 축 시스템을 읽지 않는다. Maya Y-up 이든, Max Z-up 이든, 다른 변형 좌표계든, importer 는 **모든 FBX 를 동일한 가정으로** 다룬다. → 각 FBX 의 다양한 source 축에 적응할 능력이 구조적으로 없다.

### (b) Target 축 문자열의 부적절함
하드코딩된 `"yzx"` 는 (가장 자연스러운 해석상) Y-up 계열이며 RH 일 가능성이 크다. **engine 규약(Z-up, LH) 과 일치하지 않는다.** 따라서 `DeepConvertScene` 호출은 (1) source 와 target 모두 Y-up RH 이면 no-op 에 가깝고, (2) 어떤 경우에도 LH 로 변환하지 않는다.

### (c) Handedness 반전(미러링) 누락
RH → LH 로의 올바른 변환에는 det = −1 성분이 필수인데, 현재 importer 어디에서도 (i) vertex position 의 축 부호 반전, (ii) normal/tangent 의 축 부호 반전, (iii) det = −1 인 변환 행렬 구성 등이 일어나지 않는다. `ConvertFbxMatrix` 도 단순 copy 이므로 변환 행렬에 미러링이 자동으로 들어오지도 않는다.

### (d) 런타임 root 패치는 회전 성분만 보정
사용자가 적용하는 R_z(+90°) · R_y(-90°) 는 det = +1 이므로 **handedness 반전 성분을 절대 포함할 수 없다.** 외형상 자세는 비슷해 보이도록 맞춰주지만, 메시는 **거울상(mirror)** 으로 렌더링되고 있을 가능성이 매우 높다.

### (e) Mirroring 안전망의 발동 조건 미충족
`HasMirroredHandedness(MeshToAssetBindMatrix)` 는 `MeshToAssetBindMatrix` 의 det 가 음수일 때만 winding 을 뒤집는다. 그러나 (a)~(d) 로 인해 `MeshToAssetBindMatrix` 는 미러링 없이 만들어지므로 det 가 양수가 되어 **안전망 자체가 작동하지 않는다.** (importer 로그에 "Mirrored skeletal mesh transform detected" 가 찍히는지 여부로 직접 확인 가능.)

### 종합
- importer 의 좌표계 변환은 **회전 성분조차 제대로 적용되지 않으며**, 동시에 **handedness 반전(미러링) 성분이 완전히 누락**되어 있다.
- 런타임 root 회전 패치는 시각적으로만 자세를 맞춰주는 **임시 보정**이며, 메시 자체는 거울상으로 렌더링되고 있다 (강한 추정, [§6] 으로 검증 가능).
- 결함의 형태: "회전 미반영" + "미러링 미반영" 두 축 모두에서 import 좌표계 변환이 **불완전**하다.

---

## 5. 핵심 코드 위치 요약

| 항목 | 파일 | 라인 |
|---|---|---|
| 좌표계 변환 단일 진입점 | `KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXImporter.cpp` | 347-354 |
| `PreprocessScene` 전체 | `KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXImporter.cpp` | 340-372 |
| FbxAMatrix → FMatrix (단순 copy) | `KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXUtil.cpp` | 136-148 |
| GeometricTransform 빌드 | `KraftonEngine/Source/Engine/Asset/Import/FBX/Builder/FbxMeshGeometryBuilder.cpp` | 413-425 |
| Mesh→Asset bind matrix (공유) | `KraftonEngine/Source/Engine/Asset/Import/FBX/Builder/FbxMeshGeometryBuilder.cpp` | 430-434 |
| 정점 변환 (skeletal) | `KraftonEngine/Source/Engine/Asset/Import/FBX/Builder/FbxMeshGeometryBuilder.cpp` | 367-381 |
| 정점 변환 (static) | `KraftonEngine/Source/Engine/Asset/Import/FBX/Builder/FbxMeshGeometryBuilder.cpp` | 383-397 |
| 미러링 검출 | `KraftonEngine/Source/Engine/Asset/Import/FBX/Builder/FbxMeshGeometryBuilder.cpp` | 40-53 |
| Winding flip | `KraftonEngine/Source/Engine/Asset/Import/FBX/Builder/FbxMeshGeometryBuilder.cpp` | 55-69, 462-468 |
| Tangent.W 부호 반전 | `KraftonEngine/Source/Engine/Asset/Import/FBX/Builder/FbxMeshGeometryBuilder.cpp` | 380, 396 |
| Viewport LH 매트릭스 | `KraftonEngine/Source/Editor/Viewport/SkeletalMeshViewerViewportClient.cpp` | 178, 184, 189 |

---

## 6. 검증 방법 제안

### 6-A. 거울상(mirror) 육안 검증 — 가장 빠르고 결정적
- **Ahri**: 9개 꼬리의 미세 비대칭(꼬리끝 꼬임 방향), 머리 양쪽 장식(귀 모양, 머리띠 매듭) 의 좌우 위치를 공식 LoL 인게임 모델 스크린샷 또는 Riot 공식 splash art 와 비교.
- **Galio**: 비대칭 갑옷 디테일(어깨 패드 한쪽이 큰 것, 또는 흉부 가운데의 비대칭 문장)이 좌우 어느 쪽에 붙어 있는지 비교.
- 캐릭터의 **검 / 무기 잡는 손** 이 reference 와 반대 손인지 확인 (가장 명확한 미러링 지표).
- 거울상이라면 importer 의 handedness 반전 누락이 확정된다.

### 6-B. 런타임 로깅 — 가설 검증
1. `FBXImporter::PreprocessScene` 의 `DeepConvertScene` 호출 직후, 다음을 로깅:
   - `EngineAxisSystem.GetCoorSystem()` (RightHanded / LeftHanded)
   - `int UpSign; EngineAxisSystem.GetUpVector(UpSign)` 의 반환값과 부호
   - `int FrontSign; EngineAxisSystem.GetFrontVector(FrontSign)` 의 반환값과 부호
   → `"yzx"` 가 실제로 어떤 axis system 으로 파싱되는지 확정.
2. `Scene->GetGlobalSettings().GetAxisSystem()` 으로 **source FBX 의 실제 축 시스템**을 로깅 (DeepConvertScene 호출 전·후 둘 다). source 가 어떤 축 시스템인지, 변환 전후로 어떻게 바뀌었는지 확인.
3. `FbxMeshGeometryBuilder::BuildMeshToAssetBindMatrix` 의 결과 행렬에 대해 `GetUpper3x3Determinant` 값을 로깅. 양수면 mirroring 안전망이 절대 발동하지 않는 것이 확정된다.
4. importer 의 기존 `UE_LOG("[FBXImporter] Mirrored skeletal mesh transform detected; ...")` 가 Ahri/Galio import 시 출력되는지 확인. 출력되지 않는다면 (c)·(e) 결론을 직접 뒷받침한다.

### 6-C. 비교 import — 회귀 격리
- 알려진 좌표계의 단순 mesh (Z-up LH 로 미리 변환된 FBX, 또는 Maya 에서 명시적으로 Z-up 으로 export 한 동일 mesh) 를 import 했을 때 동일 패치가 필요한가? 필요 없다면 (a)+(b) 가 확정된다.

---

## 7. 불확실 항목

추측 없이 그대로 둠. 확정에 필요한 후속 확인 항목 포함.

| # | 불확실 항목 | 확정에 필요한 확인 |
|---|---|---|
| 1 | `ParseAxisSystem("yzx", ...)` 의 정확한 파싱 결과 | 사용 중인 FBX SDK 버전의 헤더/문서 확인, 또는 [§6-B] 로깅 |
| 2 | Y -90° / Z +90° 패치의 실제 적용 위치 | importer 외부에서 grep — 후보: actor blueprint, editor UI initialization, character/pawn component initial transform, asset post-import script |
| 3 | Maya source 의 정확한 front axis 부호 (+Z vs −Z) 와 right axis 부호 | source FBX 의 `GlobalSettings.GetOriginalUpAxis()` 와 `GetAxisSystem()` 로깅, 또는 DCC tool 에서 export 설정 확인 |
| 4 | Ahri.fbx / Galio.fbx 의 실제 GlobalSettings 값 | binary FBX 이므로 텍스트 grep 불가. FBX QuickViewer / Autodesk FBX Review / Python `fbx` SDK 또는 [§6-B] 로깅으로 확인 |
| 5 | 동일 import 경로로 다른 자산(BaseHuman.fbx, Lumi.fbx, Stonefbx.fbx)도 같은 증상을 보이는지 | 각 자산을 import 하여 결과 메시의 자세·거울상 여부 비교 |
| 6 | `DeepConvertScene` 호출 자체의 실패 가능성 | `ParseAxisSystem` 의 반환값(true/false), 그리고 `"[FBXImporter] Failed to parse engine axis system."` 로그 출력 여부 확인 |
