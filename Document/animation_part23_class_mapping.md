# Engine/Asset/Animation 클래스 → 파트 2·3 기능 매핑

`Document/project_relation.md`의 **파트 2 (Animation Runtime Core)**, **파트 3 (Animation Logic System)** 을 구현할 때, 이미 작성되어 있는 `KraftonEngine/Source/Engine/Asset/Animation/` 하위 클래스/구조체들이 **어떤 기능에 어떤 식으로 사용되는지** 매핑·정리한 문서이다. 구현 코드는 포함하지 않는다.

---

## 1. Engine/Asset/Animation 클래스 인벤토리

| 심볼 | 위치 | 종류 | 핵심 데이터 / API |
|---|---|---|---|
| `FFrameRate` | `Core/AnimationTypes.h:22-38` | struct | `Numerator`/`Denominator`, `AsDecimal()` |
| `FBoneInfo` | `Core/AnimationTypes.h:43-58` | struct | `Name` / `ParentIndex` / `LocalBindPose` / `InverseBindPose` |
| `FRawAnimSequenceTrack` | `Core/AnimationTypes.h:66-81` | struct | `PosKeys` / `RotKeys` / `ScaleKeys` / `LocalMatrixKeys` |
| `FBoneAnimationTrack` | `Core/AnimationTypes.h:86-97` | struct | `Name`(FName) + `InternalTrack`(FRawAnimSequenceTrack) |
| `FAnimationCurveData` | `Core/AnimationTypes.h:104-112` | struct | (자리만 잡힌 빈 구조 — 추후 확장 예정) |
| `FAnimNotifyEvent` | `Notify/AnimNotify.h:18-47` | struct | `TriggerTime` / `Duration` / `NotifyName`, **`IsTriggeredBetween(prev, curr, length)`** |
| `FSkeleton` | `Core/Skeleton.h:7-21` | struct | `PathFileName` / `Bones` / `BoneNameToIndex` + `Serialize` / `RebuildBoneNameToIndex` |
| `USkeleton` | `Core/Skeleton.h:23-47` | UObject | `FSkeleton*` 보유 + `GetBones()` / `GetMutableBones()` / `FindBoneIndexByName()` |
| `UAnimationAsset` | `Core/AnimSequence.h:19-24` | UObject (base) | `virtual GetPlayLength()` |
| `UAnimDataModel` | `Core/AnimSequence.h:29-62` | UObject | `BoneAnimationTracks` / `PlayLength` / `FrameRate` / `NumberOfFrames` / `NumberOfKeys` / `CurveData` + getter/setter |
| `UAnimSequenceBase` | `Core/AnimSequence.h:67-83` | UObject (UAnimationAsset 파생) | `UAnimDataModel* DataModel` 보유 + `GetPlayLength()` override |
| `UAnimSequence` | `Core/AnimSequence.h:88-116` | UObject (UAnimSequenceBase 파생) | `SequenceName` / `SkeletonAssetPath` / `Notifies` + `AddNotify` / `GetNotifies` / `IsValidSequence` |

---

## 2. 파트 2 — Animation Runtime Core 매핑

| 과제 세부 항목 | 사용할 Engine/Asset/Animation 심볼 | 역할 |
|---|---|---|
| `UAnimInstance` (신규) | `UAnimSequence*`, `USkeleton*`, `UAnimDataModel*` | 인스턴스가 보유할 입력 핸들. 시퀀스(재생 대상) / 스켈레톤(본 계층) / 데이터 모델(키) 3개를 reference로 들고 있게 됨 |
| `UAnimSingleNodeInstance` (신규) | `UAnimSequence*` (단일) | 단일 시퀀스 재생 placeholder. `UAnimSequence::GetPlayLength()`로 길이 취득 |
| `PlayAnimation(UAnimSequence*, bLoop)` | `UAnimSequence` | 단일 진입점의 인자 타입. UE 시그니처와 매칭 |
| Animation time update | `UAnimSequenceBase::GetPlayLength()` / `UAnimDataModel::GetPlayLength()` | `BakedAnimTime` 누적 / `fmod`에 필요한 길이 (loop·reverse 양쪽 공통) |
| Keyframe sampling | `UAnimDataModel::GetBoneAnimationTracks()` → `FBoneAnimationTrack::InternalTrack` → `FRawAnimSequenceTrack::PosKeys`/`RotKeys`/`ScaleKeys`/`LocalMatrixKeys` | 본별 키 배열을 시간으로 인덱싱해서 사이값 보간. **TRS 분리 키가 이미 존재하므로 향후 rotation slerp 도입 가능** (현 `LocalMatrixKeys`만 쓰는 element-wise lerp의 한계 해소 경로 확보) |
| Keyframe sampling 인덱스 계산 | `UAnimDataModel::GetFrameRate()` (→ `FFrameRate::AsDecimal()`), `GetNumberOfFrames()`, `GetNumberOfKeys()` | `FrameA` / `FrameB` / `Blend` 계산용 |
| Local Pose 계산 | (출력 자리 — `UAnimInstance::OutputLocalPose`에 보관) + 입력: `FRawAnimSequenceTrack` | 본별 local matrix 배열을 결과로 들고 있음 |
| Component Space Pose 계산 (FK) | `USkeleton::GetBones()` → `FBoneInfo::ParentIndex` | 부모 체이닝 (`MeshSpace[i] = Local[i] * MeshSpace[ParentIndex]`)에 필요한 `ParentIndex` 제공. 정렬(`ParentIndex < i`) 가정도 동일 |
| Skinning Matrix 생성 | `USkeleton::GetBones()` → `FBoneInfo::InverseBindPose` | `SkinMatrix = InverseBindPose * MeshSpaceBone` 의 `InverseBindPose` 소스 |
| Bone 이름 ↔ 인덱스 매핑 | `USkeleton::FindBoneIndexByName(BoneName)` | 트랙(`FBoneAnimationTrack::Name` = FName)을 본 인덱스로 resolve. **이름 기반 트랙 → 인덱스 기반 포즈 배열 연결의 핵심** |
| Reverse Play | `UAnimSequence::GetPlayLength()` | 음수 speed 보정용 길이만 필요 (런타임 상태는 컴포넌트 측) |
| Bind pose / Reference pose fallback | `FBoneInfo::LocalBindPose` | 시퀀스가 없거나 초기화 시 fallback 포즈 |

---

## 3. 파트 3 — Animation Logic System 매핑

| 과제 세부 항목 | 사용할 Engine/Asset/Animation 심볼 | 역할 |
|---|---|---|
| Animation Blending | `UAnimSequence` × 2개 (혹은 N개), 각각의 `UAnimDataModel::GetBoneAnimationTracks()` | 두 시퀀스의 같은 본 트랙(`FBoneAnimationTrack::Name`)을 매칭해 weighted blend. `USkeleton::FindBoneIndexByName`로 본 인덱스 정렬 |
| Animation State Machine | 각 state가 `UAnimSequence*` 참조 | state ↔ 재생 시퀀스 매핑의 키. `UAnimSequence::GetSequenceName()`로 식별/디버깅 |
| State별 animation 연결 | `UAnimSequence*` (state에 1:1 또는 1:N) | state 정의의 payload |
| Transition 조건 처리 | `UAnimSequence::GetPlayLength()`, `UAnimSequenceBase::GetPlayLength()` | "현재 클립 끝났는가" / "x초 경과" 등 시간 기반 transition 조건 평가용 |
| Lua Animation State Machine | `UAnimSequence`, `USkeleton`, `UAnimDataModel` (UObject 셋) | Lua 바인딩 대상. `DECLARE_CLASS`로 reflection 등록되어 있어 LuaBindings에 노출 가능. state graph DSL이 참조할 anim asset 타입 |
| Anim Notify Runtime — 데이터 소스 | `UAnimSequence::GetNotifies()` → `TArray<FAnimNotifyEvent>` | 시퀀스에 등록된 노티파이 목록 조회 |
| Anim Notify Runtime — 트리거 판정 | **`FAnimNotifyEvent::IsTriggeredBetween(PrevTime, CurrTime, SequenceLength)`** | **이미 구현되어 있음.** 루프 감김(`prev > curr`) 케이스까지 처리. 매 틱 `UAnimInstance`가 prev/curr time을 넘기면 끝 |
| Notify dispatch — 이벤트 식별자 | `FAnimNotifyEvent::NotifyName` (FName) | dispatch key. `TDelegate<const FAnimNotifyEvent&>::BroadCast` 인자로 그대로 전달 |
| Notify dispatch — 페이로드 | `FAnimNotifyEvent::TriggerTime`, `Duration` | 리스너 측에서 정확한 트리거 시각 / 지속 구간 활용 (예: 효과음 길이 매칭) |
| Notify 추가 (런타임 / 에디터) | `UAnimSequence::AddNotify(const FAnimNotifyEvent&)` | 타임라인 에디터(파트 5)에서도 동일 API 사용 |
| 시퀀스 유효성 검사 | `UAnimSequence::IsValidSequence()` | State 진입 / blend 입력 검증에 사용 (`DataModel` 존재 + `PlayLength > 0` + 트랙 비어있지 않음) |

---

## 4. 매핑 요약 한눈에

```
[데이터 소유]
USkeleton(=FSkeleton + FBoneInfo[])  ───┐
                                         ├──> UAnimInstance (신규)
UAnimSequence ── UAnimDataModel ──┐      │
   │             (FBoneAnimationTrack[]  │
   │              = FName + FRawAnimSequenceTrack)
   │                                     │
   └─ FAnimNotifyEvent[] ─ IsTriggeredBetween() ─> Notify dispatch (파트 3)

[런타임 흐름]
UAnimInstance::Update(dt)
  └─ UAnimDataModel keys 샘플링 (FRawAnimSequenceTrack)
       └─ Local Pose 행렬 배열
            └─ USkeleton::Bones[i].ParentIndex 로 FK
                 └─ USkeleton::Bones[i].InverseBindPose 곱해 Skinning Matrix
```

---

## 5. 주의 / 공백

- `FAnimationCurveData`는 현재 **빈 자리만 잡혀있음** → 파트 3에서 curve 기반 transition / blend weight가 필요해지면 신규 구현 필요.
- `FRawAnimSequenceTrack`은 `PosKeys` / `RotKeys` / `ScaleKeys` 와 `LocalMatrixKeys`가 **동시에 존재**. 현재 컴포넌트의 element-wise lerp가 `LocalMatrixKeys` 기반이라면, TRS 분리 + slerp로 옮길 때 어느 쪽을 정본으로 둘지 결정 필요.
- `UAnimInstance` / `UAnimSingleNodeInstance` / Blending / State Machine **본체는 본 매핑 대상이 아님** (기존 자산이 아니므로 별도 신규 작성).
