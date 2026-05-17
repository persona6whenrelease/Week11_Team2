# Animation 추출~재생 파이프라인 검증 계획서

본 계획서는 "검증을 어떻게 할지"의 명세이다. 코드/계측/UI 구현은 본 계획 승인 이후 별도 작업으로 분리한다.

---

## Context

파트 1(Animation Asset Pipeline) + 파트 2(Animation Runtime Core) 구현이 끝났다. 자산 추출(FBX importer) ↔ 자산 직렬화(`UAnimSequence` / `UAnimDataModel`) ↔ 런타임 평가(`UAnimInstance` / `UAnimSingleNodeInstance` / `AnimGraph`) ↔ 컴포넌트 ↔ SceneProxy ↔ DrawCommand ↔ 뷰포트의 8단계 경로가 코드 차원에서는 모두 등장했지만, 각 단계 경계가 실제로 데이터를 정합성 있게 주고받는지는 **확인되지 않았다**. 본 계획은 그 검증을 어떻게 수행할지를 정의한다.

검증 대상 파이프라인 (단방향 데이터 흐름):

```
FBXImporter
  └─[1]→ BoneTrack(FBoneAnimationTrack / FRawAnimSequenceTrack)
        └─[2]→ UAnimSequence(+ UAnimDataModel)
              └─[3]→ UAnimInstance(추상) → UAnimSingleNodeInstance(구체)
                    └─[4]→ USkeletalMeshComponent (LocalBonePose → MeshSpace)
                          └─[5]→ FSkeletalMeshSceneProxy
                                └─[6]→ FDrawCommand
                                      └─[7]→ SkeletalMeshViewerViewportClient (화면 표시)
```

---

## 1. 파이프라인 현황 (Audit)

각 경계의 **호출 관계 + 입출력 데이터 타입 + 끊김/미구현 표기**를 사실 기반으로 정리한다. 본 절은 발견만 수록하고 수정 방향은 적지 않는다.

### 1.A 단계별 호출 관계와 데이터

| # | 단계 | 핵심 함수 / 파일·라인 | 입력 | 출력 | 상태 |
|---|---|---|---|---|---|
| 1 | FBX → BoneTrack | `FFbxAnimationParser::ParseSkeletonAnimations` (`Asset/Import/FBX/Parser/FbxAnimationParser.cpp:79-262`) | FBX 노드 계층 + 애니메이션 레이어 0 | `TArray<FBoneAnimationTrack>` + `UAnimDataModel`(PlayLength/FrameRate/NumberOfFrames/NumberOfKeys) | 부분 — 본문 1.B 참조 |
| 2 | BoneTrack → UAnimSequence | `FbxAnimationParser.cpp:176-261` + `FBXImporter.cpp:168-212` | `UAnimDataModel` + Skeleton 메타 | `UAnimSequence`(SequenceName, SkeletonAssetPath, DataModel ref) | 정상 |
| 3 | UAnimSequence → UAnimInstance | `USkeletalMeshComponent::PlayAnimation` (`Component/SkeletalMeshComponent.cpp:37-58`) → `UAnimSingleNodeInstance::SetAnimation` (`Asset/Animation/Core/AnimSingleNodeInstance.cpp:14-19`) | `UAnimationAsset*` (실제 `UAnimSequence*`) | `UAnimSingleNodeInstance` 보유 + `TrackToBoneIndex` 캐시 | 정상 |
| 4a | AnimInstance → LocalBonePose | `UAnimInstance::Update` + `EvaluateGraph` (`AnimInstance.cpp:26-110`), `FAnimGraphNode_SequencePlayer::Evaluate` (`AnimGraph.cpp:44-126`) → `USkeletalMeshComponent::TickComponent` (`SkeletalMeshComponent.cpp:126-143`) → `LocalBonePoseMatrices` 복사 | `FAnimEvalContext`(DataModel, TrackToBoneIndex, CurrentTime) | `TArray<FMatrix> LocalBonePoseMatrices` | 조건부 — 본문 1.B 참조 |
| 4b | LocalBonePose → MeshSpacePose (FK) | `USkinnedMeshComponent::RebuildMeshSpaceBoneMatrices` (`SkinnedMeshComponent.cpp:317-348` [미확인 정확 라인]) | `LocalBonePoseMatrices` + `FBoneInfo::ParentIndex` | `MeshSpaceBoneMatrices` | 정상 |
| 4c | MeshSpacePose → SkinnedVertices (CPU 스킨) | `USkinnedMeshComponent::SkinVerticesToReferencePose` + `UploadSkinnedVertices` (`SkinnedMeshComponent.cpp:308-411` [미확인 정확 라인]) | `MeshSpaceBoneMatrices` + `FBoneInfo::InverseBindPose` + asset Vertices + bone weights | `SkinnedVertices` + `RuntimeMeshBuffer`(GPU VB) | **[끊김]** — 본문 1.B 참조 |
| 5 | Component → SceneProxy | `USkinnedMeshComponent::CreateSceneProxy` (`SkinnedMeshComponent.cpp:24-28`) | 컴포넌트 라이브 참조 | `FSkeletalMeshSceneProxy*` | 정상 |
| 6 | SceneProxy → DrawCommand | `FSkeletalMeshSceneProxy::RebuildSectionDraws` (`Render/Proxy/SkeletalMeshSceneProxy.cpp:58-105` [미확인 정확 라인]) → `FDrawCommandBuilder::BuildCommandForProxy` (`Render/Renderer/DrawCommandBuilder.cpp:123-195` [미확인 정확 라인]) | proxy section draws + `RuntimeMeshBuffer` | `FDrawCommand` (VB/IB/material 바인딩) | 정상 (단 입력 VB가 갱신되어야 의미 있음 — 4c와 의존) |
| 7 | DrawCommand → Viewport | `EditorSkeletalMeshViewerWidget::TickPreviewScene` (`Editor/UI/EditorSkeletalMeshViewerWidget.cpp:704-714` [미확인 정확 라인]) → `PreviewWorld->Tick(...)` → 표준 렌더 패스 | preview world + proxy | 화면 출력 | 정상 (월드 틱 자체는 동작) |

> 라인 번호 중 "[미확인 정확 라인]"으로 표기한 것은 Explore agent가 보고했으나 본인이 직접 그 라인까지 열어 1차 확인은 못 한 항목이다. 실제 검증 수행 시 재확인이 필요하다.

### 1.B 끊김 / 미구현 / 부분 항목 (중요)

이 목록은 검증의 **선행 조건**과 직결된다.

1. **TRS 분리 키 미충전** — `[끊김]`
   - 위치: `FbxAnimationParser.cpp:246` — 매 프레임 `Tracks[BoneIndex].InternalTrack.LocalMatrixKeys[FrameIndex] = LocalMatrices[BoneIndex];` 한 줄만 실행. `PosKeys` / `RotKeys` / `ScaleKeys`는 **resize 호출도 없고 추가도 없어 size 0 상태로 직렬화**된다.
   - 영향: 파트 2에서 구현된 `FAnimGraphNode_SequencePlayer::Evaluate`(`AnimGraph.cpp`)가 Option B(TRS+Slerp)로 동작하며, 각 트랙에 대해 `Raw.PosKeys.size() >= NumKeys` 체크 후 조건 불충족 시 **bind pose를 유지**한다. 즉 현재 자산을 그대로 import 후 재생하면 **모든 본이 bind pose로 고정** — 시퀀스 데이터 자체는 디스크에 있어도 화면상 움직임이 없다.
   - 두 개 중 하나가 필요(본 계획 범위 외, 별도 결정 사항):
     - (a) FBX importer가 TRS 키를 채우도록 확장
     - (b) `FAnimGraphNode_SequencePlayer`에 `LocalMatrixKeys` fallback 경로 추가

2. **매 프레임 스킨 트리거 누락** — `[끊김]`
   - 위치: `USkeletalMeshComponent::TickComponent` (`SkeletalMeshComponent.cpp:126-143`) — `LocalBonePoseMatrices = AnimInstance->GetOutputLocalPose(); RebuildMeshSpaceBoneMatrices();` 까지만 수행. `SkinVerticesToReferencePose()` / `UploadSkinnedVertices()` / `EnsureRuntimeResources()` **호출 없음**.
   - 비교: `USkinnedMeshComponent::SetBoneLocalPose`(`SkinnedMeshComponent.cpp:94-118`)와 `ResetBonePoseToBindPose`(`:67-92`)는 두 함수 모두 호출함.
   - 영향: `LocalBonePoseMatrices`가 매 틱 갱신돼도 GPU VB(`RuntimeMeshBuffer`)는 bind pose 또는 마지막 `SetBoneLocalPose` 시점 상태에서 멈춤. **포즈가 화면에 반영되지 않는다**.

3. **Notify 적재 미구현** — `[미구현]`
   - 위치: `FbxAnimationParser.cpp` 어디에도 `UAnimSequence::AddNotify` 호출 없음. FBX의 마커/이벤트 → Notify 변환 경로가 없다.
   - 영향: `UAnimSequence::GetNotifies()`는 항상 빈 배열. 파트 2에 구현된 `UAnimInstance::Update`의 Notify 판정 패스는 **항상 빈 결과**만 산출. 본 항목은 파트 3 dispatch 작업 전까지 검증 대상에서 제외 가능.

4. **뷰어 애니메이션 컨트롤 UI 부재** — `[없음]`
   - 위치: `EditorSkeletalMeshViewerWidget.cpp:1197-1262` [미확인 정확 라인] — `RenderAnimationPlaybackPanel` 함수 자체는 존재하지만 Play/Pause/Scrub/Speed 컨트롤 본문이 주석 상태.
   - 영향: 검증을 위한 수동 조작 입력 경로가 없다. 본 계획 4절(UI 명세)이 정의할 대상.

5. **GPU 스킨 경로 부재** — 정보성
   - GPU 스킨용 상수 버퍼/SRV 업로드 코드가 어디에도 없음. 현 파이프라인은 **CPU 스킨 전용**. 본 계획은 이 사실 위에 검증을 정의한다.

### 1.C 데이터 타입 한눈에

```
FBX 노드(트리)
  ─[FBXUtil::ConvertFbxMatrix]→ FMatrix
       └─ FRawAnimSequenceTrack.LocalMatrixKeys[F]  (PosKeys/RotKeys/ScaleKeys 는 비어 있음)
            └─ FBoneAnimationTrack{ FName, FRawAnimSequenceTrack }
                 └─ UAnimDataModel.BoneAnimationTracks: TArray<FBoneAnimationTrack>
                      └─ UAnimSequence{ SequenceName, SkeletonAssetPath, DataModel*, Notifies(빈) }
                           └─ UAnimSingleNodeInstance.CurrentSequence (ref)
                                └─ UAnimInstance.OutputLocalPose: TArray<FMatrix>
                                     └─ USkinnedMeshComponent.LocalBonePoseMatrices: TArray<FMatrix>
                                          └─ .MeshSpaceBoneMatrices: TArray<FMatrix>
                                               └─ .SkinnedVertices: TArray<FVertexPNCTT>
                                                    └─ RuntimeMeshBuffer (GPU VB)
                                                         └─ FDrawCommand → 화면
```

---

## 2. 검증 체크포인트 정의

파이프라인 경계마다 **무엇이 맞으면 통과인가**를 표로 정리. 버그가 어디서 깨졌는지 이분 탐색이 가능하도록 단계 경계마다 항목을 둔다.

| ID | 체크포인트 | 판정 기준 | 확인 방법 | 통과 조건 |
|---|---|---|---|---|
| CP-1 | 추출 직후 본 개수 | `USkeleton::GetBones().size()` = FBX 본 노드 수 | importer 직후 `USkeleton` 로깅 / 어셋 로드 후 본 수 출력 | 두 값 일치 |
| CP-2 | 추출 직후 키프레임 수 | `UAnimDataModel::GetNumberOfFrames()` ≥ 2 이고 `GetNumberOfKeys()` ≈ frames × bones | importer 직후 `UAnimDataModel` 메타 로깅 | NumberOfFrames > 1; NumberOfKeys = NumberOfFrames × BoneCount 또는 합리적 변형 |
| CP-3 | 추출 직후 FrameRate / PlayLength | `FrameRate.AsDecimal() > 0`, `PlayLength = NumberOfFrames / FrameRate ± ε` | DataModel.GetFrameRate() / GetPlayLength() 로깅 | 두 값 일관 |
| CP-4 | 트랙별 키 배열 길이 | 사용 정본의 size ≥ NumberOfKeys (TRS 정본이면 PosKeys/RotKeys/ScaleKeys 모두; 매트릭스 정본이면 LocalMatrixKeys) | `for tIdx: Tracks[tIdx].InternalTrack.* size 출력` | 누락 0 트랙 |
| CP-5 | 트랙 → 본 인덱스 resolve | `TrackToBoneIndex[tIdx] ≥ 0` for all tIdx (누락 본 0) | `SetAnimation` 직후 `TrackToBoneIndex` 덤프 | -1 entry 개수 = 0 |
| CP-6 | t=0 포즈 == 첫 키 | `OutputLocalPose[BoneIdx]` 와 첫 키 행렬의 element-wise 거리 < ε | `SetEvaluationTime(0)` → `EvaluateGraph` 직후 비교 | max 행렬 원소차 < 1e-4 |
| CP-7 | t=PlayLength 포즈 == 마지막 키 | 위와 동일, frame index = NumKeys-1 | `SetEvaluationTime(PlayLength)` → `EvaluateGraph` | max 원소차 < 1e-4 |
| CP-8 | 중간 t 보간 단조성 | 두 키 사이 임의 t의 회전 각도 / 위치가 양쪽 키 값 범위 안 | t = 0.5 × keyInterval에서 평가 후, 위치는 양 키 box 안, 회전 각은 양 키 사이 (Slerp 검증) | 모든 본에 대해 위치 박스 인 + 회전 각 between |
| CP-9 | Notify 판정 누락 / 중복 없음 | `IsTriggeredBetween` 이 동일 Notify를 같은 회 재생 사이에 0회 또는 1회만 보고 | 단계 5절 시나리오 D 참조 | prev→curr 통과당 정확히 1회 |
| CP-10 | 루트 본 Component Space == Local | `MeshSpaceBoneMatrices[Root] == LocalBonePoseMatrices[Root]` | `RebuildMeshSpaceBoneMatrices` 직후 비교 | 동일 |
| CP-11 | 자식 본이 부모 변화 따라감 | 임의 비루트 본 j에 대해 `MeshSpace[j] == Local[j] * MeshSpace[ParentIndex[j]]` | 비교 | 모든 j 일치 |
| CP-12 | Skinning Matrix 개수 | `SkinMatrix[]` size == BoneCount | (계측 추가 필요 — 3절) | 정확히 일치 |
| CP-13 | bind pose 재생 시 메시 형태 유지 | `ResetBonePoseToBindPose()` 후 `SkinnedVertices` ≈ asset `Vertices` | 두 배열의 위치 평균 거리 비교 | 거리 < ε |
| CP-14 | 매 프레임 스킨 반영 | t를 변경하면 GPU에 업로드된 `RuntimeMeshBuffer` 내용이 바뀜 | 두 시점 사이 메시 모양이 화면에서 다름 + (선택) GPU readback diff | 시각 차이 + buffer hash 변화 |
| CP-15 | DrawCommand 본 수 무관 발행 | `FDrawCommand` 수 = mesh section 수, 본 수와 무관 | DrawCommand 로깅 | section count 일치 |
| CP-16 | 뷰포트에서 시간 스크럽 동작 | 스크럽 슬라이더 이동 시 CP-6/CP-7/CP-8이 시각적으로 일관 | UI 조작 + 시각 확인 + CP-6/7/8 자동 비교 | 시각 + 자동 모두 통과 |

**경계 설계 근거**: CP-1~CP-4는 자산 측 입출력 정합성, CP-5~CP-9는 평가(런타임) 정합성, CP-10~CP-12는 FK·스킨, CP-13~CP-16은 렌더·UI까지. 버그 발생 시 위에서부터 차례로 통과 여부를 보면 결함 위치가 좁혀진다.

---

## 3. 검증 계측 계획

> **본 절은 계획만 적는다. 실제 코드는 본 계획 승인 후 별도 작업으로 추가한다.**

원칙:
- **읽기 전용**: 계측은 기존 동작을 바꾸지 않는다(상태 mutation 금지).
- **토글 가능**: 빌드 매크로 또는 런타임 플래그(예: `bAnimValidationLoggingEnabled`)로 일괄 켜고 끌 수 있어야 한다.
- **위치 최소 침투**: 가능한 한 기존 함수의 진출구(끝부분) 또는 별도 helper에서 외부로 빼낸다.

### 3.A 계측 포인트와 형태

| 체크포인트 | 위치 후보 (계측 추가할 함수) | 출력 형태 |
|---|---|---|
| CP-1 ~ CP-4 | `FFbxAnimationParser::ParseSkeletonAnimations` 종료 직전 | 시퀀스 단위 로그: name / BoneCount / NumberOfFrames / NumberOfKeys / FrameRate / PlayLength / 트랙별 key 배열 size 요약 |
| CP-5 | `UAnimInstance::RebuildTrackToBoneIndex` 종료 직후 | track→bone 매핑 배열, -1 개수 |
| CP-6, CP-7, CP-8 | `UAnimInstance::EvaluateGraph` 종료 직후 (옵션 플래그 켜졌을 때만) | 본별 OutputLocalPose 요약(위치/스케일/쿼터니언). 무거우므로 디폴트 OFF |
| CP-9 | `UAnimInstance::Update` 마지막에 `TriggeredNotifiesThisFrame` 덤프 | NotifyName, prev/curr 시간 |
| CP-10, CP-11 | `USkinnedMeshComponent::RebuildMeshSpaceBoneMatrices` 종료 직후 | 본별 MeshSpace 행렬 (옵션) |
| CP-12 | `USkinnedMeshComponent::SkinVerticesToReferencePose` 진입 직후 | 계산된 SkinMatrix 배열 size |
| CP-13 | `ResetBonePoseToBindPose` 직후 vs asset Vertices 비교 hook | 거리 평균/최대값 |
| CP-14 | `UploadSkinnedVertices` 직후 | buffer hash(예: FNV-1a) 또는 첫 N개 정점 위치 다이제스트 |
| CP-15 | `FDrawCommandBuilder::BuildCommandForProxy` 종료 직후 | section count, 각 section indexCount |
| CP-16 | viewer panel 측 onScrubChanged 콜백 | 슬라이더 값 변화 + 적용된 `CurrentTime` |

### 3.B 비교 도구 (별도 유틸로 두는 것을 권장)

- `bool IsMatrixApproxEqual(const FMatrix& A, const FMatrix& B, float Tolerance)` — 모든 element 차이 < tol.
- `float QuatAngleBetween(const FQuat&, const FQuat&)` — CP-8 회전 단조성 판정.
- 위 두 유틸 모두 **읽기 전용**, 헤더 한 곳에 inline.

### 3.C 출력 채널

- 1차: 기존 `UE_LOG`/`std::cout` 류 — Explore 결과 `UE_LOG`가 `FbxAnimationParser.cpp:252`에서 이미 사용되고 있음. 동일 매크로 사용.
- 2차(선택): JSON 한 줄 덤프 → 별도 파일. CI/회귀 비교용. 본 계획에서는 1차만 필수.

---

## 4. 애니메이션 동작 UI 명세 (계획)

**본 절은 명세만 작성. 구현은 별도 작업.**

### 4.A 배치

- 부모: `EditorSkeletalMeshViewerWidget` (`Editor/UI/EditorSkeletalMeshViewerWidget.cpp` [미확인 정확 라인]).
- 위치: `RenderAnimationPlaybackPanel()` (현재 본문 주석) — 이 함수에 본 명세를 채워 넣는다.
- 인접 패널: `RenderResourcePanel`, `RenderBonePanel`, `RenderTransformPanel`. ImGui 도킹 구조를 그대로 따른다.
- 근거: 빈 자리(주석 처리된 컨트롤)가 이미 명시되어 있어, 위치 결정 비용 0.

### 4.B 컨트롤 명세

```
┌─ Animation Playback ────────────────────────────────────────────┐
│  Sequence: [PreviewMeshComponent->GetAnimation() name 표시]      │
│  ┌─[Play]─[Pause]─[Stop]─┐                                       │
│  │                         │ Loop  [✓]   Speed [───●───] 1.00x  │
│  └─────────────────────────┘  (Speed 슬라이더는 음수 영역 포함)  │
│                                                                   │
│  Time: [───●─────────────] 0.42 / 1.30 sec                       │
│        (스크럽 슬라이더; 0 ~ PlayLength)                          │
│  Frame: 12 / 39                                                   │
│  Bones: 56                                                        │
│  Active Notifies: (이번 프레임 트리거된 NotifyName 리스트)        │
└───────────────────────────────────────────────────────────────────┘
```

| 컨트롤 | 연결 대상 (UAnimSingleNodeInstance / UAnimInstance API) | 비고 |
|---|---|---|
| Play 버튼 | `AnimInstance->SetPaused(false); AnimInstance->SetLooping(LoopToggle)` (또는 `USkeletalMeshComponent::Play(bool)`) | Play 시점에 시퀀스가 set 안 돼 있으면 `PlayAnimation(AnimToPlay, ...)` 한 번 호출하는 entry helper 필요 [연결 entry 미구현] |
| Pause 버튼 | `AnimInstance->SetPaused(true)` | |
| Stop 버튼 | `USkeletalMeshComponent::Stop()` + `ResetTime()` | |
| Loop 토글 | `AnimInstance->SetLooping(bool)` | mirror: `bBakedAnimLooping` |
| Speed 슬라이더 (-3.0 ~ +3.0) | `AnimInstance->SetPlaybackSpeed(float)` | 음수 = reverse. 0 근처는 사실상 pause |
| Time 스크럽 | `AnimInstance->SetEvaluationTime(t)` + `EvaluateGraph()` 즉시 호출 후 컴포넌트 측 skin 트리거 | **1.B-2 끊김 의존** — skin 트리거가 매 틱 안 도는 한, 스크럽도 시각 반영 안 됨. 1.B-2 해결이 선행 |
| Sequence 이름 표시 | `Cast<UAnimSequence>(AnimToPlay)->GetSequenceName()` | read-only |
| Time / PlayLength 표시 | `AnimInstance->GetCurrentTime()` / `GetEffectivePlayLength()` (protected — `GetActiveDataModel()->GetPlayLength()` 경유 또는 helper 추가) | read-only |
| Frame Index / NumberOfFrames | `floor(CurrentTime * FrameRate)` / `DataModel->GetNumberOfFrames()` | read-only |
| Bones | `Skeleton->GetBones().size()` | read-only |
| Active Notifies | `AnimInstance->GetTriggeredNotifiesThisFrame()` (각 FName.ToString()) | 1.B-3 끊김으로 인해 디폴트 빈 리스트. UI 자체는 동작 |

### 4.C 스크럽 슬라이더의 검증 역할

CP-6 / CP-7 / CP-8(키프레임 보간 정합성)을 **육안 + 자동 비교**로 확인하는 핵심 도구. t를 0 → PlayLength로 이동하면서:
- t=0 / t=PlayLength 정확 일치 (CP-6, CP-7)
- 임의 중간 t에서 메시가 양쪽 키 범위 안의 자연스러운 보간 (CP-8)
- speed = -1.0에서 역재생 시 동일한 보간 곡선이 반대 방향 (Update의 음수 wrap 보정 검증)

---

## 5. 검증 수행 절차

본 계획서가 정의하는 **테스트 시나리오 순서**. 위에서 아래로 진행하며, 한 단계 실패 시 그 단계에서 멈추고 원인 추적(원인 불명 시 "원인 미상" 표기, 추측 금지).

### 시나리오 A — 자산 정합성 (CP-1 ~ CP-5)

1. 알려진 단순 FBX(예: 단일 본 + 회전 키 10프레임 / 30fps) 임포트.
2. importer 로그(`[FBXImporter] Baked animation sequence...`) 와 CP-1~CP-4 계측 출력 비교.
3. 임포트된 `.uasset`을 다시 로드한 뒤 동일 CP들 재확인 (직렬화 round-trip 정합성).
4. `USkeletalMeshComponent::PlayAnimation` 호출 후 CP-5 확인 (TrackToBoneIndex에 -1 없음).
5. 실패 항목 발생 시: 자산 측에서 멈춤. 6번 이하 건너뜀.

### 시나리오 B — 평가(샘플링) 정합성 (CP-6 ~ CP-9)

6. `SetEvaluationTime(0)` → CP-6 확인.
7. `SetEvaluationTime(PlayLength)` → CP-7 확인.
8. `SetEvaluationTime(중간값들)` → CP-8 확인 (위치 박스 인, 회전 각 between).
9. 루프 모드 + dt 누적으로 `Update`를 여러 번 호출, CP-9(Notify) 확인. (1.B-3 끊김으로 현재는 빈 결과만 — 통과 기준은 "빈 리스트 일관성"으로 한정).

### 시나리오 C — FK / 스킨 (CP-10 ~ CP-14)

10. `RebuildMeshSpaceBoneMatrices` 직후 CP-10, CP-11 확인.
11. `SkinVerticesToReferencePose` 직후 CP-12, CP-13 확인 (bind pose 입력 시).
12. 임의 t로 `EvaluateGraph` → `SkinVerticesToReferencePose` → `UploadSkinnedVertices` 순으로 강제 호출 후 CP-14 확인.
13. `TickComponent`만 호출했을 때 CP-14가 깨지는지 확인 (1.B-2 끊김의 영향 입증).

### 시나리오 D — 렌더 + UI (CP-15, CP-16)

14. DrawCommand 발행 후 CP-15 확인.
15. 4절 UI 패널이 구현된 상태에서 스크럽 슬라이더 이동 → CP-16 확인 (시각 일관 + CP-6/7/8 자동 통과).
16. Loop on + Speed 양수/음수 토글 → 시각적으로 정/역 재생 일관.

### 실패 시 원인 추적 방침

- 한 단계 실패 → 직전 단계 통과를 재확인 (이분 탐색).
- 두 시나리오 사이 끊김(예: 시나리오 B 통과, C 실패) → 그 경계의 1.B 항목을 우선 확인.
- 원인 후보가 둘 이상이면 **둘 다 적고** "원인 미상" 표기. 추측으로 단정 금지.

---

## 6. 선행 작업 / 의존성

**검증을 시작하기 전에 갖춰져야 할 것**들. 본 계획 자체는 검증을 수행하지 않는다.

| 선행 조건 | 종류 | 사유 |
|---|---|---|
| 1.B-1 (TRS 키 미충전) 해결 | 파이프라인 수정 | CP-6 이하 평가 검증이 의미를 가지려면 SequencePlayer가 실제 트랙 데이터를 쓸 수 있어야 함. 결정 사항(importer 확장 vs SequencePlayer fallback)은 본 계획 범위 외 |
| 1.B-2 (TickComponent 스킨 미트리거) 해결 | 파이프라인 수정 | CP-14 / CP-16 (시각 검증) 의 선결. 시나리오 C step 13 이후 단계 의미 있음 |
| 3절 계측 추가 | 계측 작업 | CP들의 자동 판정은 출력 채널 필요. 시각 일치만으로는 CP-6~CP-8 등 정량 판정 불가 |
| 4절 UI 구현 | UI 작업 | 시나리오 D(스크럽 검증)와 검증 작업의 운용성에 필요 |
| 알려진 단순 FBX 샘플 | 자산 | 시나리오 A의 비교 기준. 회전만, 위치만 분리된 단일 본 시퀀스 권장 |

**파이프라인 수정과 UI 추가의 분리 원칙**: 1.B-1 / 1.B-2(파이프라인 끊김 보강)와 4절(검증 UI)은 **각각 독립된 PR/작업**으로 분리한다. 두 가지가 묶이면 실패 원인 추적 시 변수가 늘어난다.

---

## 7. 미해결 항목 (Open Questions)

1. **OQ-A — 키 정본 결정**: 1.B-1 해결 방식을 importer 확장(TRS 채우기)으로 갈지, SequencePlayer fallback(LocalMatrixKeys 경로 추가)으로 갈지. 본 계획서 범위 외. 결정 전까지 시나리오 B는 "[차단]" 상태.
2. **OQ-B — Skin 트리거 위치**: 1.B-2를 `USkeletalMeshComponent::TickComponent`에 추가할지, `USkinnedMeshComponent` 측 onPostPoseUpdate 훅에 추가할지. 후자가 더 일반적이지만 후크가 존재하는지 [미확인].
3. **OQ-C — CP-8 보간 판정 허용 오차**: 회전 각도 between 판정에서 `tolerance` 값(예: 1e-3 rad)을 어떻게 정할지. FBX importer가 매트릭스 정본을 쓰면 SequencePlayer Slerp 결과와 미세 오차가 생길 수 있어 OQ-A와 연동.
4. **OQ-D — 활성 시퀀스 입력 경로**: 뷰어에서 어떤 자산 브라우저 / 콤보박스를 통해 시퀀스를 선택하는지 [미확인]. UI 명세 4.B에 "Sequence: 이름 표시" 만 있고 선택 UI가 없는 이유.
5. **OQ-E — CP-9 통과 정의의 실효성**: 1.B-3(Notify 미적재)이 해소되기 전까지 CP-9 통과 정의를 "빈 리스트 일관"으로 한정해도 좋은지. 파트 3 dispatch 작업 시점에 재정의 필요.
6. **OQ-F — 자동 vs 수동 판정**: CP들을 자동 판정(계측 + assert)으로 갈지, 1차는 수동 검사(눈+로그)만으로 갈지. 본 계획은 자동을 권장하나 비용이 클 수 있음.

---

## 본 계획서의 한계 / 검증 안 된 가정

- **[미확인]** 1.A 표의 일부 라인 번호(예: `SkinnedMeshComponent.cpp:317-348`, `EditorSkeletalMeshViewerWidget.cpp:1197+`)는 Explore agent 보고에 의존했고 본인이 그 라인까지 1차 확인 안 함. 시나리오 진행 전 재확인 필요.
- **[추정]** 1.B-1의 영향("화면상 움직임 없음")은 `FAnimGraphNode_SequencePlayer::Evaluate`의 size 가드 분기(`AnimGraph.cpp:108-119`)로부터 도출. 실제 viewer에서 빌드 후 동작 확인은 본 계획 범위 외.
- **[검증 안 된 가정]** `FBoneInfo::ParentIndex < i` 정렬은 FBX importer가 보장한다는 매핑 doc 기술을 그대로 신뢰. CP-11이 실제로 깨질 가능성은 본 계획에서 별도로 다루지 않음.
- **[검증 안 된 가정]** `USkeletalMesh::GetSkeleton()`이 컴포넌트에 `SetSkeletalMesh` 시점에 항상 유효 — `PlayAnimation` 호출 순서(mesh set → play)에 의존. 순서가 반대일 때의 동작은 검증 대상에서 빠짐.
- **[추정]** CP-14의 "buffer hash 비교"는 GPU readback 가능 여부에 의존. KraftonEngine에 D3D11 staging readback 경로가 있는지 [미확인]. 없다면 CP-14는 시각 비교만으로 대체.
- **[한계]** Notify dispatch(파트 3) 미존재로 인해 CP-9는 본 계획 범위 안에서 "빈 결과 일관성"까지만 판정. dispatch 도입 후 다시 정의 필요.
- **[한계]** 본 계획은 **단일 시퀀스 / SingleNode** 경로만 다룬다. 블렌딩 / 스테이트 머신(파트 3)이 도입되면 CP들이 추가/수정되어야 함.
- **[한계]** GPU 스킨 도입(만약 향후 채택) 시 CP-14 / CP-15가 전부 재설계됨.
