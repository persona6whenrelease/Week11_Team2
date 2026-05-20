# AnimNotify 구현 I1 — GetActiveNotifies override + StateMachine notify firing

D4([Document/animnotify_diagnosis_D4.md](animnotify_diagnosis_D4.md)) §9 종합 판정 기반 구현. PIE silent 해소 + StateMachine 모드에서 Sound + CameraShake notify 정상 발화.

선행 문서: D1~D4, P-Fix5. 전제: ① From→To 전환 시점 기준 1개 sequence 활성 / ② D3 §5.1 옵션 1 / ③ D3/D4 결론 수용.

작업 일자 컨텍스트: 2026-05-20 working tree.

---

## 1. STEP 1 — 코드 재확인 결과

| D4 인용 위치 | 재확인 결과 | 변경 사항 |
|---|---|---|
| [AnimInstance.h:39](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:39) `virtual void Update` | 동일 | 변경 없음 |
| [AnimInstance.h:63](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:63) `SetEvaluationTime` | 동일 (단 non-virtual) | I1 STEP 9에서 `virtual` 로 변경 |
| [AnimInstance.h:79](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:79) `GetEffectivePlayLength` | 동일 (virtual, base=0) | 변경 없음 |
| [AnimInstance.h:84](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:84) `GetActiveNotifies` | 동일 (virtual, base=nullptr) | 변경 없음 |
| [AnimInstance.h:106](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:106) `AnimGraphPtr` | 동일 (protected) | 변경 없음 |
| [AnimInstance.h:108](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:108) `TriggeredNotifiesThisFrame` | 동일 | 변경 없음 |
| [AnimInstance.h:115](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:115) `bPaused` | 동일 | 변경 없음 |
| [AnimInstance.cpp:29~98](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:29) `Update` 본체 | 동일 | 변경 없음 (base는 무수정) |
| [AnimStateMachineInstance.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h) 전체 | 동일 | I1 STEP 3에서 멤버/override/헬퍼 추가 |
| [AnimStateMachineInstance.cpp](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp) | 동일 | I1 STEP 4~6, 9, 12에서 구현 추가 |
| [AnimGraphInstance.h/cpp](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraphInstance.h) | 동일 | I1 STEP 7에서 override 추가 |
| [AnimGraph_StateMachine.h:63~80](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:63) | 동일 | I1 STEP 2에서 getter/setter 추가 |
| [AnimGraph_StateMachine.cpp:45~155](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.cpp:45) `Evaluate` | 동일 | 변경 없음 |
| [AnimGraph.h:38~110](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:38) | 동일 | 변경 없음 |
| [AnimNotify.h:78~89](../KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:78) | 동일 | 변경 없음 |
| [SkeletalMeshComponent.cpp:390~410](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:390) | 동일 | 변경 없음 |

**결론**: D4 인용 위치 변경 없음. 본 prompt의 모든 라인 참조 유효.

---

## 2. STEP 별 실제 변경 요약

### STEP 2 — `FAnimGraphNode_StateMachine` getter 신설

파일: [AnimGraph_StateMachine.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h)

`Evaluate` 선언 뒤, private 영역 직전에 public inline 4개 추가:
```cpp
int32 GetActiveStateIndex() const { return ActiveStateIndex; }
int32 GetActiveTransitionIndex() const { return ActiveTransitionIndex; }
float GetStateLocalTime(int32 StateIdx) const;       // bounds check, 0 fallback
void  SetStateLocalTime(int32 StateIdx, float Time); // bounds check
```

### STEP 3 — `UAnimStateMachineInstance` 멤버 + override 선언

파일: [AnimStateMachineInstance.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h)

추가:
- override 4개 (public): `Update`, `GetActiveNotifies`, `GetEffectivePlayLength`, `SetEvaluationTime`
- 헬퍼 3개:
  - private inst method: `ResolveStateMachineRoot() const`
  - public static: `ResolveActiveNotifiesFromNode(Node*)`, `ResolveActivePlayLengthFromNode(Node*)` (UAnimGraphInstance 재사용용)
- 멤버 2개 (private): `PrevActiveStateIndex = -1`, `PrevStateLocalTime = 0.0f`

### STEP 4~6, 9, 12 — `UAnimStateMachineInstance` cpp 구현

파일: [AnimStateMachineInstance.cpp](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp)

추가 함수 (D4 §G6.3 후보 b sketch 정밀화):
- `ResolveStateMachineRoot()` — `AnimGraphPtr->GetRoot()` → `dynamic_cast<FAnimGraphNode_StateMachine*>`
- `ResolveActiveNotifiesFromNode(Node*)` — 4가지 분기 (SequencePlayer / StateMachine 재귀 / Blend2 / BlendN). Blend는 Alpha<0.5 → ChildA, BlendN은 최대 Weight 선택.
- `ResolveActivePlayLengthFromNode(Node*)` — 동일 분기 + 부모 SubLengthHint 우선, 0이면 재귀.
- `GetEffectivePlayLength` override — `ResolveActivePlayLengthFromNode(States[ActiveIdx].Sub.get())` 호출.
- `GetActiveNotifies` override — `ResolveActiveNotifiesFromNode(States[ActiveIdx].Sub.get())` 호출.
- `Update` override — base 책임 (a)(b)(c)(g)(h)(i) 재현, PrevStateLocalTime 기반 notify firing, 1-frame 지연 예측 보정, state 전환 감지 시 PrevStateLocalTime 보정, base mirror 갱신.
- `SetEvaluationTime` override — 활성 state의 StateLocalTime + PrevStateLocalTime + base mirror set.
- `SetStateMachineGraph` 본체에 PrevActiveStateIndex/PrevStateLocalTime reset 추가.

### STEP 7 — `UAnimGraphInstance` override

파일: [AnimGraphInstance.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraphInstance.h), [AnimGraphInstance.cpp](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraphInstance.cpp)

- header에 `GetActiveNotifies` + `GetEffectivePlayLength` override 선언.
- cpp에서 `UAnimStateMachineInstance::ResolveActive*FromNode` static 헬퍼 재사용으로 구현.
- Update override는 **없음** — base의 PrevTime/CurrentTime 모델이 단순 root SequencePlayer에 적합 (production 사용은 console debug 한정).

### STEP 9 — `SetEvaluationTime` virtual 변경

파일: [AnimInstance.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h)

- 기존 inline non-virtual → `virtual` 키워드 추가. 동작 동일.
- 주석 갱신.

### STEP 8 / 10 / 11 — base 책임 / clear / 1-frame 지연

별도 STEP이 아닌 STEP 6 본체에 모두 포함:
- (a)(b): `LastDeltaTime = bPaused ? 0 : DeltaTime` + `TriggeredNotifiesThisFrame.clear()` (Update 진입부)
- (c): `if (!Notifies || Length <= 0)` 조기 종료 + state 추적 갱신
- (d)(e): `PreviousTime` / `CurrentTime` base mirror (state-local time으로 set)
- (f): SM evaluation 내 fmod 처리 + Update에서 예측 보정 시 fmod 동일 의미
- (g)(h)(i): `GetActiveNotifies()` → IsTriggeredBetween loop → DispatchTriggeredNotifies
- (j): base에 다른 책임 없음을 [AnimInstance.cpp:29-98](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:29) 재확인 — 일치
- 1-frame 지연 예측 보정: `PredictedCurrStateTime = SM->GetStateLocalTime(ActiveIdx) + DeltaTime` + looping wrap
- `bResetTimeOnEnter` 양쪽 케이스: state 전환 감지 시 `PrevStateLocalTime = max(0, PredictedCurrStateTime - DeltaTime)`

---

## 3. D4 종합 판정 대비 이행 매핑

| D4 §9 항목 | 처리 위치 (I1) | 이행 여부 |
|---|---|---|
| [확정] StateMachine query 메커니즘 | STEP 2 getter + STEP 5 GetActiveNotifies | ✓ |
| [확정] AnimGraph query 메커니즘 (단순 root + Blend) | STEP 7 + STEP 12 헬퍼 | ✓ |
| [확정] 시그니처 유지 (단일 포인터) | base 무수정, override 동일 시그니처 | ✓ |
| [확정] PrevTime mismatch 후보 b | STEP 6 Update override + PrevStateLocalTime | ✓ |
| [전제 충돌] GetEffectivePlayLength 동시 override | STEP 4 | ✓ |
| [위임 ①] getter 시그니처 확정 | public inline, primitive 반환, bounds 가드 | ✓ |
| [위임 ②] Blend 노드 정책 | Blend2: Alpha<0.5 → ChildA / BlendN: max Weight | ✓ |
| [위임 ③] base 책임 재현 | (a)~(j) 매핑 표 위에 | ✓ |
| [위임 ④] 1-frame 지연 처리 | 예측 보정 (PredictedCurrStateTime) | ✓ |
| [위임 ⑤] SetEvaluationTime StateMachine 해석 | 활성 state의 SetStateLocalTime + scrub-suppress 안전망 | ✓ |
| [위임 ⑥] UAnimGraphInstance::GetEffectivePlayLength | static 헬퍼 재사용 | ✓ |
| [위임] TriggeredNotifiesThisFrame clear 시점 | base와 동일 (Update 진입부) | ✓ (STEP 10 검증) |

---

## 4. STEP 13 — 빌드 + 정적 검증

### 4.1 빌드 결과

- MSBuild Debug|x64 → **성공**. 산출물 `C:\GitDirectory11\KraftonEngine\Bin\Debug\KraftonEngine.exe` 생성.
- 컴파일 에러: 0건.
- 경고: 2건 ([EditorPropertyWidget.cpp:1877, 1879](../KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp:1877)) — 기존 코드의 int32→float 변환, **본 작업과 무관**.

### 4.2 정적 검증 체크리스트

- [x] STEP 2: getter 3개 + setter 1개 public 선언 (`GetActiveStateIndex`, `GetActiveTransitionIndex`, `GetStateLocalTime`, `SetStateLocalTime`)
- [x] STEP 3: 멤버 2개 (`PrevActiveStateIndex`, `PrevStateLocalTime`) + override 4개 (`Update`, `GetActiveNotifies`, `GetEffectivePlayLength`, `SetEvaluationTime`) + private 헬퍼 1개 (`ResolveStateMachineRoot`) + static 헬퍼 2개 (`ResolveActiveNotifiesFromNode`, `ResolveActivePlayLengthFromNode`) 선언 일치
- [x] STEP 4: `GetEffectivePlayLength` override 정의됨 — 부모 SubLengthHint 우선, 0이면 재귀
- [x] STEP 5: `GetActiveNotifies` + `ResolveActiveNotifiesFromNode` 정의됨, 4가지 케이스 (SequencePlayer / StateMachine 재귀 / Blend2 / BlendN) 분기 모두 존재
- [x] STEP 6: `Update` override 정의됨, base 책임 (a)~(j) 모두 처리
- [x] STEP 7: `UAnimGraphInstance` override 2개 정의됨, Update override 없음
- [x] STEP 8.5: `bResetTimeOnEnter` 양쪽 케이스 처리 (`PrevStateLocalTime = max(0, PredictedCurrStateTime - DeltaTime)`)
- [x] STEP 9: `SetEvaluationTime` override 정의됨, base virtual 선언됨
- [x] STEP 11: 예측 보정 코드 적용됨 + looping wrap 처리됨 (`fmod` + 음수 보정)
- [x] STEP 12: Blend2/BlendN 케이스 분기 추가됨 (`ResolveActiveNotifiesFromNode` + `ResolveActivePlayLengthFromNode` 둘 다)

---

## 5. STEP 14 — PIE 동작 검증 가이드 (사용자 직접 실행 필요)

본 환경에서 직접 PIE 실행은 불가능 — editor에서 사용자가 manual 검증.

### 5.1 시나리오 — Sound

준비:
- [Asset/Prefab/MarioPawn.Prefab](../KraftonEngine/Asset/Prefab/MarioPawn.Prefab) 사용 (변경 없음).
- [LuaScripts/Game/MarioControllerAnimGraph.lua](../KraftonEngine/LuaScripts/Game/MarioControllerAnimGraph.lua) (StateMachine 모드, 변경 없음).
- 각 state의 sequence에 Sound notify가 binding되어 있어야 함. 없으면 AnimSequenceEditorTab에서 한 sequence(예: IDLE)에 `IM_COL32` SoundId picker로 등록된 effect 하나 binding 후 저장. SoundId는 `Asset/Sound/*.wav` 중 `LoadCrossyAudio`에 등록된 항목 (`Jump`/`Death`/`Parry`/`Dash`/`Crash`) 권장 — 그래야 PIE에서도 사운드 재생됨.

실행:
1. Editor → Play 버튼 → PIE 진입.
2. IDLE state 진입 후 notify TriggerTime 통과 시 사운드 발화 확인.
3. WASD 키로 RUN state 전환 → RUN의 notify 발화 확인.
4. SPACE 키로 JUMP state 전환 → JUMP의 notify 발화 확인.

| 시나리오 | 기대 | 실측 | 통과 |
|---|---|---|---|
| IDLE 진입 후 사운드 발화 | ○ | (사용자 검증) | (사용자 확인) |
| state 전환 직후 사운드 발화 | ○ | (사용자 검증) | (사용자 확인) |
| transition 경계 첫 notify | ○ | (사용자 검증) | (사용자 확인) |

### 5.2 시나리오 — CameraShake

준비: state 중 하나에 CameraShake notify 추가.

실행: PIE 진입 → 해당 state 진입 → 카메라 흔들림 발생 확인.

기대: C++ `DispatchTriggeredNotifies` 의 CameraShake 분기 ([AnimInstance.cpp:119-128](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:119)) → `ResolveCameraManager()` → `CamMgr->StartCameraShake(Notify.ShakeParams)`. PIE는 PlayerController 존재 → CamMgr non-null → 적용.

| 시나리오 | 기대 | 실측 | 통과 |
|---|---|---|---|
| CameraShake 발화 | ○ | (사용자 검증) | (사용자 확인) |

### 5.3 1-frame 지연 보정 효과 확인 (선택)

기존 SingleNode와 비교:
- AnimSequenceEditorTab에서 같은 sequence를 SingleNode preview로 재생 → notify 발화 timing 기록.
- 같은 sequence를 PIE의 StateMachine state로 binding하여 재생 → 같은 timing에 발화 확인.

기대: 1-frame 차이 없음 (예측 보정 효과).

---

## 6. STEP 15 — 회귀 검증 가이드

### 6.1 SingleNode 회귀 (AnimSequenceEditorTab)

`UAnimSingleNodeInstance` 는 본 작업에서 무수정. P-Fix5 동작 유지.

검증:
1. AnimSequenceEditorTab으로 sequence preview 열기.
2. Sound notify 포함 sequence 재생 → 정상 발화 확인.

기대: 변경 없음.

### 6.2 BakedAnimTime 회귀

`USkeletalMeshComponent::BakedAnimTime` 은 `AnimInstance->GetCurrentTime()` mirror ([SkeletalMeshComponent.cpp:409](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:409)). StateMachine 모드에서 `CurrentTime`은 STEP 6의 mirror 코드로 `PredictedCurrStateTime` (활성 state의 state-local time) 동기.

외부 caller grep:
```
$ grep -rn "GetBakedAnimTime\|BakedAnimTime" KraftonEngine/Source --include=*.cpp
```
- AnimSequenceEditorTab (P-Fix5 후 query 패턴) — SingleNode 모드만 사용 → 영향 없음.
- 다른 caller가 있다면 StateMachine 모드에서 BakedAnimTime 의 의미가 "활성 state의 local time" 임을 인지 필요. 외부 caller 가 기대하는 의미와 다를 위험 → **§7.5 보고 항목**.

### 6.3 Lua GetTriggeredNotifies 회귀

[LuaSkeletalMeshBindings.cpp:787-807](../KraftonEngine/Source/Engine/Scripting/LuaSkeletalMeshBindings.cpp:787) `GetTriggeredNotifies` 는 `AnimInstance->GetTriggeredNotifiesThisFrame()` 호출. 본 작업으로 StateMachine 모드에서도 채워짐 → Lua side 정상 query 가능.

기대: 회귀 없음, 오히려 기존에 받지 못하던 notify 이름들이 채워짐.

### 6.4 EditorConsole AnimGraph debug 회귀

[EditorConsoleWidget.cpp:1434](../KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1434) 부근에서 `SetAnimationMode(AnimationGraph)` + `SetRootGraph(SequencePlayer)` 주입.

본 작업으로 `UAnimGraphInstance::GetActiveNotifies` 및 `GetEffectivePlayLength` 가 SequencePlayer root에 대해 정상 동작 → notify 발화 가능. 기존 동작은 silent 였으므로 회귀 아니라 개선.

기대: 콘솔 debug 명령으로 SequencePlayer root 주입한 sequence가 notify 발화. 회귀 없음.

---

## 7. 본 prompt scope 외 사항 보고

### 7.1 D3 Stage 2 (Lua binding) — 미적용

본 작업은 dispatch path를 열어 C++ 측 `DispatchTriggeredNotifies` 가 도달하도록 함. **D3 §3의 Lua payload-query 모델 (b1~b4, L1~L3, C1~C3)** 은 본 prompt scope 외이므로 미적용.

- Lua 가 받는 `TriggeredNotifiesThisFrame` 은 여전히 `TArray<FName>` (이름만).
- ShakeParams 13필드는 Lua side에서 receive 불가 → C++ dispatch 측이 직접 처리.
- C++ → Lua callback hook 도 미신설.

후속 prompt (D3 §5.2의 옵션 조합 선택 후) 에서 처리.

### 7.2 옵션 2 (dispatch path를 graph 노드 내부로 이동) — 미채택

본 작업의 1-frame 지연 예측 보정은 우회책. 진정한 해결은 Evaluate 안에서 SequencePlayer 자체가 notify trigger를 수행하는 옵션 2. 본 prompt는 옵션 1 scope.

- transition frame에서 보정값이 잘못된 도메인에 있을 위험 (state flip 직전 frame에 예측이 PredictedCurrStateTime > Length 인 케이스). 단순화로 수용.
- 정밀 동기화 요구 시 옵션 2 채택을 위한 후속 prompt 필요.

### 7.3 blend ratio 기반 dual-active — 미채택

전제 1 (단일 활성) 하에 blend 중간에 To state notify 누락 가능 (D4 §G1.5).

- BlendDuration > 0.3s 의 transition 시 To의 초기 notify (TriggerTime < BlendDuration) 누락.
- footstep 등 정밀 동기화 요구 시 dual-active 시그니처 변경 필요 → 별도 prompt.

### 7.4 nested StateMachine의 SubLengthHint

- I1 STEP 4 `GetEffectivePlayLength` 는 부모 state의 `SubLengthHint` 를 우선 사용. 0이면 재귀로 자식까지 내려감 → 자식 SequencePlayer의 Length까지 도달 가능 — **nested 케이스도 자동 처리**.
- 단 `BuildStateMachineNodeFromLua` ([LuaSkeletalMeshBindings.cpp:287-345](../KraftonEngine/Source/Engine/Scripting/LuaSkeletalMeshBindings.cpp:287)) 가 nested state의 `SubLengthHint` 를 어떻게 set 하는지 확인 필요 — Lua config 의 `length` 필드를 명시하지 않으면 0일 위험. 본 prompt에서는 재귀 fallback 으로 처리하므로 silent 위험 작음.

### 7.5 SkeletalMeshComponent::BakedAnimTime의 state-aware 화

본 prompt에서 BakedAnimTime mirror 처리는 STEP 6의 base mirror 코드로 최소한만 보장. 외부 caller가 BakedAnimTime을 "전체 anim 누적 time" 으로 해석하면 StateMachine 모드에서 의미가 다름 — state 전환 시 BakedAnimTime이 갑자기 다른 도메인 값으로 점프.

후속 prompt에서 외부 caller 점검 + 의미 일관성 결정 필요.

---

## 8. 변경된 파일 목록 (최종)

| 파일 | 변경 종류 | STEP |
|---|---|---|
| [AnimGraph_StateMachine.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h) | getter 3개 + setter 1개 추가 (public inline) | 2, 9 |
| [AnimInstance.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h) | `SetEvaluationTime` virtual로 변경 (1줄) | 9 |
| [AnimStateMachineInstance.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h) | 멤버 2개 + override 4개 + 헬퍼 3개 (static 2 + private 1) 추가 | 3, 9 |
| [AnimStateMachineInstance.cpp](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp) | override 4개 + 헬퍼 3개 구현 + `SetStateMachineGraph` 본체에 reset 1줄 추가 + `<algorithm>`, `<cmath>` include 추가 | 4, 5, 6, 9, 12 |
| [AnimGraphInstance.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraphInstance.h) | override 2개 선언 | 7 |
| [AnimGraphInstance.cpp](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraphInstance.cpp) | override 2개 구현 (헬퍼 재사용) + include 1개 추가 | 7, 12 |
| `Document/animnotify_implementation_I1.md` | 신규 작성 (본 문서) | 전 STEP |

총 변경 파일 수: 6개 (구현) + 1개 (문서).

---

## 9. 완료 조건 점검

| 조건 | 결과 |
|---|---|
| 1. STEP 1~15 모두 진행 (skip 없음) | ✓ STEP 14-15는 사용자 직접 검증 필요 — 본 문서 §5/§6에 가이드 제공 |
| 2. STEP 13 빌드 에러 0건 | ✓ MSBuild Debug|x64 성공, `KraftonEngine.exe` 생성 |
| 3. STEP 14 PIE 검증 3개 시나리오 통과 | ⏳ **사용자 manual 검증 대기** — §5 가이드 참고. 본 환경에서 PIE 직접 실행 불가. |
| 4. STEP 15 회귀 검증 4개 시나리오 통과 | ⏳ **사용자 manual 검증 대기** — §6 가이드 참고. 본 환경에서 PIE 직접 실행 불가. |
| 5. `I1.md` 작성 완료 | ✓ 본 문서 |
| 6. 본 prompt 명시 외 변경 0건 | ✓ §7 보고 항목은 모두 scope 외 — 변경 없음 |

**PIE 동작 검증 및 회귀 검증은 본 환경에서 직접 실행 불가** — editor를 켜고 사용자가 §5, §6 가이드대로 확인해야 한다. 모든 정적 검증과 빌드는 통과.

---

## 10. 사용자 검증 후 후속 절차

1. **PIE 검증 결과 보고**: §5.1, §5.2 표의 "실측" / "통과" 열을 채워 알려주기. 실패 시 정확한 step + console 로그 첨부.
2. **회귀 검증 결과 보고**: §6.1~§6.4 4개 시나리오 결과.
3. **이후 작업 선택**:
   - PIE 통과 시: D3 §3 Lua binding 옵션 조합 결정 → 후속 구현 prompt.
   - 1-frame 지연 또는 정밀도 문제 발견 시: 옵션 2 검토 prompt (Evaluate 안에서 notify 발화).
   - dual-active 필요 시: 전제 1 해제 + 시그니처 변경 검토 prompt.
