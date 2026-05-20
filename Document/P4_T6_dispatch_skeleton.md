# Implementation Prompt P4 — T6: Runtime Dispatch 골격 (Stub Log)

## 목적

`UAnimInstance::Update` 의 notify 시간 판정 루프 직후, 트리거된 notify들을 모아 별도 함수 `DispatchTriggeredNotifies()` 로 넘기는 골격을 만든다.
**본 PR에서는 dispatch 함수의 분기 본문을 stub log 로만 채우고**, 실제 SoundManager / CameraManager 호출은 다음 PR(P5)에서 채운다.

## 전제

- **P1, P2, P3 머지 완료**. data model, asset 직렬화, DataSource adapter, editor UI 모두 동작.
- 에디터에서 Sound notify, CameraShake notify를 추가/저장/로드할 수 있는 상태이지만, 재생 시 아무 일도 일어나지 않는 상태.

## 참조 문서

- 통합 설계: `Document/animnotify_integration_design.md`
  - 인용 섹션: **5.4 (Layer 4 — Before/After + 의존성 영향)**, **T6 (Section 7)**, **D4, D8**, **R4, R5**

## 엄격한 제약

1. **본 PR 범위는 T6뿐**. SoundManager / CameraManager / outer chain 헤더는 include하지 않는다. dispatch 분기는 stub log + skip 만.
2. **D4 — 별도 함수 분리**: dispatch 로직을 `Update` 함수 body에 inline으로 박지 않는다. `DispatchTriggeredNotifies` 라는 별도 멤버 함수를 신설.
3. **R4 — 헤더 의존성**: `AnimInstance.h` 에는 forward declaration만 추가하고 외부 헤더 include는 금지. 본 PR 시점에는 외부 헤더 자체가 아직 필요 없으므로 (stub 단계) forward decl도 추가할 게 없다.
4. **D8 — NotifyName 보존**: 기존 `TriggeredNotifiesThisFrame : TArray<FName>` 의 누적 동작은 그대로 둔다 (Lua binding 호환). dispatch는 그와 별개의 로컬 배열을 사용.
5. **R5 — preview/game 가드 없음**: 사용자 결정에 따라 preview/game 구분 없이 dispatch 동작. world 체크 가드를 추가하지 않는다.
6. **D7 — reverse/seek 정책 변경 없음**: 본 PR에서 forward 정상 재생만 신경 쓴다. 기존 `IsTriggeredBetween` 동작을 그대로 사용.

## 작업 항목

### Step 0 — Verify Read

다음 view 후 작업. 설계 인용과 실제 code가 다르면 STOP 보고.

- `Engine/Asset/Animation/Core/AnimInstance.h` — class 선언 전체, 특히 `Update` 시그니처와 `TriggeredNotifiesThisFrame` 멤버
- `Engine/Asset/Animation/Core/AnimInstance.cpp` — `Update` 함수 본문, 특히 notify 판정 루프 (설계 인용: `.cpp:74-84`)
- `Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp` — `GetActiveNotifies` 의 반환 형태 (설계 인용: `.cpp:75-78`) — 본 PR에서 변경 안 하지만 데이터 흐름 파악용
- `Engine/Asset/Animation/Notify/AnimNotify.h` — P1에서 확장된 `FAnimNotifyEvent` 와 `EAnimNotifyType` enum

### Step 1 — Header에 함수 선언 추가

`AnimInstance.h` 에 protected 멤버 함수 한 개 추가:

```cpp
protected:
    void DispatchTriggeredNotifies(const TArray<const FAnimNotifyEvent*>& InTriggered);
```

- virtual 불필요 (D4 톤 — 단일 진실 경로).
- `FAnimNotifyEvent` 타입은 이미 헤더에서 보이므로 (struct 정의가 `AnimNotify.h` 에 있고 P1에서 멤버로 직접 보유하지는 않더라도 `GetActiveNotifies` 반환형에서 이미 의존성이 있음) 추가 include는 불필요.
  - 정확한 의존 상태는 view로 재확인. 만약 forward decl만 있던 상태라면 P1의 변경으로 이미 완전 타입 의존이 자연스러우니 그 흐름을 따른다.
- `TArray` 의 정확한 컨테이너 타입과 const-ness는 본 프로젝트의 컨벤션 확인 후 따른다.

### Step 2 — `Update` 함수 본문 변경

기존 `.cpp:74-84` 의 notify 판정 루프를 다음 형태로 확장:

기존 (대략):
```cpp
const auto& Notifies = GetActiveNotifies();
for (const FAnimNotifyEvent& Notify : Notifies) {
    if (Notify.IsTriggeredBetween(PreviousTime, CurrentTime, Length)) {
        TriggeredNotifiesThisFrame.Add(Notify.NotifyName);
    }
}
```

변경 후:
```cpp
const auto& Notifies = GetActiveNotifies();
TArray<const FAnimNotifyEvent*> LocalTriggered;  // 로컬, 함수 종료 시 자동 해제

for (const FAnimNotifyEvent& Notify : Notifies) {
    if (Notify.IsTriggeredBetween(PreviousTime, CurrentTime, Length)) {
        TriggeredNotifiesThisFrame.Add(Notify.NotifyName);  // 기존 동작 보존 (Lua)
        LocalTriggered.Add(&Notify);                         // 신규: dispatch용
    }
}

if (LocalTriggered.Num() > 0) {
    DispatchTriggeredNotifies(LocalTriggered);
}
```

- 정확한 함수명 / 변수명 / API는 view 결과를 기반으로 본 프로젝트 컨벤션에 맞춘다.
- 포인터 안정성: `Notifies` 가 `Update` 진행 도중 재할당되지 않는다는 가정 — `GetActiveNotifies()` 는 sequence의 멤버 배열에 대한 참조이고, dispatch 호출 직전에 그 sequence 자체가 교체되지 않는다는 점은 본 함수 안에서는 자명. 안전성 코멘트 한 줄 추가 권장.
- `IsTriggeredBetween` 의 정확한 시그니처 — 설계 문서에서 `(prev, curr, length)` 인용 — view로 재확인.

### Step 3 — `DispatchTriggeredNotifies` 함수 정의 (stub)

`AnimInstance.cpp` 에 함수 정의 추가:

```cpp
void UAnimInstance::DispatchTriggeredNotifies(const TArray<const FAnimNotifyEvent*>& InTriggered)
{
    for (const FAnimNotifyEvent* NotifyPtr : InTriggered)
    {
        if (!NotifyPtr) continue;   // 방어적, 정상 흐름에서는 null 불가
        const FAnimNotifyEvent& Notify = *NotifyPtr;

        switch (Notify.Type)
        {
        case EAnimNotifyType::None:
            // D3: v3 백필 항목 — skip
            continue;

        case EAnimNotifyType::Sound:
            // TODO(P5): FSoundManager::Get().PlayEffect(Notify.SoundId);
            UE_LOG(LogTemp, Log, TEXT("[AnimNotify stub] Sound dispatch — SoundId=%s"), *Notify.SoundId);
            break;

        case EAnimNotifyType::CameraShake:
            // TODO(P5): ResolveCameraManager() → StartCameraShake(Notify.ShakeParams);
            UE_LOG(LogTemp, Log, TEXT("[AnimNotify stub] CameraShake dispatch — NotifyName=%s"), *Notify.NotifyName.ToString());
            break;

        default:
            // 알려지지 않은 타입 (forward-compat). 무시.
            break;
        }
    }
}
```

- 정확한 로그 매크로는 본 프로젝트가 사용하는 매크로(`UE_LOG`, 또는 자체 `UE_LOG`-호환 매크로)에 맞춘다. 기존 `AnimInstance.cpp` 의 다른 로그 호출을 참고.
- 로그 카테고리 (`LogTemp`)는 본 프로젝트에 정의된 카테고리로 대체. 없으면 기존 카테고리 중 가장 적합한 것 사용.
- `TODO(P5)` 코멘트는 다음 PR에서 본 분기에 실제 호출을 채울 자리임을 명시.

### Step 4 — `paused` / `Length<=0` 분기 확인

기존 `Update` 의 early-return 분기들 — `bPaused`, `Length <= 0`, 그 외 — 이 dispatch 이전에 return하는 구조라면 그대로 둔다 (설계 5.4 항목 6에서 "dispatch 미실행이 자연스럽다"고 명시).

만약 dispatch 호출이 추가된 위치가 early-return을 우회하는 형태가 되면 멈추고 보고. 의도된 동작이 아니다.

### Step 5 — 수동 확인 체크리스트

- [ ] 빌드 통과
- [ ] Sound notify가 포함된 sequence를 게임 모드에서 재생 시 log에 `[AnimNotify stub] Sound dispatch ...` 가 정확히 1번 출력 (instant notify 기준)
- [ ] CameraShake notify에 대해서도 동일하게 log 출력
- [ ] v3 백필된 Type=None notify는 log에 아무것도 출력 안 함
- [ ] **에디터 preview 재생 시에도 동일하게 log 출력** (R5: 가드 없음 정책 확인)
- [ ] paused 상태에서는 log 출력 안 함
- [ ] Loop 재생 시 한 사이클당 1번씩 log 출력 (wrap 직후 동일 notify가 정확히 1번 발화하는지 확인)

## 변경 파일 목록 (예상)

| 파일 | 변경 |
|---|---|
| `Engine/Asset/Animation/Core/AnimInstance.h` | `DispatchTriggeredNotifies` 선언 추가 |
| `Engine/Asset/Animation/Core/AnimInstance.cpp` | `Update` 에 dispatch 호출 + 함수 정의 (stub log) |

## 절대 금지

- `Sound/SoundManager.h` 또는 `Camera/PlayerCameraManager.h` include (다음 PR P5의 범위)
- outer chain helper (`ResolveCameraManager`) 함수 본 PR에 추가 (P5에서)
- `TriggeredNotifiesThisFrame` 의 타입이나 누적 시점 변경 (D8, Lua 호환)
- preview world / game world 구분하는 가드 추가 (R5)
- dispatch를 `Update` 내부에 inline으로 박는 것 (D4 위반)
- forward-compat을 위해 enum 값을 추가하거나 default 분기에서 별도 처리하는 것
- `IsTriggeredBetween` 동작 자체 변경 (D7)

## 보고 시 포함할 것

1. Step 0의 verify 결과
2. Step 2의 정확한 함수명/변수명 (본 프로젝트 컨벤션 반영)
3. Step 3의 로그 매크로 / 카테고리 결정
4. Step 4의 early-return 분기 확인 결과
5. Step 5의 수동 확인 체크리스트 결과 (각 항목 실제 결과)
6. **다음 PR(P5 — 외부 시스템 호출)에 영향을 줄 만한 발견사항**, 특히 outer chain 의존성 관련

(끝)
