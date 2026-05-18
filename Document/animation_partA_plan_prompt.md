# [Claude Code 프롬프트] 파트 A 구현 계획 수립 — Animation Blend / State Machine

## 0. 이 프롬프트의 목적과 한계

이 프롬프트는 **계획(설계) 수립 전용**이다. Animation Blend 노드와 State Machine
노드(이하 파트 A)를 구현하기 위한 **설계 계획서**를 산출하는 것이 유일한 목적이다.

**이 프롬프트로 하지 말 것:**
- 실제 구현 코드 작성 (`.h`/`.cpp` 신규 작성·수정 일절 금지)
- 기존 파일 수정 / 리팩터 실행
- Notify dispatch / Lua 바인딩 설계 (이는 파트 B의 범위)

**이 프롬프트로 할 것:**
- 1절의 확정 사항을 전제로, 2절의 미보강 항목을 코드로 재확인
- 3절의 설계 과제에 대해 **헤더 인터페이스 골격 + 의사코드 + 구현 순서**를 계획서로 작성
- 결과를 `Document/animation_partA_blend_statemachine_plan.md` 로 출력

작성 언어: **한국어 본문 + 코드/심볼명은 영어 원문 그대로.**

이 계획서가 사용자 검토로 확정되면, 그것을 정본으로 **별도의 구현 프롬프트**
(Blend 구현 / StateMachine 구현으로 분리)가 이어진다. 이 프롬프트는 거기까지 가지 않는다.

---

## 1. 확정된 전제 (스캔 결과 + 사용자 결정)

`Document/animation_partA_infra_scan_result.md` 스캔과 그에 따른 사용자 결정으로
다음이 **확정**되었다. 계획서는 이 전제를 의심하지 말고 그대로 따른다.

### 1.1 스캔으로 확정된 현 인프라 사실

- `FAnimGraphNode_Base`(`AnimGraph.h:34-38`)는 abstract. 가상 소멸자 + 순수 가상
  `Evaluate(const FAnimEvalContext&, TArray<FMatrix>&)` 한 개. **Evaluate는 non-const
  멤버 함수** — 노드가 자체 상태를 보유·갱신해도 시그니처상 허용된다.
- `FAnimGraphNode_SequencePlayer`(`AnimGraph.h:45-48`)는 **stateless**. 멤버 0개,
  시간은 `Ctx.TimeSeconds`에서 받음. 키 보간은 TRS 분리 + `FQuat::Slerp`
  (`AnimGraph.cpp:122-124`), 최종적으로 `FTransform(P,R,S).ToMatrix()`로 합성
  (`AnimGraph.cpp:126`).
- `AnimGraph`(`AnimGraph.h:53-63`)는 `std::unique_ptr<FAnimGraphNode_Base> Root`
  단일 슬롯 + `SetRoot`/`GetRoot`/`Evaluate`만. **노드 컨테이너·동적 add/remove/swap
  없음.**
- `UAnimInstance`(`AnimInstance.h`)가 `std::unique_ptr<AnimGraph> AnimGraphPtr`를
  소유. 단 `UAnimSingleNodeInstance`는 이 `AnimGraphPtr`을 set하지 않고
  `EvaluateGraph()`를 override해 자체 보유한 `FAnimGraphNode_SequencePlayer` 값
  멤버를 직접 호출 — 즉 **`AnimGraph`/`Root` 메커니즘은 현재 어떤 경로에서도
  활성화돼 있지 않다.**
- `UAnimInstance` 파생은 현재 `UAnimSingleNodeInstance` **하나뿐.**
- `UAnimInstance`에 `BoolVariables`/`FloatVariables`/Notify delegate/스크래치 포즈
  풀은 **없음.** Notify는 `TArray<FName> TriggeredNotifiesThisFrame` 폴링 패턴.
- `UAnimInstance`의 protected 가상 훅: `GetEffectivePlayLength()`,
  `GetActiveNotifies()`, `GetActiveDataModel()` — 파생이 활성 시퀀스 메타를 제공하는
  구조.
- `FAnimEvalContext`(`AnimGraph.h:22-29`)는 `Skeleton`/`DataModel`/`Sequence`/
  `TrackToBoneIndex`/`TimeSeconds` 5필드. `DeltaTime` 없음. `Sequence` 필드는
  선언만 되어 있고 현재 두 호출자 모두 채우지 않아 항상 nullptr.
- Math: `FQuat::Slerp`(최단경로 처리 포함, `Quat.h:80-113`), `FQuat::ToMatrix`/
  `FromMatrix`, `FMatrix::ToQuat`(선언 `Matrix.h:120`), `FVector::Lerp`,
  `FTransform`(T/R/S 보유, `Transform.h:6-29`) 모두 존재. **통합 행렬 decompose
  함수와 전용 `ComposeTRS` 함수는 없음** — `FTransform::ToMatrix()`가 compose 역할.
- `UObject` 파생 인스턴스 생성은 `UObjectManager::CreateObject<T>()` 컨벤션을 따른다.
  `USkeletalMeshComponent::EvaluateAnimationPose(const UAnimSequence*, float)`가
  그 생성 경로를 포함한다.

### 1.2 사용자가 내린 설계 결정 (확정 — 계획서는 이 노선으로만 작성)

1. **포즈 전달 형식 = (나) `FTransform`.** `FAnimGraphNode_Base::Evaluate`의 출력
   파라미터를 `TArray<FMatrix>&` → `TArray<FTransform>&`로 **수정한다.** 이유: Blend가
   회전을 행렬 재분해 없이 `FQuat` 단위로 Slerp할 수 있어야 정확하며, `FTransform`은
   이미 T/R/S를 보유하고 SequencePlayer가 이미 사용 중이라 도입 비용이 사실상 0.
   - 이 시그니처 변경은 다음으로 전파된다(계획서가 영향 범위를 정확히 명시할 것):
     `FAnimGraphNode_Base::Evaluate` / `FAnimGraphNode_SequencePlayer::Evaluate`
     (`AnimGraph.cpp`) / `AnimGraph::Evaluate` / `UAnimSingleNodeInstance::EvaluateGraph`
     (`AnimSingleNodeInstance.cpp`) / `UAnimInstance::EvaluateGraph`.
   - `UAnimInstance::OutputLocalPose`의 타입, `GetOutputLocalPose()` 반환형,
     그리고 그 결과를 소비하는 `USkeletalMeshComponent` 측 FK 코드
     (`RebuildMeshSpaceBoneMatrices` 등)도 영향을 받는다. 계획서는 이 소비 지점까지
     영향 범위에 포함해 명시할 것. **단 컴포넌트 측 FK/스키닝 코드 자체의 재설계는
     파트 A 범위가 아니며, "어디서 `FTransform`→`FMatrix` 변환이 일어나야 하는가"의
     경계만 계획서가 짚는다.**

2. **StateMachine을 담을 인스턴스 = (A) 새 파생 `UAnimStateMachineInstance`.**
   `UAnimInstance`를 상속하는 신규 클래스를 만들고, 그 클래스가 `AnimGraphPtr`을
   실제로 생성·소유하며 `Root`를 `FAnimGraphNode_StateMachine`으로 set한다.
   `UAnimSingleNodeInstance`는 건드리지 않는다(기존 동작 보존).
   - 인스턴스 생성은 `UObjectManager::CreateObject<UAnimStateMachineInstance>()`
     컨벤션을 따른다.

3. **Blend 산술 정본 = TRS + Slerp.** 위치·스케일은 `FVector::Lerp`, 회전은
   `FQuat::Slerp`. LocalMatrix element-wise 보간은 채택하지 않는다.

4. **문서 범위 = Blend + StateMachine.** Notify dispatch / Lua 바인딩은 파트 B로
   분리하며 이 계획서에서 다루지 않는다. 단 StateMachine의 transition 조건 중
   Notify 기반 조건(`EKind::OnNotify` 류)은 **자리만 정의하고 평가 로직은 파트 B
   대기**로 명시할 것.

---

## 2. 계획 작성 전 보강 확인 항목

스캔에서 [미해결]로 남았던 항목 중 파트 A 설계에 필요한 것을 **계획서 작성
전에 코드로 확인**하라. 추측 금지. 확인 결과를 계획서 맨 앞 "보강 확인 결과"
섹션에 기록한 뒤 본문 작성에 들어간다.

1. **`UAnimInstance`의 `EvaluateGraph()` / `Update()`가 `OutputLocalPose`를
   어떻게 다루는지** — `TArray<FMatrix>` → `TArray<FTransform>` 전환 시 영향 받는
   라인을 정확히 특정. 특히 컴포넌트가 `GetOutputLocalPose()` 결과를 받아 FK를
   도는 지점(`Component/` 디렉터리)의 파일·라인.

2. **`USkeletalMeshComponent::EvaluateAnimationPose`의 인스턴스 생성 경로 전체** —
   `CreateObject<T>()`로 어떤 타입을 만들고, `AnimInstance` 멤버에 어떻게 대입하며,
   교체 시 기존 인스턴스를 어떻게 처리하는지. `UAnimStateMachineInstance`를 이
   경로에 어떻게 끼울지 판단하는 데 필요.

3. **`UObjectManager::CreateObject<T>()`의 시그니처와 제약** — 템플릿 인자 요건,
   `DECLARE_CLASS` 등 reflection 등록이 선행돼야 하는지. (범위: 해당 정의가
   `Object/` 등 지정 범위 밖이면 `[범위초과]`로 표기하고 호출 관습만 기록.)

4. **`USkeleton`/`FBoneInfo`의 본 트리 API** — `GetBones()`, `ParentIndex`,
   `FindBoneIndexByName`의 정확한 시그니처. Blend가 본 단위 루프를 돌 때, 그리고
   bind pose fallback을 만들 때 필요. (`Asset/Animation/Core/Skeleton.h` 추정.)

5. **`FTransform`의 보간 관련 API 유무** — `FTransform` 자체에 Lerp/Blend 류
   메서드가 이미 있는지. 없으면 Blend 노드가 본별로 `FVector::Lerp`+`FQuat::Slerp`를
   직접 조합해야 한다는 사실을 계획서에 명시.

위 5개 외에 설계에 필요한 사실이 더 있으면 그것도 확인하되, **확인 못 한 것은
계획서에 `[미확인]`으로 정직하게 표기**하고 그 불확실성이 설계의 어느 부분에
영향을 주는지 적는다.

---

## 3. 계획서가 다뤄야 할 설계 과제

계획서는 아래 섹션 구조를 따른다. **Blend와 StateMachine을 명확히 분리된 섹션으로**
둔다(다음 단계에서 구현 프롬프트를 둘로 쪼갤 수 있도록).

### 섹션 1. 공통 기반 변경 — `Evaluate` 시그니처 전환

- `FAnimGraphNode_Base::Evaluate`의 출력 파라미터를 `TArray<FTransform>&`로 바꾸는
  변경의 **전체 영향 범위**를 파일·라인 단위로 나열.
- 각 영향 지점이 어떻게 바뀌어야 하는지 의사코드 수준으로 기술. 특히:
  - `FAnimGraphNode_SequencePlayer::Evaluate` — 현재 마지막에 `.ToMatrix()`로
    합성하던 것을 `FTransform`을 그대로 출력하도록. bind pose fallback도
    `FTransform` 단위로.
  - `AnimGraph::Evaluate` / `UAnimInstance`·`UAnimSingleNodeInstance`의
    `EvaluateGraph` / `OutputLocalPose` 타입.
  - **`FTransform` → `FMatrix` 변환이 일어나야 하는 최종 경계 지점**을 한 곳으로
    특정(컴포넌트 FK 진입 직전이 자연스러움). 이 경계를 명확히 해서 파이프라인
    중간에 행렬↔TRS 왕복이 생기지 않도록.
- 이 변경이 `UAnimSingleNodeInstance`의 **기존 동작을 바꾸지 않음**을 어떻게
  보장하는지(동작 동치성) 기술.

### 섹션 2. Blend 노드 설계

- **`FAnimGraphNode_Blend2`** (2-way) 와 **`FAnimGraphNode_BlendN`** (N-way)의
  헤더 인터페이스 골격. 자식 노드 보유 방식은 `std::unique_ptr<FAnimGraphNode_Base>`
  (기존 `Root` 소유 관습과 일치).
- `Evaluate`의 의사코드:
  - 각 자식 노드를 평가해 자식별 `TArray<FTransform>` 포즈를 얻는다. 자식 포즈를
    담을 **임시 버퍼를 어디서 확보하는가** — 스크래치 풀이 현재 없으므로(스캔 S7),
    노드가 자체 멤버 버퍼를 갖는 안 / 매 평가 지역 할당 안 / `FAnimEvalContext`에
    풀을 추가하는 안 중 **장단점을 비교하고 권고안 1개를 제시**. (non-const
    Evaluate이므로 노드 멤버 버퍼는 시그니처상 허용됨.)
  - 본별 합성: 위치·스케일 `FVector::Lerp`, 회전 `FQuat::Slerp`. **N>2 회전 합성**은
    일반적 quaternion 가중평균 해가 없으므로 순차 Slerp 근사 / Blend2 트리 펼침 중
    하나를 권고하고 근거를 적을 것.
  - weight 정규화 규칙. weight 합이 0 또는 음수인 경계 케이스의 처리(bind pose
    fallback 등)를 명시 — 단순히 0번 자식을 내보내는 비대칭 처리는 피한다.
  - 자식 수와 weight 수 불일치 등 방어 처리.
- Blend2와 BlendN의 합성 산술이 동일하므로, **공용 합성 유틸**(예: 본별
  `FTransform` 2개 + alpha → `FTransform`)을 free function이나 static으로 두어
  Blend2/BlendN/StateMachine transition이 공유하는 구조를 권고. 이 유틸의
  시그니처를 명시.

### 섹션 3. State / Transition 데이터 구조

- `FAnimState` — 상태 1개. sub-graph(보통 `FAnimGraphNode_SequencePlayer` 또는
  Blend 노드)를 `std::unique_ptr<FAnimGraphNode_Base>`로 owned. 상태 이름,
  진입 시 시간 초기화 정책 플래그.
- `FAnimTransitionCondition` — 평가 단위 1개. 종류 enum(`TimeElapsed`,
  `BoolVariable`, `OnNotify`, `Custom`). **`OnNotify`/`Custom`은 자리만 정의하고
  평가 로직은 파트 B 대기로 명시.** `TimeElapsed`/`BoolVariable`만 이번에 평가
  가능하도록 설계.
- `FAnimTransition` — From/To 상태 인덱스 + BlendDuration + 조건 배열(AND 결합).
- **여기서 핵심 미결정 사항을 계획서가 결론지을 것:** `BoolVariable` 조건이 읽을
  변수 저장소가 현재 `UAnimInstance`에 없다(스캔 S7). `UAnimStateMachineInstance`에
  `TMap<FName,bool> BoolVariables`를 신설하는 안을 기본으로 하되, 이를
  base `UAnimInstance`에 둘지 새 파생에만 둘지 결정하고 근거를 적을 것. (파트 B의
  Lua 바인딩이 이 저장소를 건드릴 예정이라는 점도 고려.)

### 섹션 4. `FAnimGraphNode_StateMachine` 설계

- 헤더 인터페이스 골격: `States[]`, `Transitions[]`, 런타임 상태(활성 상태 인덱스,
  진행 중 transition, transition 경과 시간, 활성 상태 체류 시간).
- `Evaluate` 의사코드:
  - transition 미진행 시: 활성 상태에서 출발하는 transition들의 조건 평가 →
    트리거 시 진입.
  - transition 진행 중: from/to 두 상태를 각각 평가 → alpha 합성 → 완료 시 활성
    상태 갱신. **합성은 섹션 2의 공용 합성 유틸을 재사용**(중복 산술 금지).
  - 시간/dt 문제: 노드는 시간을 보유하지 않고 `Ctx.TimeSeconds`만 받는다(스캔 S2).
    transition 경과·상태 체류 시간 누적에 dt가 필요하다. **`FAnimEvalContext`에
    `DeltaTime`을 추가하는 안 vs `UAnimInstance` 측에서 누적해 노드에 전달하는 안**을
    비교하고 권고안을 제시. (`FAnimEvalContext` 변경은 호출자 2곳에 전파되므로
    영향 범위를 명시.)
  - 상태 진입 시 sub-graph 시간 초기화(`bResetTimeOnEnter`) 정책: SequencePlayer가
    stateless이고 시간을 `Ctx`에서 받으므로, "상태별 시간"을 어떻게 분리해 줄지가
    설계 난점이다. **이 문제를 계획서가 정면으로 다루고 해결안을 제시**하라 —
    SequencePlayer에 노드 로컬 시간을 도입하는 안(파트 2 코드 변경 수반),
    StateMachine이 상태별 시간 오프셋을 관리해 `Ctx.TimeSeconds`를 조정해 넘기는 안
    등. 파트 2 코드를 건드리는 안이라면 그 영향 범위를 분명히 하고, 건드리지 않는
    안이 가능하면 그쪽을 우선 검토.
- `UAnimStateMachineInstance`의 헤더 골격: `UAnimInstance` 상속, `AnimGraphPtr`
  생성·`Root`에 StateMachine 노드 set, protected 가상 훅(`GetEffectivePlayLength`
  등) override 방침, `BoolVariables` 저장소.

### 섹션 5. 구현 순서와 의존성

- 단계별 작업 표(작업 / 선행 조건 / 검증 마일스톤).
- Blend가 StateMachine transition 합성에 재사용되므로 **Blend 선행**. 근거 명시.
- 각 단계가 어디까지 독립적으로 검증 가능한지(SingleNode 동작 회귀 테스트 포함).
- 다음 단계에서 구현 프롬프트를 **Blend 구현 / StateMachine 구현 둘로 분리**할 수
  있도록, 표가 그 분리선을 명확히 드러낼 것.

### 섹션 6. 미해결 결정 사항 / 검증 안 된 가정

- 계획 과정에서 코드로 확정하지 못한 것, 사용자 판단이 필요한 선택지를 모은다.
- 각 항목에 영향 받는 섹션·단계를 명시.
- 파트 B(Notify/Lua)로 넘어가는 인터페이스 접점(`OnNotify` 조건, `BoolVariables`를
  Lua가 set하는 경로 등)을 "파트 B 대기"로 분명히 표시.

---

## 4. 작업 수칙

- **코드를 작성하지 마라.** 이 단계의 산출물은 계획서 마크다운 1개뿐이다.
  헤더 골격은 "인터페이스 스케치"로서 계획서 안에 코드블록으로 적되, 실제
  `.h`/`.cpp` 파일을 만들거나 고치지 않는다.
- 2절 보강 확인은 **실제 파일을 열어서** 한다. 추측한 것은 `[미확인]`으로 표기.
- 모든 설계 판단에 **근거**를 붙인다. "왜 이 안인가"가 없는 권고는 쓰지 않는다.
- 1절의 확정 전제(특히 1.2의 4개 결정)는 재론하지 않는다. 그 위에서 설계한다.
- **스코프 엄수:** Notify dispatch·Lua 바인딩의 *구현 설계*는 하지 않는다. 접점만
  남긴다. 파트 2의 기존 동작(`UAnimSingleNodeInstance` 재생)을 깨는 설계는 금지 —
  `Evaluate` 시그니처 전환이 SingleNode 동작을 보존함을 계획서가 입증할 것.
- 파트 2 코드를 수정해야 하는 설계 항목이 나오면(예: SequencePlayer 시간 도입),
  그것을 숨기지 말고 "파트 2 영향" 항목으로 명시하고 영향 범위를 정확히 적는다.
- 계획서 작성 후, 마지막에 **"이 계획서로 충분한가 / 추가 스캔이 필요한가"**를
  한 문단으로 자평하라.
