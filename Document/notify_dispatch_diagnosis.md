# Notify 런타임 Dispatch 진단

> 본 문서는 진단 전용. 코드 변경 없음. 모든 `file:line`은 `feature/project` 브랜치
> 작업 트리를 직접 열어 확인했다. 미확인 항목은 "확인 필요"로 명시한다.
> 결정은 사람이 한다 — 본 문서는 후보와 트레이드오프만 나열한다.

---

## TL;DR — dispatch 경로가 존재하는가

**부분적으로 존재한다. "검출"은 있지만 "전달"이 끊겨 있다.**

- 검출 측: `UAnimInstance::Update`(`KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:74-84`)가 매 틱 `GetActiveNotifies()`를 순회하며 `FAnimNotifyEvent::IsTriggeredBetween`을 호출하고, 통과한 `NotifyName`을 `TriggeredNotifiesThisFrame`(`AnimInstance.h:93`)에 push 한다.
- 전달 측: 외부에 그 결과를 노출하는 getter `GetTriggeredNotifiesThisFrame()`(`AnimInstance.h:46`)는 **현재 코드 전체에서 호출처가 0건**이다. 즉 트리거된 Notify 이름은 채워졌다가 다음 프레임 시작 시점에 그대로 지워진다(`AnimInstance.cpp:26`의 `clear()`).
- `FSoundManager`로 향하는 호출도 이 경로 어디에도 없다 — Notify→소리 연결 코드가 한 줄도 없다는 뜻이다.

따라서 "소리 한 번 내기"를 구현하려면 **`Update` 내부 push 위치에서 직접 소리를 재생하거나, 외부 폴링 소비자를 새로 만들거나, delegate를 도입**해야 한다 — 어느 쪽이든 "끼워 넣을 자리"이지 "이미 있는 자리"는 아니다.

---

## 1. Tick 시간 진행 경로  [Q1]

### 확인된 사실

| 항목 | 위치 | 코드 |
|---|---|---|
| 컴포넌트 tick 진입 | [`SkeletalMeshComponent.cpp:131-147`](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp) | `USkeletalMeshComponent::TickComponent` |
| AnimInstance 업데이트 호출 | [`SkeletalMeshComponent.cpp:139`](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp) | `AnimInstance->Update(DeltaTime);` |
| `PreviousTime` 백업 | [`AnimInstance.cpp:43`](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) | `PreviousTime = CurrentTime;` |
| `CurrentTime` 누적 (looping) | [`AnimInstance.cpp:44-54`](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) | `fmod(NewTime, Length)`로 wrap |
| `CurrentTime` 누적 (non-looping) | [`AnimInstance.cpp:56-71`](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) | 끝/시작에 도달하면 `bPaused = true` |
| Paused 시 동기화 | [`AnimInstance.cpp:35-41`](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) | `PreviousTime = CurrentTime`만 정렬, 누적 없음 |

`Update` 한 함수 안에서 (a) `PreviousTime` 저장 → (b) `CurrentTime` 갱신 → (c) Notify 판정 루프가 순서대로 이뤄진다. 즉 "끼워 넣을 후보 지점"은 별도 함수가 아니라 **이 `Update`의 `for` 루프 본문 안 또는 그 직후**이다.

### 미확인 / 정책 결정 항목

- 다른 컴포넌트(`USkeletalMeshComponent` 외)에서 `AnimInstance->Update`를 호출하는 경로가 있는지는 본 스캔에서 추가 확인하지 않았다. **확인 필요** — 단, 본 작업의 수직 슬라이스(SingleNode + Notify→Sound)는 `USkeletalMeshComponent` 경로 하나만 검증해도 충분.

---

## 2. Dispatch 부재 검증  [Q2]

### grep 결과 (실행한 대로 기록)

| 검색 패턴 | 결과 | 의미 |
|---|---|---|
| `IsTriggeredBetween` (코드만) | **2건** — 정의 [`AnimNotify.h:35`](../KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h), 호출 [`AnimInstance.cpp:79`](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) | 호출처는 단 한 곳 — `UAnimInstance::Update` 내부 |
| `TriggeredNotifiesThisFrame` (코드만) | **5건** — 선언 `AnimInstance.h:93`, getter `AnimInstance.h:46`, `clear()` `AnimInstance.cpp:18`, `clear()` `AnimInstance.cpp:26`, `push_back` `AnimInstance.cpp:81` | 자가 채움/자가 초기화 외 사용 0건 |
| `GetTriggeredNotifiesThisFrame` (코드만, Document/ 제외) | **1건** — 정의 `AnimInstance.h:46` | **소비자 0건.** 외부에서 폴링하는 코드 자체가 없다. |
| `Notifies` 배열 시간 순회 dispatch | 1건 ([`AnimInstance.cpp:74-84`](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp)) | 위 루프가 유일. 그 외 dispatch 로직 없음. |

### 해석

검출 단계는 완성되어 있고 그 결과를 "이번 프레임에 발화된 이름들"로 만들어 두지만, **그 결과를 들고 가는 코드가 어디에도 없다**. `AnimInstance.cpp:73` 주석도 `"// Notify 판정 (dispatch는 파트 3) — ..."`라고 표시해 둔 미완 지점이다.

`FSoundManager::Get().PlayEffect(...)` 호출은 코드베이스 전체에서 7건이 있지만 (`HopMovementComponent.cpp:76`, `:156`, `ParryComponent.cpp:28`, `LuaSoundLibrary.cpp:17,21`, Crossy 게임 모듈 등), **AnimInstance / Notify / SkeletalMeshComponent 경로에는 단 한 건도 없다**.

---

## 3. `IsTriggeredBetween` 루프 경계  [Q3]

### 확인된 사실

본문 ([`AnimNotify.h:35-46`](../KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h)):

```cpp
bool IsTriggeredBetween(float PreviousTime, float CurrentTime, float SequenceLength) const
{
    if (SequenceLength <= 0.0f) return false;
    if (PreviousTime <= CurrentTime)
        return PreviousTime < TriggerTime && TriggerTime <= CurrentTime;
    return TriggerTime > PreviousTime || TriggerTime <= CurrentTime;
}
```

**함수 본문이 wrap을 직접 처리한다.** `Prev > Curr` 분기(line 45)에서 `(Prev, End] ∪ [0, Curr]` 합집합 판정을 한 줄로 수행한다. 호출자가 구간을 둘로 쪼개 두 번 부를 필요 없다.

실제 호출자([`AnimInstance.cpp:74-84`](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp))도 이 가정 위에서 작성되어 있다 — wrap 분기 없이 단 한 번만 호출한다.

### 경계 케이스 — 확인 필요

- **`TriggerTime == 0.0f` 또는 `TriggerTime == SequenceLength`**: 비-wrap 분기는 `(Prev, Curr]` 반개구간이므로 첫 프레임(`Prev = Curr = 0`)에는 `0`인 Notify가 트리거되지 않는다. wrap 분기에서는 `TriggerTime <= Curr` (`Curr`가 작은 값)이므로 wrap 직후 첫 평가에서 잡힌다. 의도된 동작인지는 코드만으로 판단 불가 — **확인 필요**.
- **재생 속도가 매우 빨라 한 프레임에 두 번 이상 wrap되는 경우**(`DeltaTime * Speed > Length`): `Update`의 `fmod`(line 48)는 `[0, Length)` 안으로 욱여넣지만, 그 사이에 있던 Notify들이 일괄 누락된다. 본 함수 책임 밖이지만 dispatch 정확도에 영향. **수직 슬라이스에서는 무시 가능, 정책 결정 항목으로 기록만.**

---

## 4. SoundManager 인터페이스  [Q4]

### 확인된 사실

| 항목 | 근거 | 값 |
|---|---|---|
| 싱글톤 베이스 | [`SoundManager.h:9`](../KraftonEngine/Source/Engine/Sound/SoundManager.h) | `class FSoundManager : public TSingleton<FSoundManager>` |
| 접근 방식 | 호출자 예: [`HopMovementComponent.cpp:76`](../KraftonEngine/Source/Games/Crossy/Components/HopMovementComponent.cpp), [`EditorEngine.cpp:435`](../KraftonEngine/Source/Editor/EditorEngine.cpp) | `FSoundManager::Get()` (TSingleton 패턴 — `Get()` 정적 멤버) |
| 키 타입 | [`SoundManager.h:7`](../KraftonEngine/Source/Engine/Sound/SoundManager.h) | `using FSoundId = FString;` |
| 효과음 로드 시그니처 | [`SoundManager.h:25`](../KraftonEngine/Source/Engine/Sound/SoundManager.h) | `void LoadEffect(const FSoundId& ID, const std::wstring& FilePath)` |
| 효과음 재생 시그니처 | [`SoundManager.h:26`](../KraftonEngine/Source/Engine/Sound/SoundManager.h) | `void PlayEffect(const FSoundId& ID)` |
| 사전 로드 필수성 | [`SoundManager.cpp:97-107`](../KraftonEngine/Source/Engine/Sound/SoundManager.cpp) | `PlayEffect`는 `Sounds` 맵에 없으면 `[Sound] Effect not loaded:` 로그만 찍고 return — **로드 안 한 키는 무성** |
| 음악 계열 | [`SoundManager.h:16-19`](../KraftonEngine/Source/Engine/Sound/SoundManager.h) | `LoadMusic` / `PlayMusic` / `StopMusic` 별도. `bLoop` 인자 있음. |

### 사용 예 (Crossy)

`CrossyGameModule.cpp:113-118`에서 게임 시작 시 모든 효과음을 `LoadEffect`로 등록하고, 이후 `PlayEffect(CrossyAudioIds::Jump)` 같은 식으로 재생. ID는 `inline const FSoundId Jump = "Jump";` 형태의 문자열 상수 (`Source/Games/Crossy/Audio/CrossyAudioIds.h:5-13`).

### 본 작업에 함의

- 사용 측은 `PlayEffect(FSoundId)` 한 줄만 호출하면 된다. **싱글톤 접근/시그니처 측면에서 호출은 매우 가볍다.**
- **단, "그 사운드가 사전에 로드되어 있어야 한다"가 강한 전제**다. 어디서 어떤 시점에 `LoadEffect`를 부를지가 별도 정책 문제 (8절 참조).

---

## 5. AnimInstance 종류별 tick 경로  [Q5]

### 확인된 사실

- `Update`는 **base `UAnimInstance::Update`만 정의**되어 있다 ([`AnimInstance.cpp:23-85`](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp)). 파생 클래스(`UAnimSingleNodeInstance`, `UAnimStateMachineInstance`) 어디에도 `Update` override가 없다 — 시간 누적 로직과 Notify 검출 루프는 **하나의 함수**에서 통합 수행.
- 파생 차이는 두 가상 훅에 국한:
  - `GetEffectivePlayLength()` — SingleNode가 `CurrentSequence->GetPlayLength()` 반환 (`AnimSingleNodeInstance.cpp:70-73`). StateMachine은 미override (확인됨 — `AnimStateMachineInstance.h` 전문 읽음, override 선언 없음) → base default `0.0f` 반환.
  - `GetActiveNotifies()` — SingleNode가 `&CurrentSequence->GetNotifies()` 반환 (`AnimSingleNodeInstance.cpp:75-78`). **StateMachine은 미override → base default `nullptr`** → Notify 루프 자체가 진입하지 않음.

### dispatch 코드를 어디에 둘지 — 후보별 트레이드오프

세 후보를 본 작업과 무관하게 평가만 한다.

| 후보 | 위치 | 장점 | 단점 / 트레이드오프 |
|---|---|---|---|
| **A. `UAnimInstance::Update` 본문**(이미 검출 루프가 있는 그 자리) | [`AnimInstance.cpp:79-82`](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) `push_back` 직전/직후에 인라인 호출 | 가장 적은 코드 변경. `GetActiveNotifies` 훅 한 번만 손보면 SingleNode·StateMachine 모두 자연 수혜. Sound 의존을 AnimInstance 계층이 직접 짊어짐. | base 계층이 `FSoundManager`에 hard depend하면 엔진 모듈 의존 그래프가 거꾸로 흐른다(Animation → Sound). 테스트성/모듈성↓. |
| **B. `USkeletalMeshComponent::TickComponent`에서 폴링** | [`SkeletalMeshComponent.cpp:139` 직후](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp) `AnimInstance->GetTriggeredNotifiesThisFrame()`를 받아 처리 | 의존 방향 자연(Component가 양쪽을 알아도 정상). 이미 정의된 getter가 비로소 쓰임. SingleNode·StateMachine 공통 경로. | "이름 → 소리" 매핑을 컴포넌트가 들고 있어야 함. 매핑 표현 결정 필요(Q6). |
| **C. delegate 도입 (`OnNotify(FAnimNotifyEvent)`)** | base에 멤버 추가, push_back 자리에서 broadcast | 가장 유연 — 임의 콜백 등록. StateMachine OnNotify transition 조건과 같은 향후 기능에 재사용. | 본 작업 한 줄 슬라이스 대비 과설계. delegate 인프라 정합성/소유권 결정 비용↑. **수직 슬라이스에서는 비추천.** |

본 진단은 어느 하나를 택하지 않는다. **"가장 가벼움"은 A지만 의존 방향이 거꾸로 흐른다**, **"의존 방향이 자연"은 B지만 컴포넌트에 매핑 책임이 생긴다** — 어느 쪽을 비용으로 받아들일지가 사람이 결정할 부분.

수직 슬라이스 한정 의견: **B**가 기존 getter(`GetTriggeredNotifiesThisFrame`)의 첫 소비자를 만들고 의존 그래프를 깨지 않는다. 다만 이는 의견이지 단정이 아니다.

---

## 6. Notify → 소리 매핑 표현  [Q6]

### 확인된 사실

`FAnimNotifyEvent` 멤버 ([`AnimNotify.h:18-30`](../KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h)):

```cpp
struct FAnimNotifyEvent
{
    float TriggerTime = 0.0f;
    float Duration    = 0.0f;
    FName NotifyName;
    // operator<< 직렬화만 추가, 다른 페이로드 멤버 없음
};
```

**임의 페이로드(소리 키, payload string, payload struct 등) 멤버는 없다.** 직렬화 라운드트립도 위 3개 필드만 다룬다. 따라서 "어떤 Notify가 어떤 소리인지"는 `NotifyName`이라는 단일 키 외에 표현 수단이 없다.

### 후보별 트레이드오프

| 후보 | 어떻게 | 장점 | 단점 |
|---|---|---|---|
| **a. `NotifyName.ToString()`을 그대로 `FSoundId`로 사용** | `FName::ToString()`([`FName.h:24`](../KraftonEngine/Source/Engine/Object/FName.h)) → `FString` → `FSoundId`(`= FString`, [`SoundManager.h:7`](../KraftonEngine/Source/Engine/Sound/SoundManager.h)). 한 줄. `Notify.NotifyName.ToString()`을 그대로 `PlayEffect`에 넘김 | 코드 0줄. 타입 정합. Crossy의 기존 등록 패턴(`"Jump"`, `"Dash"` 등 짧은 문자열 키)과 정확히 일치 — Notify 이름을 `"Jump"`로 짓고 사운드도 `"Jump"`로 로드하면 끝. | "Notify 이름 ≡ 사운드 키"를 강제하므로 같은 Notify로 두 사운드를 재생하거나 사운드 없이 다른 처리를 하려면 매핑이 깨진다. 본 슬라이스에서는 문제 안 됨, 확장 시 제약. |
| **b. dispatch 지점의 하드코딩 `if/switch`** | `if (Name == "Foot_L") PlayEffect("Footstep_L"); else if ...` | 가장 명시적. 디버깅 용이. | 매 Notify 추가마다 코드 수정. 본 작업에는 과함. |
| **c. 별도 매핑 테이블** | `TMap<FName, FSoundId>` 멤버를 `USkeletalMeshComponent` 또는 별도 컴포넌트에 둠. 게임 시작 시 채워 둠. | 데이터 주도. Notify 이름과 사운드 키 분리. | 테이블을 어디에 둘지, 누가 채울지(에디터 UI? 코드?) 결정 필요. 본 슬라이스 대비 인프라 비용. |
| **d. `FAnimNotifyEvent`에 새 멤버 추가** (예: `FName SoundId`) | 구조체 확장 + 직렬화 versioning | 데이터에 매핑 내장. 에디터에서 사운드 키 편집 가능. | `operator<<` 호환성, 기존 `.asset` 마이그레이션 필요. **범위 외**. |

### 가장 가벼운 테스트용 방식 (수직 슬라이스 한정 의견)

**a**가 가장 가볍다 — 새 자료구조 0개, 새 코드 1줄, 기존 SoundManager 패턴과 100% 정합. **단** "Notify 이름과 사운드 키가 같아야 한다"는 비공식 계약이 생긴다. 본 슬라이스가 "소리 한 번 내기"만 검증한다면 그 비용은 무시 가능. 장기 정책으로 굳히려면 c 또는 d가 필요할 수 있으나 그건 본 진단 범위를 넘어선다.

---

## 7. 구현 작업 분할 (개략)

> 본 진단은 구현하지 않는다. 변경 예상 file만 지목한다.
> 수직 슬라이스 = "Notify 발화 시점에 소리 한 번 재생되는 것을 확인" 하나.

| # | 작업 | 변경 예상 file | 비고 |
|---|---|---|---|
| 1 | dispatch 위치 결정 (A/B/C 중 택1) | — | **사람 결정 필요.** 5절 후보 참조. 단정 금지. |
| 2 | 매핑 표현 결정 (a/b/c/d 중 택1) | — | **사람 결정 필요.** 6절 후보 참조. |
| 3 | dispatch 코드 한 곳 추가 | (택1) `AnimInstance.cpp` / `SkeletalMeshComponent.cpp` / 신규 delegate 인프라 | 결정 1·2 후 변경 위치 자동 도출 |
| 4 | 테스트용 사운드 사전 로드 1건 | 게임 모듈 / 또는 테스트 셋업 코드 (예: Crossy 모듈 패턴 참조) | `FSoundManager::Get().LoadEffect(...)` 1줄 |
| 5 | 테스트 Notify 1개 갖는 `UAnimSequence` 픽스처 준비 | 기존 `.asset` 임포트 자산 + 에디터 `AddNotify` 사용 (편집 경로는 이미 존재) | 신규 직렬화 작업 **없음** |
| 6 | 재생 확인 (수동) | — | 청취/로그 |

**작업 5에서 새 직렬화 코드를 짜지 않는다.** 사전 전제대로 `FAnimNotifyEvent` 직렬화·에디터 편집 경로는 이미 완비.

---

## 8. 확인 필요 / 정책 결정 항목

1. **dispatch 계층 (Q5/5절 표)**: A=AnimInstance, B=SkeletalMeshComponent, C=delegate. **사람 결정.**
2. **매핑 표현 (Q6/6절 표)**: a=NotifyName 직접, b=hardcoded if, c=table, d=struct 확장. **사람 결정.**
3. **사운드 사전 로드 시점**: SoundManager는 `PlayEffect` 시 로드하지 않는다(`SoundManager.cpp:100-104`). 테스트용 사운드를 어디서 로드할지 — 게임 모듈 boot? 컴포넌트 BeginPlay? 별도 매니페스트? **사람 결정.**
4. **`TriggerTime == 0`/`== Length` 경계 의도** (3절): 첫 프레임 무발화가 의도된 동작인지 미확정. **확인 필요.**
5. **빠른 재생/저 fps에서의 다중 wrap**: 한 프레임 wrap 1회 초과 시 누락 (3절). 본 슬라이스 무시 가능하나 장기적으로 기록.
6. **`AnimInstance->Update` 호출처 추가 검증** (1절): `USkeletalMeshComponent` 외 호출 경로가 있는지 본 스캔에서 추가 확인 안 함. **확인 필요 — 단 본 슬라이스 범위 외에서 처리해도 무방.**

---

## 9. 범위 외 관찰 사항 (기록만, 진단하지 않음)

- `AnimStateMachineInstance`는 `GetActiveNotifies`를 override하지 않아 base의 `nullptr`을 반환 → StateMachine 모드에서는 현재 Notify 검출이 아예 동작하지 않음. (`AnimStateMachineInstance.h` 전문에 선언 없음.) — **본 작업 범위 외**, StateMachine 패스에서 별도 정의 필요.
- `AnimInstance::InitializeAnimation`이 `TriggeredNotifiesThisFrame.clear()`를 호출하는데 같은 frame 내에서 `Update`가 다시 한 번 `clear` (`AnimInstance.cpp:18`, `:26`) — 중복이지만 부작용 없음.
- `AnimSingleNodeInstance::EvaluateGraph`에 `[TEMP DIAG — root_rotation_coordsys_verification]` 디버그 로그가 남아 있음 (`AnimSingleNodeInstance.cpp:52-67`) — 범위 외, 별 작업에서 제거 대상.
