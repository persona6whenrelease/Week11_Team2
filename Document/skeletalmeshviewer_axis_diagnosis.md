# SkeletalMeshViewer 축 틀어짐 진단

## 0. 증상 요약 및 확정 관찰

### 증상
- SkeletalMeshViewer (FBX 더블클릭 시 뜨는 전용 viewer) 가 import 된 skeletal mesh 를 축이 틀어진 상태로 렌더링한다.
- 같은 mesh 가 main level viewport (drag-drop 으로 배치) 에서는 정상으로 보인다.
- 틀어지는 방향은 **파일마다 다르다.**
- FBX importer 의 좌표계 변환은 직전 작업에서 수정 완료. 본 진단의 대상이 아니다.

### 진단 전제
- 두 viewer 는 **동일한 SkeletalMeshComponent 렌더링 경로**를 공유한다 → 차이는 경로가 아니라 경로에 넘기는 입력(혹은 그 외 요소)에 있다.
- 파일별 제각각 틀어짐 → viewer 의 고정 변환 오류(고정 카메라/projection/박힌 회전)는 모든 파일을 같은 방향으로 틀어지게 할 것이므로 단독 원인이 아니다.

### 사용자의 주 가설
> Main viewer 는 root bone 위에 actor / scene-component / world-transform 체인을 두어 root 의 초기 회전을 흡수/정규화한다. SkeletalMeshViewer 는 root 를 원점에 직접 두므로 파일별 초기 회전이 그대로 노출된다.

→ 본 진단은 이 가설을 **코드로 검증**하고, 결과를 보고한다.

---

## 1. Main viewer 의 skeletal mesh 배치 경로

**파일:** `KraftonEngine/Source/Editor/Viewport/FLevelViewportLayout.cpp`

Drag-drop 처리에 두 분기가 존재한다.

### 1-A. `SkeletalMeshContentItem` 경로 (line 1200-1228)

콘텐츠 브라우저에서 **saved/cached skeletal mesh 자산** 을 드래그할 때:

```cpp
// FLevelViewportLayout.cpp:1200-1228
else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SkeletalMeshContentItem"))
{
    FContentItem ContentItem = *reinterpret_cast<const FContentItem*>(payload->Data);
    const FString MeshPath = FPaths::ToUtf8(ContentItem.Path.wstring());
    USkeletalMesh* SkeletalMesh = FMeshManager::LoadSkeletalMesh(MeshPath);
    ...
    AActor* NewActor = Cast<AActor>(FObjectFactory::Get().Create(AActor::StaticClass()->GetName(), Editor->GetWorld()));
    USkeletalMeshComponent* SkeletalMeshComponent = NewActor->AddComponent<USkeletalMeshComponent>();
    NewActor->SetRootComponent(SkeletalMeshComponent);     // ← SkeletalMeshComponent 자체가 root
    SkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);
    Editor->GetWorld()->AddActor(NewActor);

    FVector SpawnLocation(0, 0, 0);
    FPoint MP = { ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y };
    if (TryComputePlacementLocation(GetActiveViewportSlotIndex(), MP, SpawnLocation))
    {
        NewActor->SetActorLocation(SpawnLocation);          // ← world location 만 설정
    }
}
```

- Actor 와 component 위에 **추가 변환을 거치는 단계는 없다.**
- `SetActorLocation` 은 **translate 만** 적용한다 (rotation 손대지 않음).
- SkeletalMeshComponent 의 relative rotation 은 **identity**.

### 1-B. `FBXContentItem` 경로 (line 1229-1315)

FBX 파일 자체를 드래그할 때:

```cpp
// FLevelViewportLayout.cpp:1229-1315 (요약 발췌)
UFBXSceneAsset* SceneAsset = FMeshManager::LoadFbxScene(FbxPath);
...
auto NewActor = Cast<AActor>(FObjectFactory::Get().Create(...));
USceneComponent* RootComponent = NewActor->AddComponent<USceneComponent>();
NewActor->SetRootComponent(RootComponent);                  // ← 빈 USceneComponent 가 root

for (const FFBXSceneComponentDesc& Desc : SceneAsset->GetSceneComponents())
{
    if (Desc.Type == EFBXSceneComponentType::SkeletalMesh)
    {
        ...
        USkeletalMeshComponent* SkeletalMeshComponent = NewActor->AddComponent<USkeletalMeshComponent>();
        SkeletalMeshComponent->AttachToComponent(RootComponent);
        //SkeletalMeshComponent->SetRelativeLocation(Desc.RelativeTransform.GetLocation());
        //SkeletalMeshComponent->SetRelativeRotation(Desc.RelativeTransform.ToQuat());   // ← 주석 처리
        //SkeletalMeshComponent->SetRelativeScale(Desc.RelativeTransform.GetScale());
        SkeletalMeshComponent->SetSkeletalMesh(SkeletalMeshes[Desc.SkeletalMeshAssetIndex]);
    }
}
...
NewActor->SetActorLocation(SpawnLocation);
```

- root 는 **빈 USceneComponent** (identity transform).
- SkeletalMeshComponent 는 그 child 로 attach 되며, **`SetRelative*` 호출은 모두 주석 처리** 되어 동작하지 않는다.
- 즉 SkeletalMeshComponent 의 world rotation = `RootComponent.WorldRotation × SkeletalMeshComponent.RelativeRotation` = identity × identity = **identity**.
- `Desc.RelativeTransform` 은 `ImportMeta.Nodes[SkeletonMeta.RootNodeId].LocalTransform` (`FBXImporter.cpp:85-87`) 으로 채워져 있으나, 본 경로에서 사용되지 않는다.

### 1-C. 결론

- Main viewer 의 두 경로 모두 SkeletalMeshComponent 를 **identity rotation** 으로 배치한다.
- **root bone 의 초기 회전을 흡수/상쇄/정규화하는 transform 체인은 어디에도 없다.**

---

## 2. SkeletalMeshViewer 배치 경로

**파일:** `KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorPreviewScene.cpp`

### 2-A. Preview scene 초기화 (line 18-56)

```cpp
void FSkeletalEditorPreviewScene::Ensure()
{
    ...
    PreviewWorld = UObjectManager::Get().CreateObject<UWorld>();
    ...
    PreviewActor = PreviewWorld->SpawnActor<AActor>();
    PreviewActor->bTickInEditor = true;

    PreviewMeshComponent = PreviewActor->AddComponent<USkeletalMeshComponent>();
    PreviewActor->SetRootComponent(PreviewMeshComponent);    // ← SkeletalMeshComponent 가 root
    ...
}
```

- Actor 가 world 원점에 spawn 되며 identity rotation.
- SkeletalMeshComponent 가 root component.

### 2-B. 메시 적용 (line 86-124)

```cpp
void FSkeletalEditorPreviewScene::SetPreviewMesh(USkeletalMesh* InMesh, bool bResetCamera)
{
    Ensure();
    ...
    PreviewMeshComponent->SetSkeletalMesh(InMesh);
    PreviewMeshComponent->SetBakedAnimPaused(true);
    PreviewMeshComponent->SetBakedAnimTime(0.0f);
    PreviewMeshComponent->SetBakedAnimPlaybackSpeed(1.0f);
    ...
    FSkeletalMesh* MeshAsset = InMesh ? InMesh->GetSkeletalMeshAsset() : nullptr;
    if (MeshAsset)
    {
        if (!MeshAsset->bBoundsValid)
        {
            MeshAsset->CacheBounds();
        }
        const FVector Center = MeshAsset->BoundsCenter;
        PreviewMeshComponent->SetRelativeLocation(FVector(-Center.X, -Center.Y, -Center.Z));  // ← translate 만
    }
    if (bResetCamera && PreviewViewportClient)
    {
        PreviewViewportClient->FrameMesh(MeshAsset);
    }
}
```

진입: `SkeletalMeshEditorTab::OpenFbxAsset` (`SkeletalMeshEditorTab.cpp:94-118`) →
```cpp
CurrentSceneAsset = FMeshManager::LoadFbxScene(FbxPath);                 // ← FBX 를 직접 로드
const TArray<USkeletalMesh*>& SkeletalMeshes = CurrentSceneAsset->GetSkeletalMeshes();
SelectedResourceIndex = 0;
PreviewSkeletalMesh = GetSelectedSkeletalMesh();
PreviewScene.SetPreviewMesh(PreviewSkeletalMesh);
```

- `SetRelativeLocation(-BoundsCenter)` 는 **translate 만**, rotation 은 identity.
- 자산은 항상 `FMeshManager::LoadFbxScene(FbxPath)` — **FBX 직접 로드**.

### 2-C. `FrameMesh` 의 카메라 거동 (line 547-562)

`SkeletalMeshViewerViewportClient.cpp`:

```cpp
void FSkeletalMeshViewerViewportClient::FrameMesh(const FSkeletalMesh* MeshAsset)
{
    if (!Camera || !MeshAsset) return;

    FVector Extent = MeshAsset->BoundsExtent;
    float Radius = Extent.Length();
    Radius = (std::max)(Radius, 1.0f);

    const FVector Target = FVector::ZeroVector;
    const float Distance = Radius * 2.5f;
    Camera->SetWorldLocation(Target + FVector(Distance, 0.0f, Distance * 0.35f));  // ← 고정 방향
    Camera->LookAt(Target);
}
```

- 카메라 위치는 항상 **`(Distance, 0, Distance*0.35)`** — `Distance` 만 mesh 크기에 비례, **방향(각도) 은 고정**.
- 즉 `FrameMesh` 는 per-file 카메라 *각도* 보정을 하지 않는다 (거리만 스케일).
- 따라서 "카메라가 mesh local-up 에 끌려가서 per-file 로 어긋난다" 는 시나리오는 **배제된다.**

### 2-D. 결론

- SkeletalMeshViewer 도 SkeletalMeshComponent 를 **identity rotation** 으로 배치한다.
- `FrameMesh` 의 카메라 각도는 고정.
- **placement-transform 측면에서 main viewer 와 동등하다.** main viewer 가 root rotation 을 흡수하지 못하는 만큼 SkeletalMeshViewer 도 흡수하지 못한다 — 그러나 그 역도 마찬가지이다.

---

## 3. Root bone 초기 transform 의 파일별 차이

### 3-A. 자료구조

- **`FBoneInfo`** (`KraftonEngine/Source/Engine/Asset/Animation/Core/AnimationTypes.h:43-58`):
  ```cpp
  struct FBoneInfo {
      FString Name;
      int32   ParentIndex = -1;
      FMatrix LocalBindPose = FMatrix::Identity;
      FMatrix InverseBindPose = FMatrix::Identity;
  };
  ```
- **`FSkeleton`** (`Skeleton.h:7-21`): `TArray<FBoneInfo> Bones`. Root 는 index 0 (`ParentIndex == -1`).
- **`FSkeletalMesh`** (`SkeletalMeshAsset.h:36-88`): 자산 레벨에 별도 "asset root rotation" 필드 없음. 임시 `Bones` 만 들고 있다가 `FSkeleton` 으로 옮겨짐.

### 3-B. Root bone 의 `LocalBindPose` 산출

`KraftonEngine/Source/Engine/Asset/Import/FBX/Builder/FbxSkeletalMeshAssembler.cpp:103-171`:

```cpp
OutMesh.Bones.resize(SkeletonMeta.BoneIds.size());
TArray<FMatrix> BoneBindInSkeletonSpace;
BoneBindInSkeletonSpace.resize(SkeletonMeta.BoneIds.size(), FMatrix::Identity);

const FMatrix InvSkeletonRootBindGlobal = FMatrix::Identity;    // ← 항상 Identity

for (int32 SkeletonBoneIndex = 0; SkeletonBoneIndex < ...; ++SkeletonBoneIndex)
{
    const int32 BoneId = SkeletonMeta.BoneIds[SkeletonBoneIndex];
    const FFbxBoneMeta &BoneMeta = ImportMeta.Bones[BoneId];

    FBoneInfo &BoneInfo = OutMesh.Bones[SkeletonBoneIndex];
    BoneInfo.Name = BoneMeta.Name;
    BoneInfo.ParentIndex = ...;

    BoneBindInSkeletonSpace[SkeletonBoneIndex] =
        BoneMeta.BindGlobalMatrix * InvSkeletonRootBindGlobal;   // ← root: == BindGlobalMatrix
    BoneInfo.InverseBindPose = BoneBindInSkeletonSpace[SkeletonBoneIndex].GetInverse();
}

for (int32 SkeletonBoneIndex = 0; ...; ++SkeletonBoneIndex)
{
    FBoneInfo     &BoneInfo = OutMesh.Bones[SkeletonBoneIndex];
    const FMatrix &BoneGlobalInSkeletonSpace = BoneBindInSkeletonSpace[SkeletonBoneIndex];

    if (BoneInfo.ParentIndex >= 0) {
        BoneInfo.LocalBindPose =
            BoneGlobalInSkeletonSpace * BoneBindInSkeletonSpace[BoneInfo.ParentIndex].GetInverse();
    } else {
        BoneInfo.LocalBindPose = BoneGlobalInSkeletonSpace;       // ← root: local == global
    }
}
```

- `BoneMeta.BindGlobalMatrix` 는 `FbxMetaParser.cpp:1131, 1369-1371` 에서 FBX 의 `EvaluateGlobalTransform` 결과로 채워진다.
- **Root bone 의 `LocalBindPose` = FBX 의 root bone `BindGlobalMatrix`** (그대로).
- DCC export 설정·리깅 차이에 따라 이 행렬은 **파일마다 다르다**.

### 3-C. Render 경로에서의 root LocalBindPose 사용

`KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.cpp`:

```cpp
// ResetBonePoseToBindPose (line 85-112)
for (int32 BoneIndex = 0; BoneIndex < ...; ++BoneIndex)
{
    LocalBonePoseMatrices[BoneIndex] = (*Bones)[BoneIndex].LocalBindPose;   // ← root 포함 그대로 복사
}

// RebuildMeshSpaceBoneMatrices (line 344-375)
for (int32 i = 0; i < ...; ++i)
{
    const int32 ParentIndex = (*Bones)[i].ParentIndex;
    MeshSpaceBoneMatrices[i] =
        (ParentIndex >= 0 && ParentIndex < i)
            ? LocalBonePoseMatrices[i] * MeshSpaceBoneMatrices[ParentIndex]
            : LocalBonePoseMatrices[i];                                       // ← root: mesh-space == local
}
```

- root 의 회전이 **mesh-space 의 시작점**이며, 자식 bone 과 vertex skinning 전반에 그대로 전파된다.
- 어디에서도 root LocalBindPose 를 정규화·상쇄하지 않는다.

### 3-D. 결론

- root bone 의 초기 회전은 **파일마다 다르다** (FBX BindGlobalMatrix 직결).
- import 후 자산의 외형 orientation 은 이 root 회전에 직접 의존한다.

### 3-E. 직접 비교 (수행하지 않음 — 검증 필요)

- 본 진단에서 Ahri/Galio 등의 실제 `OutMesh.Bones[0].LocalBindPose` 값은 로깅하지 않았다.
- 행렬 비교가 필요하면 §5-(e) 검증 절차로 수행할 것.

---

## 4. 판정

### 4-A. 주 가설은 코드와 일치하지 않는다 — **불성립**

근거 종합:
- §1-C: main viewer 의 어느 drag-drop 경로도 root bone 회전을 흡수하지 않는다. SkeletalMeshComponent 의 relative/world rotation 은 항상 identity 이다 (`Desc.RelativeTransform.ToQuat()` 적용은 주석 처리).
- §2-D: SkeletalMeshViewer 도 동일하게 identity rotation 으로 배치한다. `FrameMesh` 의 카메라는 고정 각도.
- 즉 두 viewer 의 **placement transform 및 카메라 각도는 동등** 하며, "main viewer 의 transform 체인이 root 회전을 흡수한다" 는 설명은 코드와 맞지 않다.

### 4-B. 그렇다면 왜 main viewer 는 정상으로 보이고 SkeletalMeshViewer 는 틀어지는가?

두 viewer 의 placement 가 동등하므로, 차이는 **양쪽 viewer 에 넘겨지는 `USkeletalMesh` 객체(또는 그 안의 bind pose)** 에 있을 수밖에 없다. 다음 후보가 남는다:

#### (a) 자산 로딩 경로의 차이 — **현재 가장 유력**
- Main viewer 의 `SkeletalMeshContentItem` 경로 (§1-A): `FMeshManager::LoadSkeletalMesh(MeshPath)` — saved/cached skeletal mesh 자산을 로드.
- Main viewer 의 `FBXContentItem` 경로 (§1-B) 와 SkeletalMeshViewer (§2-B): `FMeshManager::LoadFbxScene(FbxPath)` — FBX 를 직접 import.
- `FMeshManager` 헤더 주석(`MeshManager.h:4`): "사용자가 요청한 경로가 OBJ인지, FBX 씬 캐시의 하위 에셋 참조인지 판단하고 적절한 [로딩을 수행한다]" → cache 경로와 fresh import 경로가 분리됨.
- 만약 사용자가 main viewer 에서 평소 **saved skeletal mesh 자산**(이전 import + 사용자 수동 보정의 결과)을 드래그한 반면 SkeletalMeshViewer 는 **항상 fresh FBX import** 라면, **두 viewer 는 서로 다른 USkeletalMesh 객체** 를 보고 있다. saved 쪽 bind pose 와 fresh 쪽 bind pose 가 다르면 외형이 다르게 나타난다.

#### (b) Importer 수정 이전 saved 자산이 그대로 사용 중일 가능성
- 직전 작업에서 FBX importer 의 좌표계 변환이 수정되었으나, 이미 디스크에 캐시된 saved 자산은 **수정 이전 importer + 사용자 수동 보정**의 결과일 수 있다.
- main viewer (saved 로드 시) 는 이 "이전 보정본" 을 보고 정상으로 인식. SkeletalMeshViewer (fresh import) 는 새 importer 결과 + 사용자 보정 없음 → 다른 외형.
- 이 경우 "main viewer 가 정상" 이라는 인식은 *이전 보정본이 우연히 잘 어울려 보이는 결과* 일 가능성이 있다.

#### (c) 카메라/시야 각도에 의한 인지적 차이 — **약한 후보**
- main viewer 의 사용자 조작 카메라는 일반적인 view 로 정렬되어 mesh 의 틸트가 잘 안 보일 수 있다.
- SkeletalMeshViewer 는 `FrameMesh` 가 카메라를 `(Distance, 0, Distance*0.35)` 에서 원점을 보도록 고정. mesh 가 자산-로컬 공간에서 누워 있으면 같은 카메라 각도에서 더 두드러져 보인다.
- 그러나 "파일별 제각각 틀어짐" 은 이 단독으로는 설명되지 않는다 (카메라가 고정되어 있으므로 모든 파일에 같은 방향 편향이 적용된다). per-file 차이는 §3-D 의 자산 자체 bind pose 차이에서 와야 한다.

### 4-C. 누락된 코드 위치는 없다

"흡수 단계가 main viewer 에 있고 SkeletalMeshViewer 에 빠져 있다" 라는 가설을 그대로 받아 누락 위치를 보고할 수는 없다 — **양쪽 모두 그런 단계가 없다.** 누락이 아니라 *입력 데이터(USkeletalMesh) 의 차이* 가 원인일 가능성이 가장 크다.

---

## 5. 검증 방법 제안

원인을 확정하기 위한 추가 확인 절차 (사용자 작업).

**(a) Main viewer 에서 어떤 콘텐츠 아이템을 드래그했는지 확인**
- 콘텐츠 브라우저에서 "정상으로 보이는" skeletal mesh 의 아이템 타입이 `SkeletalMeshContentItem` (saved) 인지 `FBXContentItem` (FBX 전체) 인지 확인.
- saved 라면 §4-B-(a)/(b) 가 강하게 지지된다.

**(b) 같은 USkeletalMesh 를 양쪽 viewer 에 로드해 비교**
- 같은 경로의 USkeletalMesh path 를 main viewer 와 SkeletalMeshViewer 양쪽에 넘겨 결과 비교.
- 동일하면 viewer 자체 차이는 0 이고 자산 차이가 원인.
- 다르면 다른 미발견 요인이 있음 (re-investigate).

**(c) 캐시 무효화 + 재 import 검증**
- saved skeletal mesh 자산 캐시 파일(.skel 등) 을 삭제.
- 같은 FBX 로 다시 import 한 후 main viewer 결과 확인.
- 이전과 같이 정상이면 §4-B-(b) 가 약화 (수정된 importer 결과 자체가 정상).
- 틀어지면 §4-B-(b) 가 강하게 지지 (이전 saved 가 보정본이었던 것).

**(d) Root LocalBindPose 로깅**
- `FbxSkeletalMeshAssembler.cpp` line 169 직후 다음 한 줄을 임시 추가 (진단용, 본 작업 이후 제거):
  ```cpp
  UE_LOG("[Diag] Root LocalBindPose for asset %s = %s", ..., OutMesh.Bones[0].LocalBindPose.ToString().c_str());
  ```
- 틀어지는 파일 2개 이상 + 정상 파일 1개 이상에 대해 `LocalBindPose` 의 회전 성분을 비교.
- 모두 identity 라면 root bone 의 회전은 원인이 아님 → 다른 요인 재조사 필요.
- 파일마다 다르고 그 차이가 관측된 틸트 방향과 일치하면 §3-D 가 확정된다.

**(e) Mesh 자산 비교 (메타데이터)**
- saved skeletal mesh asset 의 직렬화 포맷이 bone bind pose 를 포함하는지 확인. 포함하지 않거나 다르게 저장되면, fresh import 와 saved load 결과가 본질적으로 다를 수 있음.

---

## 6. 불확실 항목

추측 없이 그대로 둠. 확정에 필요한 후속 확인을 함께 기재.

| # | 불확실 항목 | 확정 방법 |
|---|---|---|
| 1 | 사용자가 main viewer 에서 평소 어떤 콘텐츠 아이템 타입을 드래그하는지 (`SkeletalMeshContentItem` vs `FBXContentItem`) | §5-(a) |
| 2 | `FMeshManager::LoadSkeletalMesh` 의 캐시 정책과 saved 자산이 언제 생성되었는지 (importer 수정 전/후) | `MeshManager.cpp` / `.h` 본문 정독 + 디스크의 saved 자산 파일 mtime 확인 |
| 3 | saved skeletal mesh 자산 직렬화 포맷이 bone bind pose 를 보존하는지 | saved 포맷 직렬화 코드 확인 |
| 4 | "main viewer 정상" 관찰이 모든 파일에 대해 검증되었는지, 일부 파일에 한정된 인상인지 | §5-(b) — 동일 자산 비교 |
| 5 | `FMeshManager::LoadFbxScene` 가 같은 FBX 에 대해 호출마다 결정론적인 결과를 주는지 (캐시/재호출 거동) | `MeshManager` 본문 정독 |
| 6 | 본 진단에서 Ahri/Galio 의 실제 root `BindGlobalMatrix` 값은 측정하지 않음 | §5-(d) 로깅으로 확정 |
