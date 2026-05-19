# [Claude Code 프롬프트] 파트 A 계획서 보강 — 길 2(노드 field화) 반영

## 0. 이 프롬프트의 목적과 한계

`Document/animation_partA_blend_statemachine_plan.md`(이하 **기존 계획서**)는 이미
작성되어 있다. 이 프롬프트는 그 계획서를 **개정**하는 작업이다. 사용자 검토 결과
6절 미해결 #3(노드별 시퀀스 ref + 캐시)을 **파트 A 범위 안으로 끌어들이는 결정
(이하 "길 2")**이 내려졌고, 그 결정이 계획서의 여러 섹션에 연쇄 수정을 일으킨다.

**이 프롬프트로 하지 말 것:**
- 실제 구현 코드 작성·수정 (`.h`/`.cpp` 일절 금지)
- 기존 계획서를 폐기하고 처음부터 새로 쓰기 — 개정이지 재작성이 아니다
- Notify/Lua 설계 (파트 B 범위 유지)

**이 프롬프트로 할 것:**
- 1절의 확정 결정을 반영해 기존 계획서의 영향 섹션을 수정
- 2절의 보강 확인을 코드로 수행하고 결과를 계획서에 반영
- 개정된 계획서를 같은 경로 `Document/animation_partA_blend_statemachine_plan.md`에
  덮어쓰기 (개정 전 버전은 `..._plan_v1_backup.md`로 보존)

작성 언어: **한국어 본문 + 코드/심볼명은 영어 원문 그대로.**

이 개정 계획서가 사용자 검토로 확정되면, 구현 프롬프트(Blend / StateMachine 분리)가
이어진다. 이 프롬프트는 거기까지 가지 않는다 — **계획 개정까지만.**

---

## 1. 확정된 결정 — 길 2

### 1.1 결정의 핵심 (★ 용어 주의)

**노드(`FAnimGraphNode_SequencePlayer`)가 평가 입력을 자체 field로 보유한다.**
구체적으로 `Sequence` / `DataModel` / `TrackToBoneIndex`를 `FAnimEvalContext`에서
**노드의 멤버 field로 이동**한다.

**★ 소유(own) 아님 — 보유(hold / reference)다.** 이 구분을 코드 설계에 정확히
반영하라:
- 노드는 `const UAnimSequence*` / `const UAnimDataModel*`를 **raw pointer ref로
  hold**한다. asset의 수명은 여전히 asset 시스템이 책임진다. **노드 소멸자가
  asset을 해제하거나 `delete`하지 않는다.** `unique_ptr`/`shared_ptr`로 asset을
  감싸지 않는다.
- 이것은 UE의 "AnimInstance가 asset을 직접 own"하는 방식을 **회피**하기 위한
  설계다. 노드는 "평가에 필요한 입력을 가리키는 ref"만 들고, asset 자체의
  생명주기에는 관여하지 않는다.
- `TrackToBoneIndex`는 노드가 **값으로 보유**(`TArray<int32>`)한다. 이건 캐시이지
  외부 asset이 아니므로 노드가 소유해도 무방하다.

### 1.2 결정의 근거 (계획서 §6-3 재론 종결)

기존 계획서는 #3을 "파트 A 범위 초과"로 미뤘으나, 그 미룸의 실제 이유는
"불가능"이 아니라 "시그니처 전환과 겹쳐 SingleNode 동치성 검증이 무거워짐"이었다.
사용자 결정은 다음 근거로 **지금 함께 처리**한다:
- `UAnimSingleNodeInstance`가 UE식으로 asset을 직접 소유하도록 바꾸면 수정 범위가
  과대하다. 반대로 **노드가 평가 입력을 field로 갖는 방향**은 `graphnode` 도입 시
  인터페이스·로직 일관성을 주는 비용으로 지불할 가치가 있다.
- StateMachine의 각 `FAnimState`가 sub-graph(`FAnimGraphNode_SequencePlayer`)를
  서로 다른 시퀀스로 가져야 하는데, `FAnimEvalContext.DataModel` 단일 필드로는
  from/to 동시 평가가 불가능하다. 노드 field화가 이 문제의 정공법이다.
- 기존 계획서 §4.4가 `Ctx`를 시간만 패치하고 `DataModel`을 패치하지 않은 것은
  **명백한 결함**이었다. 노드 field화는 이 결함 자체를 소멸시킨다 (DataModel이
  Ctx에 없으므로 패치 대상이 아님).

### 1.3 길 2가 일으키는 연쇄 수정 (계획서 개정 대상)

아래는 길 2 채택으로 기존 계획서에서 **반드시 바뀌어야 하는** 부분이다. 개정
계획서는 각 항목을 처리해야 한다.

1. **`FAnimEvalContext` 구조 변경.**
   - 제거: `const UAnimDataModel* DataModel`, `const UAnimSequence* Sequence`,
     `const TArray<int32>* TrackToBoneIndex`.
   - 잔존/추가: `const USkeleton* Skeleton`, `float DeltaTime`(파트 A에서 추가),
     `float TimeSeconds`, 그리고 미해결 #1의 `UAnimInstance* OwningInstance`
     (BoolVariable 평가용 — §1.5 참조).
   - `TimeSeconds`의 거취: 노드 field화 이후에도 StateMachine이 자식에 상태별
     시간을 주입해야 하므로 `TimeSeconds`는 **남긴다**. 단 그 의미가 "현재 노드가
     평가해야 할 로컬 시간"으로 재정의된다. 개정 계획서가 이 의미를 명확히 적을 것.

2. **`FAnimGraphNode_SequencePlayer`에 field 추가.**
   - `const UAnimSequence* Sequence = nullptr;` (ref, not owned)
   - `const UAnimDataModel* DataModel = nullptr;` (ref, not owned — `Sequence`에서
     파생 가능하나 평가 hot path에서 매번 `GetDataModel()` 호출을 피하려면 캐시로
     둘 수 있음. 개정 계획서가 "Sequence만 두고 DataModel은 매번 조회" vs "둘 다
     field로 캐시" 중 택1하고 근거를 적을 것.)
   - `TArray<int32> TrackToBoneIndex;` (값 보유 — 트랙→본 인덱스 캐시)
   - 시퀀스를 set하고 캐시를 빌드하는 메서드(예: `SetSequence(const UAnimSequence*)`
     또는 `Initialize(...)`). 이 메서드가 `RebuildTrackToBoneIndex`의 로직을
     노드 내부로 이관한 형태가 된다.
   - 노드는 여전히 stateless에 가깝지만(시간은 `Ctx.TimeSeconds`로 받음) 평가
     **입력**은 보유하게 된다. `Evaluate`가 non-const 멤버 함수임이 스캔으로
     확정됐으므로 field 보유는 시그니처상 문제없다.

3. **`FAnimGraphNode_SequencePlayer::Evaluate` 본문 수정.**
   - 현재 `Ctx.DataModel` / `Ctx.TrackToBoneIndex`를 보던 부분을 `this->DataModel`
     (또는 `this->Sequence->GetDataModel()`) / `this->TrackToBoneIndex`로 변경.
   - 시간은 계속 `Ctx.TimeSeconds`에서 받음.
   - 출력은 §2(시그니처 전환)에 따라 `TArray<FTransform>&`.

4. **`UAnimSingleNodeInstance` 수정.**
   - 클래스 구조 자체는 유지(UE식 asset 직접 소유로 갈아엎지 않음).
   - `SetAnimation`(또는 그에 준하는 시퀀스 지정 지점)에서, 자신이 보유한
     `FAnimGraphNode_SequencePlayer SequencePlayer` 값 멤버(`AnimSingleNodeInstance.h:35`)에
     **DataModel/시퀀스를 setting**하고 트랙→본 캐시 빌드를 트리거한다.
   - `EvaluateGraph`는 더 이상 `Ctx`에 `DataModel`/`TrackToBoneIndex`를 채우지
     않는다(필드가 없어졌으므로). `Ctx`에는 `Skeleton`/`TimeSeconds`/`DeltaTime`/
     `OwningInstance`만 채운다.
   - **개정 계획서는 이 변경이 SingleNode의 비주얼 동작을 보존함(키 샘플링 결과
     동치)을 명시하고, A1 검증 마일스톤을 그에 맞게 갱신할 것.**

5. **`UAnimInstance::TrackToBoneIndex` / `RebuildTrackToBoneIndex` 제거.**
   - 결정: **제거** 방향. 캐시 정본이 노드로 내려갔으므로 인스턴스 레이어에 같은
     멤버를 남기면 정본이 둘이 된다.
   - 단 **제거 실행 전 `RebuildTrackToBoneIndex`와 `TrackToBoneIndex`의 호출처/
     참조처를 코드로 전수 확인**하라 (탐색 범위: `Animation/`, `Component/`).
     예상치 못한 호출처(컴포넌트·에디터 등)가 있으면 개정 계획서에 그 목록과
     각 호출처를 어떻게 처리할지(노드 경유로 대체 / 삭제)를 명시.
   - 호출처 전수 확인 결과를 개정 계획서의 "보강 확인 결과" 섹션에 파일·라인으로
     기록한 뒤 제거 설계를 확정.
   - `RebuildTrackToBoneIndex`의 **로직 자체**(트랙 FName → `Skeleton`
     `FindBoneIndexByName` resolve)는 사라지지 않고 노드의 `SetSequence` 류
     메서드 내부로 **이동**한다. "삭제"가 아니라 "이관"임을 명확히.

6. **기존 계획서 §4.4 재작성.**
   - 기존 §4.4는 StateMachine이 `Ctx`를 복사·패치할 때 `TimeSeconds`만 패치했다.
     이는 `DataModel`을 패치하지 않아 from/to 동시 평가 시 결함이 있었다.
   - 길 2 이후 `DataModel`은 `Ctx`에 없고 각 sub-graph 노드의 field이므로, **패치
     대상은 `TimeSeconds` 하나로 줄어든다.** §4.4는 이 단순화를 반영해 재작성된다.
   - 상태별 시간 메커니즘(`StateLocalTimes[]`, 자식 호출 직전 `ChildCtx.TimeSeconds`
     덮어쓰기)은 그대로 유효하다 — 오히려 더 깨끗해진다. 개정 계획서가 이를 확인.
   - looping wrap 정책(`SubLengthHint`, `FAnimState::bLooping`)도 유지하되,
     이제 `SubLength`를 노드의 `Sequence`에서 직접 얻을 수 있는지(`Sequence->
     GetPlayLength()`) 검토하고, 가능하면 `SubLengthHint` 수동 지정 대신 노드
     field에서 조회하는 안으로 개선할 것.

7. **기존 계획서 §4.5 수정.**
   - `GetActiveDataModel() const` → `nullptr` 반환 권고는 **소멸**한다. 트랙→본
     캐시가 노드로 내려갔으므로 인스턴스 레이어가 DataModel을 노출할 이유가
     없어졌다. 이 항목 자체를 §4.5에서 제거하거나, `GetActiveDataModel`이라는
     훅 자체의 존치 여부를 재검토(파트 B Notify가 쓸 가능성이 있으면 존치, 아니면
     제거 후보로 §6에 기록).
   - `GetEffectivePlayLength` / `GetActiveNotifies` 관련 서술은 영향 없으면 유지.

8. **기존 계획서 §6-3 항목 종결.**
   - #3은 "파트 A 범위 초과"에서 "**파트 A에서 처리 — 길 2 채택**"으로 상태 변경.
   - §6 표에서 #3을 "해결됨(길 2)"로 표기하고, 길 2의 구현이 어느 단계에서
     일어나는지(§5 구현 순서) 가리킬 것.

### 1.4 길 2가 구현 순서(§5)에 미치는 영향

기존 계획서 §5의 A1 단계는 "`Evaluate` 시그니처 전환"이었다. 길 2로 인해 SequencePlayer
한 노드에 **출력 형식 변경(`FMatrix`→`FTransform`)과 입력 소스 변경(`Ctx`→field)**이
동시에 일어난다. 두 변경이 겹치면 SingleNode 회귀 시 원인 격리가 어렵다.

개정 계획서 §5는 이를 **두 하위 단계로 분리**할 것:
- **A1a — 입력 소스 전환**: `Sequence`/`DataModel`/`TrackToBoneIndex`를 노드 field로
  이동, `Ctx`에서 제거, `UAnimSingleNodeInstance`가 노드에 setting. 출력은 아직
  `TArray<FMatrix>` 유지. 검증: SingleNode 비주얼 동치.
- **A1b — 출력 형식 전환**: `Evaluate` 출력 파라미터 `TArray<FMatrix>&`→
  `TArray<FTransform>&`. 검증: SingleNode 비주얼 동치 + `FTransform`→`FMatrix`
  변환 경계 1지점 확인.
- 두 단계 각각이 독립적으로 SingleNode 동치성을 검증받는다. 순서는 a→b 권장이나,
  개정 계획서가 어느 순서가 회귀 격리에 유리한지 판단해 명시할 것.

### 1.5 길 2와 무관하게 유지되는 결정 (재론 금지)

다음은 기존 계획서에서 이미 확정됐고 길 2가 바꾸지 않는다:
- 출력 형식 = `FTransform` (시그니처 전환).
- StateMachine 인스턴스 = 새 파생 `UAnimStateMachineInstance`, `UObjectManager::
  CreateObject<T>()` 생성.
- Blend 산술 = TRS+Slerp, Blend2/BlendN 공용 합성 유틸.
- Notify/Lua = 파트 B 대기, enum 자리만.
- `FAnimEvalContext`에 `OwningInstance` 추가(미해결 #1) 및 `bPaused`시 `DeltaTime=0`
  세팅(미해결 #2) — 기존 계획서 §6 권고 유지.

---

## 2. 계획 개정 전 보강 확인 항목

개정 작업 중 다음을 코드로 확인하고, 결과를 개정 계획서의 "보강 확인 결과"
섹션에 파일·라인으로 기록하라. 추측 금지.

1. **`RebuildTrackToBoneIndex` / `TrackToBoneIndex`의 호출처·참조처 전수.**
   `Animation/` + `Component/` 범위에서 grep. 각 참조가 (a) 단순 제거 가능한지,
   (b) 노드 경유로 대체해야 하는지 분류.

2. **`UAnimSingleNodeInstance`의 시퀀스 지정 진입점.** `SetAnimation` 또는 그에
   준하는 메서드의 정확한 위치·시그니처, 그리고 거기서 현재 `RebuildTrackToBoneIndex`가
   어떻게 호출되는지. 길 2 이후 이 지점이 "노드에 setting"으로 바뀌므로 정확한
   현 상태를 알아야 한다.

3. **`UAnimSequence` / `UAnimSequenceBase`의 `GetDataModel()` / `GetPlayLength()`
   시그니처.** 노드가 `Sequence` field에서 DataModel·길이를 조회할 수 있는지
   확인 (§1.3-2, §1.3-6의 `SubLength` 조회 가능성 판단).

4. **`FAnimEvalContext`의 현재 두 호출자가 채우는 필드 전체.** `AnimInstance.cpp:
   103-109`, `AnimSingleNodeInstance.cpp:44-50`. 길 2로 `DataModel` 등을 제거할 때
   이 호출자들에서 어떤 줄이 삭제·변경되는지 정확히.

5. 그 외 개정에 필요한 사실은 확인하되, 확인 못 한 것은 `[미확인]`으로 표기.

---

## 3. 개정 계획서가 갖춰야 할 형태

- 기존 계획서의 섹션 구조를 유지하되, 1절에서 지정한 연쇄 수정을 모두 반영.
- 맨 앞에 **"v2 개정 요약"** 섹션을 신설: 길 2 채택으로 무엇이 바뀌었는지, v1
  대비 변경된 섹션 목록을 표로.
- **"보강 확인 결과"** 섹션: 2절 확인 항목의 코드 근거.
- 길 2로 **소멸한 항목**(§4.5의 `GetActiveDataModel`→nullptr, §6-3의 미룸)은
  삭제하지 말고 "v1에서 이러했으나 길 2로 해소됨"으로 짧게 남겨 추적성을 유지.
- §6 미해결 표 갱신: #3을 해결됨으로, 새로 생긴 미해결이 있으면 추가.
- §5 구현 순서: A1을 A1a/A1b로 분리한 표.
- 마지막에 "이 개정으로 구현 프롬프트 분리(Blend/StateMachine)로 진입 가능한가"
  자평.

---

## 4. 작업 수칙

- **코드를 작성하지 마라.** 산출물은 개정된 계획서 마크다운 1개. 헤더 골격은
  계획서 내 코드블록 스케치로만.
- 개정 전 계획서를 `animation_partA_blend_statemachine_plan_v1_backup.md`로 먼저
  복사 보존한 뒤, 원래 경로에 v2를 덮어쓴다.
- 2절 보강 확인은 실제 파일을 열어서. `[미확인]` 정직 표기.
- **★ own vs hold 구분을 코드 설계 전반에 정확히 반영** — 노드는 asset을 ref로
  hold할 뿐 own하지 않는다(§1.1). 노드 소멸자가 asset 수명에 관여하는 설계가
  나오면 그 자체가 오류다.
- 1절의 확정 결정은 재론하지 않는다. 그 위에서 개정한다.
- **스코프 엄수:** Notify/Lua 구현 설계 금지(파트 B). 길 2가 건드리는 것은
  SequencePlayer 노드 / `FAnimEvalContext` / `UAnimSingleNodeInstance`의 입력
  주입 경로 / `UAnimInstance`의 캐시 멤버까지다. 그 밖으로 번지면 §6에 기록하고
  멈춘다.
- `UAnimSingleNodeInstance`의 **비주얼 동작 보존**은 길 2에서도 불변식이다 —
  클래스 구조를 UE식으로 갈아엎지 않으며, 입력 주입 경로만 바뀐다. 개정 계획서가
  이 동치성을 A1a/A1b 각각에서 어떻게 검증하는지 명시할 것.
