# Animation Notify 처리 로직 조사

## 요약
Notify 구조체는 `FAnimNotifyEvent` 한 종류로 단일 헤더에 정의되어 있고, 런타임 처리 로직은 `UAnimInstance::Update` 한 곳에 응집되어 있어 정의 → 처리 구간까지는 흐름이 명확하다. 그러나 처리 결과는 `TriggeredNotifiesThisFrame` 배열에 이름을 쌓는 데서 멈추며, 디스패치/콜백 단계와 viewer·component 측의 소비 호출이 모두 존재하지 않아 "처리 → 호출 지점" 연결이 끊긴 상태다. 또한 `Duration` 필드는 정의되어 있으나 Core에서 한 번도 읽지 않아 구간형(state/window) Notify의 begin/tick/end 의미는 런타임에 표현되지 않는다.

## Notify 구조체 인벤토리

| 구조체명 | 파일:줄 | 멤버 | 타입 분류 |
|---|---|---|---|
| `FAnimNotifyEvent` | [AnimNotify.h:18-47](../KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h) | `TriggerTime: float` (L20), `Duration: float` (L21), `NotifyName: FName` (L22) | 단일 구조체. 단발/구간 타입 구분 없음 |

보조:
- `IsTriggeredBetween(PreviousTime, CurrentTime, SequenceLength)` 메서드 — [AnimNotify.h:35-46](../KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h). `prev ≤ curr`일 때는 `(prev, curr]` 구간, `prev > curr`(루프 wrap)일 때는 `TriggerTime > prev OR TriggerTime ≤ curr` 조건으로 판정한다.
- 소유 컨테이너: `UAnimSequence::Notifies` (`TArray<FAnimNotifyEvent>`) — [AnimSequence.h:103-105, 116](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h).
- `Notify/` 디렉토리에 다른 파일 없음. 별도 .cpp 구현 파일 0개 (헤더 전용).
- **타입 수준 instant/state 구분 없음.** 상속도 enum도 없으며 `Duration` 값(0 vs >0)이 유일한 구분자다. 단, 아래 진단 1·2번에서 보듯 Duration 값을 실제로 활용하는 로직은 코드 어디에도 없다.

## Core 처리 로직 맵

| 단계 | 파일:줄 | 함수 시그니처 | 역할 |
|---|---|---|---|
| (a) 시간구간 판정 | [AnimNotify.h:35-46](../KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h) | `bool FAnimNotifyEvent::IsTriggeredBetween(float PreviousTime, float CurrentTime, float SequenceLength) const` | prev/curr 구간 안에 TriggerTime이 들어왔는지 루프 wrap 포함 판정 |
| (a) 활성 Notify 공급 (인터페이스) | [AnimInstance.h:81](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h) | `virtual const TArray<FAnimNotifyEvent> *UAnimInstance::GetActiveNotifies() const` (기본 `nullptr`) | 파생이 현재 활성 Notify 배열을 노출하는 hook |
| (a) 활성 Notify override | [AnimSingleNodeInstance.cpp:75-78](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp) | `const TArray<FAnimNotifyEvent> *UAnimSingleNodeInstance::GetActiveNotifies() const` | `CurrentSequence->GetNotifies()` 반환. `CurrentSequence`가 없으면 `nullptr` |
| (a) 트리거 수집 | [AnimInstance.cpp:73-84](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) | `void UAnimInstance::Update(float DeltaTime)` 내 inline 루프 | `GetActiveNotifies()` 순회 → `IsTriggeredBetween` 호출 → 트리거된 `NotifyName`을 `TriggeredNotifiesThisFrame`에 push |
| 시간 진행 / wrap | [AnimInstance.cpp:43-71](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) | 동일 `Update` 함수 | `PreviousTime = CurrentTime` 갱신 후 `NewTime = CurrentTime + DeltaTime * PlaybackSpeed`. Looping은 `fmod` 기반 + 음수 보정(L48-53), non-looping은 양/음 clamp + `bPaused=true` (L57-71) |
| 외부 노출 (폴링 API) | [AnimInstance.h:44](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h) | `const TArray<FName> &GetTriggeredNotifiesThisFrame() const` | 이번 프레임에 트리거된 Notify 이름 배열 반환 |
| (b) 디스패치/콜백 | — | **부재** | 코드/주석 모두 미구현 — [AnimInstance.cpp:73](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) 주석에 `"Notify 판정 (dispatch는 파트 3)"`로 명시 |
| (c) 구간형 begin/tick/end | — | **부재** | `Duration` 필드를 읽는 코드 없음 (grep 결과 정의 외 참조 없음) |
| 초기화 시 클리어 | [AnimInstance.cpp:14-21](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) | `UAnimInstance::InitializeAnimation` | `TriggeredNotifiesThisFrame.clear()` 후 bind pose 초기화 |
| 매 프레임 클리어 | [AnimInstance.cpp:26](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) | `UAnimInstance::Update` 진입부 | 이번 프레임 배열 초기화. paused 또는 length≤0이면 조기 return하여 채워지지 않음 |

관련: StateMachine transition 조건 평가 — [AnimGraph_StateMachine.cpp:20-43](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp), `static bool FAnimGraphNode_StateMachine::EvaluateConditions(...)`. `EAnimTransitionConditionKind::OnNotify`와 `::Custom`은 L37-39에서 즉시 `return false` (주석 `"파트 B 대기"`).

## 호출 지점

| 파일:줄 | 호출 | 컨텍스트 함수 | 비고 |
|---|---|---|---|
| [SkeletalMeshComponent.cpp:139](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp) | `AnimInstance->Update(DeltaTime)` | `USkeletalMeshComponent::TickComponent` (L131-147) | 매 틱 시간 누적 + Notify 판정 진입 |
| [SkeletalMeshComponent.cpp:140](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp) | `AnimInstance->EvaluateGraph()` | 동일 | 포즈 평가(Notify와 별개 경로) |
| — | `AnimInstance->GetTriggeredNotifiesThisFrame()` | **호출 없음** | 코드 전체 grep 결과 Document/*, 정의(`AnimInstance.h:44`), 자기 자신 push(`AnimInstance.cpp:81`) 외 사용처 0건 |

호출 체인(평가 경로 일치 여부):
```
UWorld::Tick
 → FActorComponentTickFunction::ExecuteTick
   → USkeletalMeshComponent::TickComponent (SkeletalMeshComponent.cpp:131)
     → UAnimInstance::Update (AnimInstance.cpp:23)   ← Notify 판정 여기서 발생
     → UAnimInstance::EvaluateGraph (AnimInstance.cpp:87)
```
판정 진입점(`Update`)은 평가 경로와 동일 틱 안에서 일관되게 호출된다. 다만 판정 *결과*를 소비하는 후속 호출이 없으므로 흐름은 component 단계에서 끊긴다.

## 진단

1. **디스패치 단계가 정의된 적이 없다 (가장 큰 끊김).** `Update`는 `TriggeredNotifiesThisFrame`에 이름만 쌓고 종료된다. 외부 폴링 호출도 0건이므로 Notify는 사실상 "발생을 기록만 하고 누구에게도 전달되지 않는" 상태다. 근거:
   - 주석 명시: [AnimInstance.cpp:73](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) `"Notify 판정 (dispatch는 파트 3) — IsTriggeredBetween이 prev > curr 루프 wrap 케이스를 처리한다."`
   - 호출 부재: `GetTriggeredNotifiesThisFrame`은 정의 외 호출 0건 (Document/*는 조사 노트로 카운트하지 않음).

2. **`Duration` 필드가 미사용 — 구간형 Notify의 begin/tick/end 의미 없음.** [AnimNotify.h:21](../KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h)에 `Duration: float`이 정의되어 있으나, Core에서 `Duration`을 읽는 코드는 grep 상 [AnimNotify.h:27](../KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h)(직렬화) 외에는 없다. 결과적으로 단발성과 구간형이 런타임에서 동일하게 처리되며 둘 다 TriggerTime 단발 이벤트로만 동작한다. 구간 진입/유지/종료를 추적하는 상태도 없다.

3. **StateMachine 경로에서 활성 Notify 공급이 누락될 수 있다.** [AnimInstance.h:81](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h)의 `GetActiveNotifies` 기본 구현은 `nullptr`이며, 현재 override는 [AnimSingleNodeInstance.cpp:75-78](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp) 하나뿐이다 (확인: 해당 시그니처 grep 결과 단일 정의). StateMachine을 사용하는 AnimInstance 파생이 별도로 override하지 않으면 state machine 안 시퀀스의 Notify는 절대 발화하지 않는다. 응집도 문제이기보다는, Notify 공급 책임이 AnimInstance 파생에 분산되어 있고 파생마다 누락 가능하다는 **흐름 단절 위험**이다.

4. **`EAnimTransitionConditionKind::OnNotify`가 미구현.** [AnimGraph_StateMachine.cpp:37-39](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp)에서 `OnNotify`와 `Custom`은 즉시 `return false`이고 주석은 `"파트 B 대기"`다. Notify를 transition 트리거로 사용하려는 의도가 enum 레벨에는 표시되어 있으나, 실제 평가가 항상 실패하므로 의미적으로 죽은 경로다.

5. **payload 표현력 한계.** `NotifyName: FName` 하나만 저장한다. 매개변수·페이로드 전달 메커니즘이 없어 향후 dispatch가 구현되더라도 "이름 기반 이벤트" 이상의 정보 전달이 어렵다.

### 응집도 관점
처리 로직 자체는 분산되어 있지 않다 — 판정·시간진행·결과수집이 모두 `UAnimInstance::Update` 한 함수에 모여 있고, 시간구간 판정 헬퍼만 `FAnimNotifyEvent` 메서드로 분리되어 있다. 문제는 분산이 아니라 **체인의 후반부(디스패치·소비)가 존재하지 않는 것**이다. 정의 → Core 판정까지 응집되어 있고, 그 이후가 빈 채로 끝난다.

### 후속 작업이 필요한 지점
- (a) 디스패치 메커니즘 도입(델리게이트/콜백 또는 component 측 폴링 호출) — `AnimInstance.cpp:73` 주석에 예고된 "파트 3".
- (b) `Duration` 의미 구현 — 구간형 Notify의 begin/tick/end 상태 전이 또는 명시적인 instant-only 정책.
- (c) StateMachine 파생 AnimInstance에서 `GetActiveNotifies` override 정책 정의 (현재 활성 상태/transition 중인 양쪽 상태의 sub-sequence Notify를 어떻게 모을지).
- (d) `EAnimTransitionConditionKind::OnNotify` 평가 로직 구현 — `TriggeredNotifiesThisFrame`와 transition 조건 매칭.

### 범위 외 관찰 사항
- `FUAnimSequenceDataSource::CachedNotifyEntries` — 에디터 UI 캐시(파일: [AnimSequenceDataSource.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequenceDataSource.h), 위치는 확인 필요). 런타임 Notify 처리 흐름과 무관하므로 본 조사 범위 밖.
- `AnimSingleNodeInstance.cpp:52-67`의 `[TEMP DIAG — root_rotation_coordsys_verification]` 로깅 블록은 Notify와 무관한 일시 진단 코드 (Notify 조사 범위 밖, 별도 정리 대상).
