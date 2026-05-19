# SkeletalMeshViewer root X축 180° 출처 추적

## 0. 증상 및 계측값

- SkeletalMeshViewer (FBX 더블클릭 시 뜨는 전용 viewer) 에서 skeletal mesh 를 열고 **root 본(index 0)** 을 선택하면 Transform 패널에 다음 값이 표시된다:
  - Location: `(0, 0, 0)`
  - **Rotation: `(180, -0, 0)`** — X축 180° 순수 회전
  - Scale: `(1, 1, 1)`
- 즉 `PreviewMeshComponent->GetLocalBonePoseMatrices()[0]` 에 X축 180° 순수 회전(det = +1)이 들어 있다.
- 같은 mesh 를 main level viewport 에 배치하면 root 본이 identity 로 보이고 외형도 정상.
- FBX importer 는 직전 작업에서 수정 완료된 상태 (`FBXImporter.cpp:347-351` 의 `FbxAxisSystem(eZAxis, eParityOdd, eLeftHanded)` 변환). 본 진단은 importer 를 대상으로 하지 않는다.

본 진단의 목표: SkeletalMeshViewer 전용 경로에서 X-180° 가 `LocalBonePoseMatrices[0]` 에 들어가는 **정확한 코드 위치**를 특정한다.

---

## 1. SkeletalMeshViewer 전용 경로 스캔 결과 — 하드코딩 X-180° 없음

다음 파일들을 전문 정독·grep:
- `KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorPreviewScene.cpp` (135 lines, 전체)
- `KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorPreviewScene.h`
- `KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.cpp` (479 lines, 전체)
- `KraftonEngine/Source/Editor/Viewport/SkeletalMeshViewerViewportClient.cpp` (883 lines)

검색 패턴: `180`, `-180`, `1.5707`(주의: π/2 이므로 90°), `3.1415`, `PI`, `XM_PI`, `M_PI`, `RotationX`, `XMMatrixRotationX`, `FromAxisAngle`, `MakeRotationFromEulerXYZ`, `FRotator(180`, `SetBoneLocalPose(0,`, `LocalBonePoseMatrices[0]` 직접 write, 주석 처리된 줄까지 포함.

**결과:**
- root 본에 X축 180° 회전을 명시적으로 적용하는 라인은 **codebase 전체에 존재하지 않는다.**
- `SkeletalEditorPreviewScene.cpp:41` 의 `PreviewDirectionalLightActor->SetActorRotation(FRotator(15.0f, 180.0f, 0.0f))` 는 directional light 의 actor 회전(조명 방향 설정)이며, mesh/skeleton 과 무관하다.
- 따라서 X-180° 는 **데이터 또는 평가 경로에서 동적으로 들어온다**.

---

## 2. X-180° 가 `LocalBonePoseMatrices[0]` 에 들어가는 흐름 추적

### 2-A. FBX 열림 시 초기화

`KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.cpp:94-122`:
```cpp
bool FSkeletalMeshEditorTab::OpenFbxAsset(const FString& FbxPath)
{
    ...
    CurrentSceneAsset = FMeshManager::LoadFbxScene(FbxPath);
    ...
    SelectedResourceIndex = 0;
    PreviewSkeletalMesh = GetSelectedSkeletalMesh();
    PreviewScene.SetPreviewMesh(PreviewSkeletalMesh);   // ← line 118
    ...
}
```

`SkeletalEditorPreviewScene.cpp:86-124` 의 `SetPreviewMesh`:
```cpp
PreviewMeshComponent->SetSkeletalMesh(InMesh);          // ← bind pose 로 초기화
PreviewMeshComponent->SetBakedAnimPaused(true);
PreviewMeshComponent->SetBakedAnimTime(0.0f);
PreviewMeshComponent->SetBakedAnimPlaybackSpeed(1.0f);
...
PreviewMeshComponent->SetRelativeLocation(FVector(-Center.X, -Center.Y, -Center.Z));
```

`SkinnedMeshComponent.cpp:47-72` 의 `SetSkeletalMesh` 는 마지막에 `ResetBonePoseToBindPose()` 를 호출 → `LocalBonePoseMatrices[i] = Bones[i].LocalBindPose` (`SkinnedMeshComponent.cpp:103-106`).

이 시점까지는 root 가 bind pose 의 root 값. `AnimInstance` 는 **nullptr** (아직 안 만들어짐).

### 2-B. UI 첫 프레임 — `RenderAnimationPlaybackPanel` 가 자동으로 `SetAnimation` 호출

`SkeletalMeshEditorTab.cpp:64-74`:
```cpp
void FSkeletalMeshEditorTab::RenderLeftPanel()
{
    RenderResourcePanel();
    ImGui::Separator();
    RenderBonePanel();                  // ← line 68
}
```

`SkeletalMeshEditorTab.cpp:176-227` 의 `RenderBonePanel`:
```cpp
if (ImGui::BeginChild("##SkeletalMeshBoneHierarchy", ImVec2(0.0f, 0.0f), false))
{
    RenderAnimationPlaybackPanel();     // ← line 183
    ...
}
```

`SkeletalMeshEditorTab.cpp:229-334` 의 `RenderAnimationPlaybackPanel` 핵심 (line 248-255):
```cpp
int32 SequenceIndex = std::clamp(GetCurrentAnimSequenceIndex(), 0, SequenceCount - 1);
FString CurrentAnimSequencePath;
UAnimSequence* CurrentSequence = FMeshManager::FindAnimSequenceForSkeletalMesh(
    CurrentSceneAsset, PreviewSkeletalMesh, SequenceIndex, &CurrentAnimSequencePath);
if (CurrentSequence && PreviewMeshComponent->GetAnimation() != CurrentSequence)
{
    PreviewMeshComponent->SetAnimation(CurrentSequence);   // ← 자동 attach
}
```

- FBX 안에 animation sequence 가 있고 (`FindAnimSequenceForSkeletalMesh` 가 nullptr 이 아닐 때), 현재 component 의 animation 이 다른 값이면 자동으로 `SetAnimation` 을 호출한다.
- 사용자가 Play 를 누르기 전, FBX 를 열자마자 첫 ImGui 프레임에서 발동.
- Ahri/Galio FBX 는 animation sequence 를 포함하므로 이 분기가 항상 실행된다.

### 2-C. `SetAnimation` 이 AnimInstance 를 생성

`KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:70-80`:
```cpp
void USkeletalMeshComponent::SetAnimation(UAnimationAsset *NewAnimToPlay)
{
    AnimToPlay = NewAnimToPlay;

    EnsureAnimInstance();                                  // ← AnimInstance 생성

    if (auto *Single = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        Single->SetAnimation(Cast<UAnimSequence>(NewAnimToPlay));
    }
}
```

`SkeletalMeshComponent.cpp:36-50` 의 `EnsureAnimInstance`:
```cpp
void USkeletalMeshComponent::EnsureAnimInstance()
{
    if (AnimInstance) return;
    switch (AnimationMode)
    {
    case EAnimationMode::AnimationStateMachine:
        AnimInstance = UObjectManager::Get().CreateObject<UAnimStateMachineInstance>(this);
        break;
    case EAnimationMode::AnimationSingleNode:
    default:
        AnimInstance = UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>(this);
        break;
    }
    AnimInstance->InitializeAnimation(ResolveSkeletonFromMesh(SkeletalMesh));
}
```

→ AnimInstance 가 nullptr 이 아니게 됨.

### 2-D. `TickComponent` 가 매 프레임 `EvaluateGraph` + `ApplyEvaluatedPose` 실행

`SkeletalMeshComponent.cpp:131-147`:
```cpp
void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                           FActorComponentTickFunction &ThisTickFunction)
{
    if (!AnimInstance)
    {
        return;                                            // ← AnimInstance 없으면 조기 종료
    }

    AnimInstance->Update(DeltaTime);
    AnimInstance->EvaluateGraph();
    ApplyEvaluatedPose(AnimInstance->GetOutputLocalPose());

    BakedAnimTime = AnimInstance->GetCurrentTime();
}
```

- `SetBakedAnimPaused(true)` 라도 `EvaluateGraph()` 자체는 실행된다 (Update 단계에서만 시간 누적이 멈춤).
- `CurrentTime` 은 `SetBakedAnimTime(0.0f)` 로 0 인 채 유지되므로 **animation t=0 의 포즈**가 매 프레임 계산된다.

### 2-E. `EvaluateGraph` 가 animation t=0 키프레임을 산출

`KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:28-50`:
```cpp
void UAnimSingleNodeInstance::EvaluateGraph()
{
    if (!Skeleton)
    {
        OutputLocalPose.clear();
        return;
    }

    const size_t BoneCount = Skeleton->GetBones().size();
    if (OutputLocalPose.size() != BoneCount)
    {
        OutputLocalPose.resize(BoneCount);
    }

    FAnimEvalContext Ctx;
    Ctx.Skeleton       = Skeleton;
    Ctx.TimeSeconds    = CurrentTime;                       // ← 0
    Ctx.DeltaTime      = LastDeltaTime;
    Ctx.OwningInstance = this;

    SequencePlayer.Evaluate(Ctx, OutputLocalPose);          // ← root 포함 모든 본의 t=0 키프레임 산출
}
```

- `SequencePlayer.Evaluate` 는 `OutputLocalPose[i]` 에 각 본의 local TRS 를 채운다 (i=0 은 root).
- root track 의 t=0 키프레임이 X-180° 회전을 갖고 있다면 `OutputLocalPose[0].Rotation` 에 그것이 담긴다.

### 2-F. `ApplyEvaluatedPose` 가 `LocalBonePoseMatrices[0]` 를 덮어씀

`KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.cpp:440-476`:
```cpp
void USkinnedMeshComponent::ApplyEvaluatedPose(const TArray<FTransform>& EvaluatedLocalPose)
{
    if (EvaluatedLocalPose.empty())
    {
        return;
    }

    const size_t N = EvaluatedLocalPose.size();

    if (LocalBonePoseMatrices.size() != N)
    {
        LocalBonePoseMatrices.resize(N);
        BoneOverrideMask.assign(N, false);
        for (size_t i = 0; i < N; ++i)
        {
            LocalBonePoseMatrices[i] = EvaluatedLocalPose[i].ToMatrix();
        }
    }
    else
    {
        const size_t MaskSize = BoneOverrideMask.size();
        for (size_t i = 0; i < N; ++i)
        {
            const bool bOverridden = (i < MaskSize) ? BoneOverrideMask[i] : false;
            if (!bOverridden)
            {
                LocalBonePoseMatrices[i] = EvaluatedLocalPose[i].ToMatrix();   // ← root 덮어씀
            }
        }
    }
    ...
}
```

- `BoneOverrideMask[0]` 가 false (기본값) 이면 root 의 `LocalBonePoseMatrices[0]` 가 animation t=0 의 root 키프레임 값으로 **매 프레임** 덮어쓰여진다.

### 2-G. Transform 패널이 그 값을 읽어 표시

`SkeletalMeshEditorTab.cpp:336-423` 의 `RenderTransformPanel` (line 362-377 핵심):
```cpp
const TArray<FMatrix>& LocalPoses = PreviewMeshComponent->GetLocalBonePoseMatrices();
...
const FMatrix& LocalPose = LocalPoses[SelectedBoneIndex];
const FVector LocationVector = LocalPose.GetLocation();
const FVector RotationVector = GetRotationEulerNoScale(LocalPose);   // ← Euler 추출
const FVector ScaleVector = LocalPose.GetScale();
...
bChanged |= ImGui::DragFloat3("Rotation", Rotation, ...);
```

- 패널이 표시하는 `(180, -0, 0)` 은 위 2-F 단계에서 매 프레임 채워진 `LocalBonePoseMatrices[0]` 의 Euler 분해 결과이다.

---

## 3. Main viewer 와의 차이 대조

### 3-A. Main viewer 의 두 drag-drop 경로 — `SetAnimation` 자동 호출 없음

`KraftonEngine/Source/Editor/Viewport/FLevelViewportLayout.cpp:1200-1228` (SkeletalMeshContentItem):
```cpp
USkeletalMesh* SkeletalMesh = FMeshManager::LoadSkeletalMesh(MeshPath);
...
USkeletalMeshComponent* SkeletalMeshComponent = NewActor->AddComponent<USkeletalMeshComponent>();
NewActor->SetRootComponent(SkeletalMeshComponent);
SkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);                  // ← bind pose 까지만
Editor->GetWorld()->AddActor(NewActor);
...
NewActor->SetActorLocation(SpawnLocation);
```

`KraftonEngine/Source/Editor/Viewport/FLevelViewportLayout.cpp:1229-1315` (FBXContentItem) — SkeletalMesh 분기 (line 1271-1292):
```cpp
USkeletalMeshComponent* SkeletalMeshComponent = NewActor->AddComponent<USkeletalMeshComponent>();
SkeletalMeshComponent->AttachToComponent(RootComponent);
//SkeletalMeshComponent->SetRelativeLocation(Desc.RelativeTransform.GetLocation());
//SkeletalMeshComponent->SetRelativeRotation(Desc.RelativeTransform.ToQuat());
//SkeletalMeshComponent->SetRelativeScale(Desc.RelativeTransform.GetScale());
SkeletalMeshComponent->SetSkeletalMesh(SkeletalMeshes[Desc.SkeletalMeshAssetIndex]);   // ← bind pose 까지만
```

- 두 경로 모두 `SetAnimation` 을 **자동 호출하지 않는다**.
- 따라서 `EnsureAnimInstance` 도 호출되지 않고, `AnimInstance` 는 nullptr 인 채 유지된다.
- `TickComponent` 의 `if (!AnimInstance) { return; }` 으로 매 프레임 조기 종료 → `LocalBonePoseMatrices` 는 `ResetBonePoseToBindPose` 결과 (root identity bind pose) 그대로.

### 3-B. 단계별 차이 요약

| 단계 | SkeletalMeshViewer | Main viewer |
|---|---|---|
| 자산 로드 | `FMeshManager::LoadFbxScene` | `LoadSkeletalMesh` 또는 `LoadFbxScene` |
| `AddComponent<USkeletalMeshComponent>` | 예 | 예 |
| `SetSkeletalMesh` | 예 | 예 |
| `ResetBonePoseToBindPose` 실행 | 예 (LocalBonePoseMatrices[0] = bind pose root) | 예 (LocalBonePoseMatrices[0] = bind pose root) |
| **`SetAnimation` 자동 호출** | **예 (`SkeletalMeshEditorTab.cpp:252-255`)** | **아니오** |
| **AnimInstance 생성** | **예 (자동)** | **아니오** (기본 상태) |
| Tick 마다 `EvaluateGraph` + `ApplyEvaluatedPose` | 예 | **아니오 (조기 종료)** |
| 결과 `LocalBonePoseMatrices[0]` | **animation t=0 의 root 키프레임 (X-180°)** | **bind pose root (identity)** |

---

## 4. 판정

### 4-A. X-180° 의 주입 위치 (SkeletalMeshViewer-specific)

- **가장 가까운 SkeletalMeshViewer 전용 트리거:** `SkeletalMeshEditorTab.cpp:252-255` 의 자동 `PreviewMeshComponent->SetAnimation(CurrentSequence)` 호출.
- 이 호출이 없으면 main viewer 와 동일하게 `AnimInstance` 가 만들어지지 않아 `LocalBonePoseMatrices[0]` 가 bind pose root 로 유지된다.
- 즉 **두 viewer 의 차이는 "자동 SetAnimation 의 유무" 한 줄에 집중**된다.

### 4-B. X-180° 값 자체의 출처 (가설, 검증 필요)

- 코드에는 X-180° 를 명시적으로 적용하는 라인이 없다.
- 값은 `SequencePlayer.Evaluate` 가 산출한 `OutputLocalPose[0]` 에서 온다.
- 가장 유력한 후보: **import 된 animation sequence 의 root track 첫 키프레임이 (bind pose 대비) X-180° 만큼 회전된 값을 갖고 있다**.
- 이는 흔히 발생한다: Maya 측에서 character 의 bind pose 와 첫 animation frame 의 자세가 다르게 authoring 되면, FBX SDK 가 animation track 을 evaluate 했을 때 t=0 가 bind 와 일치하지 않는다. importer 의 `FbxAxisSystem` 변환은 bind 와 animation 의 *상대 차이* 를 보존하므로 변환 후에도 그 차이가 그대로 들어온다.
- 확정 방법은 §5 참조.

### 4-C. 사용자 전제 검토 — 부분적으로 부정확

사용자 전제: "Ahri.fbx / Galio.fbx 는 외부 표준 에셋이므로 FBX 파일 자체에 X축 180° 가 박혀 있을 수 없다."

- 이 전제는 **bind pose 에는 부합**한다 — bind pose 는 보통 표준 자세 (T/A-pose) 로 authoring 되어 있다.
- 그러나 **animation track 에는 자연스럽게 박혀 있을 수 있다**. 캐릭터 animation 의 첫 프레임은 종종 bind pose 와 다른 자세 (idle 시작 자세 등) 로 시작하며, 그 차이가 root 의 회전으로 나타날 수 있다.
- 본 진단은 X-180° 가 animation track t=0 에서 자연스럽게 발생했다고 추정하며, "FBX 에 박혀 있을 수 없다" 는 전제는 *bind pose 한정* 으로만 성립한다고 본다.

### 4-D. 의도 여부

- `SkeletalMeshEditorTab.cpp:252-255` 의 자동 `SetAnimation` 은 의도된 UX (Editor 가 FBX 를 열 자마자 첫 animation sequence 를 preview component 에 attach 하여 사용자가 Play 만 누르면 재생되게).
- 부작용: paused 상태라도 `EvaluateGraph` 가 매 프레임 실행되어 root 가 animation t=0 으로 덮어쓰여진다.

### 4-E. 제거 시 영향 범위

| 변경 | 효과 | 부작용 |
|---|---|---|
| `SkeletalMeshEditorTab.cpp:252-255` 의 자동 SetAnimation 제거 | SkeletalMeshViewer 에서 사용자가 Play 누르기 전까지 bind pose 유지. X-180° 안 보임. | "FBX 열자마자 첫 시퀀스가 attach 되어 있음" UX 가 사라짐. 사용자가 sequence combo 에서 명시 선택해야 SetAnimation 발동 (line 276-282). |
| `BoneOverrideMask[0] = true` 로 root 만 protect | root 가 animation 평가에 영향받지 않음. 다른 본은 정상 평가. | 사용자가 의도적으로 root animation 을 재생하고자 할 때 무효화됨. |
| Animation track 의 root key 자체 수정 | 근본 데이터 보정. | importer / animation parser 영역. 본 진단 범위 밖. |
| 카메라 / 기즈모 / 본 피킹 호환성 | 영향 없음. 해당 시스템들은 `LocalBonePoseMatrices[i]` 를 일반 본으로 다루며 root 값의 특정 값을 가정하지 않는다 (`SkeletalMeshViewerViewportClient.cpp:547-562` 의 `FrameMesh` 도 mesh bounds 만 사용). | — |

---

## 5. 검증 방법 제안

가설 확정 또는 반박에 필요한 사용자 작업.

**(a) 자동 `SetAnimation` 차단 실험**
- `SkeletalMeshEditorTab.cpp:252-255` 의 if 블록을 임시 주석 처리.
- 빌드 후 같은 FBX 를 SkeletalMeshViewer 에서 열어 root 본의 Rotation 이 `(180,-0,0)` → `(0,0,0)` 으로 바뀌는지 확인.
- 그렇다면 §4-A 확정.

**(b) `OutputLocalPose[0]` 로깅**
- `AnimSingleNodeInstance.cpp:49` 의 `SequencePlayer.Evaluate(Ctx, OutputLocalPose);` 직후 root 의 Euler 를 임시 `UE_LOG` 출력.
- t=0 에서 X-180° 가 평가되면 animation track 자체에 그 회전이 들어 있는 것이 확정된다.

**(c) `FbxAnimationParser` 검토**
- `KraftonEngine/Source/Engine/Asset/Import/FBX/Parser/FbxAnimationParser.cpp` 의 root track 변환 로직 확인 (`FBXUtil::ConvertFbxMatrix(LocalMatrix).ToQuat()` 등) — line 60, 270 부근.
- importer 의 axis 변환 이후에도 root local rotation 이 X-180° 를 만들어내는 path 가 있는지 점검.

**(d) Animation 없는 FBX 비교**
- animation sequence 가 없는 단순 skeletal mesh (또는 anim 을 제거한 FBX) 를 SkeletalMeshViewer 에서 열어 root 가 identity 로 보이는지 확인.
- 그렇다면 X-180° 는 animation track 의존성이 확정된다.

**(e) Main viewer 의 root 본 값 확인**
- main viewer 에 같은 FBX 를 배치한 뒤 `PreviewMeshComponent->GetLocalBonePoseMatrices()[0]` 를 로깅 (또는 main viewer 측에도 본 transform 패널을 임시 추가).
- 정말 identity 인지 확인하여 "main viewer 정상" 관찰을 코드 레벨에서 재확인.

**(f) `bake anim` 사용처 추적**
- `SetBakedAnimPaused`, `SetBakedAnimTime` 가 `Update` 단계만 멈추고 `EvaluateGraph` 는 실행한다는 사실을 코드로 한 번 더 확인 (`UAnimSingleNodeInstance::Update` 본문 정독). 본 진단은 헤더 시그니처와 `TickComponent` 호출 순서를 근거로 추정함.

---

## 6. 불확실 항목

| # | 항목 | 확정 방법 |
|---|---|---|
| 1 | X-180° 가 FBX animation track t=0 자체에 박혀 있는지, 아니면 importer (`FbxAnimationParser`) 의 axis 변환에서 합성된 것인지 | §5-(b), §5-(c) |
| 2 | `RenderAnimationPlaybackPanel` 의 자동 `SetAnimation` (line 252-255) 이 의도된 영구 동작인지 임시 코드인지 (코드 주석 부재) | 작성자 의도 확인 또는 git blame |
| 3 | `AnimSequenceEditorTab` 등 다른 SkeletalMeshViewer 호출 경로도 동일하게 자동 `SetAnimation` 을 하는지 (영향 범위 확장 여부) | `AnimSequenceEditorTab.cpp:218-223, 288` 부근 정독 — 본 진단에서는 한 경로(`SkeletalMeshEditorTab`) 만 추적 |
| 4 | `UAnimSingleNodeInstance::Update` 가 paused 상태에서 `CurrentTime` 을 0 으로 유지하는지의 정확한 거동 (`SetEvaluationTime` 호출이 어떻게 반영되는지 포함) | `AnimInstance.cpp` 및 `UAnimSingleNodeInstance::Update` 정독 |
| 5 | Animation 이 여러 개인 FBX 에서 `FindAnimSequenceForSkeletalMesh(..., 0, ...)` 가 첫 번째 sequence 를 결정론적으로 반환하는지 | `MeshManager` 의 해당 함수 정독 |
