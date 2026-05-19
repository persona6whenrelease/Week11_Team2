# Animation Graph - Node 개념 정리

> Unreal Engine의 AnimGraph 구조를 기준으로, 자체 제작 엔진의 `GraphAnimInstance` 설계를 위한 Node 개념 정리 문서.

---

## 1. Node 기본 개념

### 1.1 Node란 무엇인가

**Node는 런타임(runtime)에 한 프레임의 pose를 다루는 "행위의 단위"다.**

흔한 오해는 `Node = animation 단위`로 보는 것이다. 이는 틀렸다. 층위가 다르기 때문이다.

- **Animation 에셋**: 디스크에 저장된 수동적(passive) 데이터. 키프레임의 묶음. 그 자체로는 아무 동작도 하지 않는다.
- **Node**: 매 프레임 실행되는 능동적(active) 객체. "pose를 만들거나, 가공하거나, 합성하는 행위" 그 자체.

같은 animation 에셋을 두 개의 Source node가 서로 다른 시간/속도로 동시에 재생할 수 있다. 에셋은 하나인데 node는 둘이다 → `Node = animation`이 성립하지 않는 결정적 근거.

### 1.2 Node의 본질 정의

> **Node는 입력 pose를 0개 이상 받아서, 출력 pose를 정확히 1개 내놓는 함수다.**

```
node : (Pose x Pose x ... x Pose) -> Pose
```

입력 pose의 개수가 node의 종류를 가른다. (0개 = Source, 1개 = Modifier, 2개+ = Blend)

이렇게 정의하면 복잡한 애니메이션은 **함수 합성(function composition)** 으로 표현된다.
각 node는 "pose를 받아 pose를 낸다"는 계약만 지키므로, 임의로 조합·재배열·재사용이 가능하다.

### 1.3 Update / Evaluate 두 관점

한 node의 책임은 두 개의 분리된 패스(pass)로 나뉜다. 호출 시점이 다르다.

| 관점 | Update | Evaluate |
|------|--------|----------|
| 하는 일 | status 갱신 (시간 진행, weight 보간, transition 판정) | 실제 pose 계산 |
| 다루는 데이터 | 시간, 가중치, 상태값 | Pose (본 transform) |
| 결과 저장 위치 | node 내부 상태 변수 | 반환되는 Pose 1개 |
| 비용 | 가벼움 | 무거움 |
| 실행 순서 | 먼저 | 나중 |

분리 이유: 멀티스레딩. 무거운 pose 계산(Evaluate)을 워커 스레드로 분리하려면,
"무엇을 얼마나 재생할지"(Update)와 "실제 pose 산출"(Evaluate)이 분리되어야 한다.

**대원칙: 결정은 Update, 실행은 Evaluate.**
Update가 status에 "무엇을 어떻게 할지"를 기록하고, Evaluate는 그 기록을 *읽어서 그대로 따를* 뿐
스스로 무언가를 결정하지 않는다. 이 원칙은 State Machine을 포함한 모든 node에 예외 없이 적용된다.

**모든 node가 공유하는 두 가지 규칙:**

1. **Update 규칙**: 자식(입력) node들을 먼저 `Update` → 그다음 자기 status 갱신
2. **Evaluate 규칙**: 자식들을 먼저 `Evaluate`해서 입력 pose 확보 → 그 pose들로 자기 출력 pose 계산 → 1개 반환

---

## 2. Node 종류별 정리

모든 node는 동일한 공통 인터페이스를 구현한다. node 종류 간 차이는
오직 "입력 pose가 몇 개인가"와 "Evaluate 안에서 무슨 연산을 하는가"뿐이다.

### 2.0 공통 인터페이스

```
abstract class Node:
    inputs : list<Node>        // 입력 pose를 공급하는 자식 node들 (0개 이상)

    abstract Update(dt: float)         // status 갱신만. pose 계산 안 함.
    abstract Evaluate() -> Pose        // 실제 pose 계산. pose 1개 반환.
```

---

### 2.1 Source Node — 생성자

| 항목 | 내용 |
|------|------|
| 역할 | 무(無)에서 pose를 생성. 재료는 pose가 아니라 animation 에셋. |
| input | pose 입력 0개 |
| output | pose 1개 |
| 대표 예 | `SequencePlayer`, `BlendSpacePlayer` |
| 비고 | 자체 엔진의 `SingleNodeInstance`가 사실상 이 node 1개에 해당 |

```
class SequencePlayerNode extends Node:
    inputs = []                     // 입력 없음
    animAsset : AnimationAsset      // 참조하는 애니메이션 에셋 (재료)
    currentTime : float = 0         // 재생 위치 (status)
    playRate : float = 1.0

    Update(dt):
        // status만 갱신 - 시간을 진행시킨다
        currentTime = currentTime + dt * playRate
        currentTime = wrap(currentTime, animAsset.length)   // 루프 처리

    Evaluate() -> Pose:
        // currentTime을 감싸는 두 키프레임을 찾아 보간
        (keyA, keyB, alpha) = animAsset.findKeyframes(currentTime)
        return interpolate(keyA, keyB, alpha)   // lerp / slerp
```

---

### 2.2 Modifier Node — 변형자

| 항목 | 내용 |
|------|------|
| 역할 | 입력 pose 하나를 받아 일부를 수정해서 내보냄. animation 에셋과 무관. |
| input | pose 입력 1개 |
| output | pose 1개 |
| 대표 예 | `Two Bone IK`, `TransformBone` |

```
class IKNode extends Node:
    inputs = [ child ]              // 입력 1개
    targetPosition : Vector         // 외부에서 주입되는 파라미터

    Update(dt):
        child.Update(dt)            // 자식을 먼저 update (재귀)
        // 이 node 자체는 갱신할 status가 거의 없음

    Evaluate() -> Pose:
        inputPose = child.Evaluate()                     // 자식 pose를 먼저 받고
        outputPose = applyIK(inputPose, targetPosition)  // 일부 본만 수정
        return outputPose
```

---

### 2.3 Blend Node — 합성자

| 항목 | 내용 |
|------|------|
| 역할 | 입력 pose 여러 개를 weight로 섞어 하나로 만듦. animation 에셋과 무관. |
| input | pose 입력 2개 이상 |
| output | pose 1개 |
| 대표 예 | `BlendListByBool`, `LayeredBoneBlend` |
| 비고 | animation을 직접 들고 있지 않음. "들어온 pose를 섞는 행위"만 담당. |

```
class BlendNode extends Node:
    inputs = [ childA, childB ]
    weight : float = 0.0            // 0이면 A, 1이면 B. status.
    targetWeight : float            // 외부 variable이 정해주는 목표값
    blendSpeed : float

    Update(dt):
        childA.Update(dt)
        childB.Update(dt)
        // weight를 목표값 쪽으로 부드럽게 이동 (status 갱신)
        weight = moveTowards(weight, targetWeight, blendSpeed * dt)

    Evaluate() -> Pose:
        poseA = childA.Evaluate()
        poseB = childB.Evaluate()
        return blendPoses(poseA, poseB, weight)   // 본별 lerp / slerp
```

> **참고 - 2-입력 vs N-입력**: pose를 섞는 바닥 연산(`lerp`/`slerp`)은 정의상
> 이항 연산(2-입력)이다. N개 blend가 필요하면 (A) BlendNode를 트리로 중첩하거나
> (B) node 내부 루프로 2-pose blend를 N-1번 반복한다. 어느 쪽이든 바닥 연산은 2-입력.
> 초기 구현은 2-입력 BlendNode 하나로 충분하다 (transition blend가 본질적으로 2-입력).

---

### 2.4 Output Node — 종착점

| 항목 | 내용 |
|------|------|
| 역할 | 입력 pose를 받아 graph의 최종 결과로 확정. |
| input | pose 입력 1개 |
| output | pose 1개 (graph 전체의 최종 출력) |
| 대표 예 | `FAnimNode_Root` |

```
class RootNode extends Node:
    inputs = [ child ]

    Update(dt):
        child.Update(dt)

    Evaluate() -> Pose:
        return child.Evaluate()     // 자식 pose를 그대로 최종 결과로
```

---

## 3. State Machine Node

State Machine node는 그 자체로 하나의 node이지만, 내부 구조와 동작이 복잡해
별도의 장으로 다룬다.

> 주의: 아래 pseudo code는 개념을 코드 레벨로 합리적으로 구성한 예시이며,
> 실제 엔진 구현에 맞춰 검증·조정이 필요하다.

### 3.1 핵심 개념 — State와 Transition

**State**: instance가 가질 수 있는 임의의 동작 상태. Idle, Walk, Run, Jump 등.
transition 중이 아닐 때, instance는 정확히 한 state에 "있다".

**Transition**: state와 state를 잇는 **연결선(edge) 그 자체**.
"전환 과정"이 아니라 정적인 데이터라는 점이 중요하다.

- transition은 graph가 만들어질 때 미리 정의되어 **항상 존재**한다.
  "Idle->Walk transition"은 캐릭터가 가만히 서 있을 때도 이미 존재한다 (아직 발동되지 않았을 뿐).
- transition은 두 부분으로 구성된다:
  - **rule (조건)**: "언제 넘어갈지". `speed > 0` 같은 boolean 함수.
    animation instance의 variable을 읽어 판정한다.
    (variable은 AnimGraph 바깥의 캐릭터 게임 로직이 매 프레임 채워 넣음)
  - **duration (전환 시간)**: "얼마에 걸쳐 넘어갈지". transition은 순간이 아니다.
    motion의 자연스러운 interpolation을 위해 일정 시간에 걸쳐 진행된다.

**정적 transition vs 활성 transition 구분:**

| 구분 | 성격 | 내용 |
|------|------|------|
| transition (정적) | 항상 존재하는 데이터 | from-state, to-state, rule, duration |
| 활성 transition (동적) | 발동되어 진행 중인 상태 | 진행 시간 같은 status를 가짐 |

비유: transition 자체는 지도 위의 도로다. 활성 transition은 그 도로를 지금 달리는 중인 것.
도로는 아무도 안 달려도 거기 있다.

### 3.2 State Machine은 그냥 하나의 node다

State Machine은 특별한 시스템이 아니라, Source/Blend/Modifier와 똑같이
`Update`/`Evaluate` 인터페이스를 구현하는 **하나의 node**다.
바깥에서 보면 그냥 "pose 1개를 내놓는 node"일 뿐이다.

다른 node와의 차이는 단 하나로 요약된다:

> **소유한 자식(state별 sub-graph) 중 무엇을 쓸지가 매 프레임 동적으로 바뀐다 = 선택성(selectivity)**

- node가 자식을 소유하는 것 자체는 특이한 게 아니다. 모든 node가 `inputs`로 자식을 소유한다.
  Blend node도 childA/childB를 소유한다.
- 특이한 것은 "선택"이다. Blend node는 항상 두 자식을 다 쓴다.
  State Machine은 N개 state sub-graph를 소유하지만 그중 1개(안정) 또는 2개(전환 중)만 골라 쓴다.

따라서 State Machine은 "남들과 다른 별종"이 아니라
**"남들과 똑같이 자식을 소유하되, 자식 선택이 동적인 node"** 일 뿐이다.

### 3.3 내부 구조

```
StateMachineNode
 ├── State A  ── 내부에 sub-graph 하나 (예: SequencePlayer -> Root)
 ├── State B  ── 내부에 sub-graph 하나
 └── Transition 목록 (A->B 등): rule + duration을 담은 정적 연결선들
```

| 항목 | 내용 |
|------|------|
| 역할 | variable로 transition을 판정해 어떤 state sub-graph를 쓸지 고르고, 전환 중이면 두 state의 pose를 blend. **pose를 직접 만들지 않는다.** |
| input | pose 입력은 외부 연결이 아니라 내부의 state별 sub-graph. 외부에서 들어오는 것은 pose가 아니라 animation instance의 variable. |
| output | pose 1개 (안정 상태면 현재 state의 pose, 전환 중이면 두 state pose의 blend 결과) |
| 대표 예 | `FAnimNode_StateMachine` |

State Machine은 항상 두 모드 중 하나에 있다:

1. **안정 상태(stable)**: transition 중이 아님. 정확히 한 state만 active.
2. **전환 중(transitioning)**: 두 state가 동시에 active. 진행도(alpha)로 둘을 blend.

### 3.4 Update 관점에서의 동작

State Machine의 `Update`는 세 가지 일을 순서대로 한다.
**"어느 모드인가, 어떤 state가 active인가"를 결정하는 단계.** pose 계산은 없다.

1. **transition 판정 (시작할지 결정)**
   transition 중이 아니라면, 현재 state에서 *나가는* transition들의 rule을 검사.
   rule은 animation instance의 variable을 읽어 판정. 어떤 rule이 참이 되면
   그 transition을 발동(진행 시간 0으로 리셋, "전환 중" 모드 진입).
   여러 rule이 동시에 참일 수 있으므로 우선순위(보통 목록 순서)로 하나만 선택.

2. **transition 진행 (이미 전환 중이라면)**
   진행 시간을 `dt`만큼 누적. 누적 시간이 `duration`에 도달하면 transition 완료 →
   도착 state를 현재 state로 확정하고 안정 상태로 복귀.

3. **active한 state의 sub-graph를 Update**
   - 안정 상태: 현재 state의 sub-graph 하나만 update.
   - 전환 중: 출발 state와 도착 state의 sub-graph **둘 다** update.
   (도착 state의 애니메이션도 시간이 흘러야 전환이 끝났을 때 올바른 시점의 pose가 나옴.
    전환 끝나고 나서야 update하면 0.2초간 멈춰있던 애니메이션이 갑자기 재생되는 꼴이 됨)

> Update의 책임은 "결정"에서 끝나지 않고, 결정된 state의 sub-graph를
> 실제로 `Update`까지 호출하는 데까지 간다.

### 3.5 Evaluate 관점에서의 동작

`Evaluate`는 무언가를 **정하지 않는다.** Update가 status에 기록해 둔 모드를
*읽어서 그대로 따를* 뿐이다 (대원칙: 결정은 Update, 실행은 Evaluate).

- **안정 상태**: 현재 state sub-graph를 `Evaluate`한 pose를 그대로 반환.
- **전환 중**:
  1. 출발 state sub-graph `Evaluate` → `poseFrom`
  2. 도착 state sub-graph `Evaluate` → `poseTo`
  3. 진행도 `alpha = 진행시간 / duration`을 weight로 두 pose를 blend
  4. blend 결과 반환

3번의 blend 연산은 Blend node의 `blendPoses`와 **완전히 동일**하다
(본별 lerp/slerp). → Blend node와 State Machine node가 `blendPoses`를 공유한다 (재사용 포인트).

`alpha`는 0→1로 흐른다. 시작 순간 `alpha=0`이라 `poseFrom`만, 끝 순간 `alpha=1`이라
`poseTo`만 보이며, 그 사이는 혼합. 이것이 부드러운 전환의 정체.

### 3.6 Pseudo Code

```
class State:
    subGraph : Node            // 이 state의 pose를 만드는 sub-graph (Root node)

class Transition:              // 정적 데이터 - 항상 존재
    fromState : State
    toState   : State
    rule      : function() -> bool   // animation instance variable을 읽어 판정
    duration  : float                // blend에 걸리는 시간

class StateMachineNode extends Node:
    states          : list<State>
    transitions     : list<Transition>
    currentState    : State
    // 활성 transition 관련 status (동적)
    activeTransition : Transition = null
    transitionTime   : float = 0       // 진행 시간 누적

    Update(dt):
        // --- 1) transition 판정: 시작할지 결정 ---
        if activeTransition == null:
            for t in transitions where t.fromState == currentState:
                if t.rule() == true:           // variable 기반 조건 충족
                    activeTransition = t       // 정적 transition을 발동
                    transitionTime  = 0
                    break                      // 우선순위: 먼저 참인 것 하나

        // --- 2) transition 진행: 끝났는지 확인 ---
        if activeTransition != null:
            transitionTime = transitionTime + dt
            if transitionTime >= activeTransition.duration:
                currentState     = activeTransition.toState   // 도착 state로 확정
                activeTransition = null                       // 안정 상태로 복귀

        // --- 3) active한 state의 sub-graph를 Update ---
        if activeTransition != null:
            activeTransition.fromState.subGraph.Update(dt)
            activeTransition.toState.subGraph.Update(dt)       // 둘 다
        else:
            currentState.subGraph.Update(dt)                   // 하나만

    Evaluate() -> Pose:
        // Update가 정해둔 모드를 읽어서 따를 뿐, 여기서 결정하지 않음
        if activeTransition == null:
            // 안정 상태: 현재 state pose 그대로
            return currentState.subGraph.Evaluate()
        else:
            // 전환 중: 두 state pose를 진행도로 blend
            poseFrom = activeTransition.fromState.subGraph.Evaluate()
            poseTo   = activeTransition.toState.subGraph.Evaluate()
            alpha    = transitionTime / activeTransition.duration
            return blendPoses(poseFrom, poseTo, alpha)
```

### 3.7 한 프레임 전체 흐름 예시

Idle → Walk 전환, `duration = 0.2초`, `dt = 1/60초` 가정:

```
[프레임 N] 캐릭터가 막 움직이기 시작 (speed: 0 -> 5)
  Update:
    1단계: 현재 state=Idle. "Idle->Walk" rule(speed>0) 검사 -> 참!
           activeTransition 발동. transitionTime = 0.
    3단계: Idle.subGraph.Update(dt), Walk.subGraph.Update(dt)   // 둘 다
  Evaluate:
    전환 중. alpha = 0 / 0.2 = 0.0
    결과 = blendPoses(Idle pose, Walk pose, 0.0) ~= Idle pose

[프레임 N+6] 전환 시작 후 0.1초 경과
  Update:
    2단계: transitionTime = 0.1. 0.2 미만 -> 전환 계속.
    3단계: Idle, Walk sub-graph 둘 다 Update
  Evaluate:
    alpha = 0.1 / 0.2 = 0.5
    결과 = blendPoses(Idle pose, Walk pose, 0.5)   // 반반 혼합

[프레임 N+12] 전환 시작 후 0.2초 경과
  Update:
    2단계: transitionTime = 0.2 >= duration -> 전환 완료!
           currentState = Walk 확정. activeTransition = null.
    3단계: Walk.subGraph.Update(dt)               // 이제 하나만
  Evaluate:
    안정 상태. 결과 = Walk.subGraph.Evaluate()    // 순수 Walk pose
```

---

## 4. 각 Node들의 연산 관계

### 4.1 평가 흐름 관점 — 트리의 후위 순회

graph는 Root node 포인터 하나만 들고 있으면 된다. 한 프레임은 두 번의 재귀로 끝난다.

```
class Graph:
    root : Node

    EvaluateFrame(dt) -> Pose:
        root.Update(dt)              // 1패스: 트리 전체 status 갱신
        finalPose = root.Evaluate()  // 2패스: 트리 전체 pose 계산
        return finalPose
```

각 node는 "내 자식 먼저, 그다음 나"만 지킨다. 자식이 누군지·몇 개인지는
node 종류가 정한다. 이 단순 규칙으로 임의 깊이의 트리가 평가된다
= **후위 순회(post-order traversal)**.

**예시 graph:**

```
Root
 └─ Blend
     ├─ SequencePlayer(walk)
     └─ IK
         └─ SequencePlayer(run)
```

**Update 패스 (status 갱신, 자식 먼저):**

```
Root.Update(dt)
└─ Blend.Update(dt)
   ├─ SequencePlayer(walk).Update(dt)   // currentTime 진행
   └─ IK.Update(dt)
      └─ SequencePlayer(run).Update(dt) // currentTime 진행
   // Blend 자신: weight 보간
```

**Evaluate 패스 (pose 계산, 자식 먼저 → 그 결과로 자기 연산):**

```
Root.Evaluate()
└─ Blend.Evaluate()
   ├─ SequencePlayer(walk).Evaluate()  -> poseA
   └─ IK.Evaluate()
      └─ SequencePlayer(run).Evaluate() -> (input)
      // IK: input pose의 일부 본 수정 -> poseB
   // Blend: blendPoses(poseA, poseB, weight) -> 최종 pose
```

핵심: Source node(잎)에서 pose가 "생성"되고, 트리를 거슬러 올라가며
Modifier가 "변형", Blend가 "합성"하여, Root에서 단 하나의 pose로 수렴한다.
State Machine node가 트리에 끼면, 그 지점에서 내부 sub-graph 트리로 한 단계 더 중첩된다.

### 4.2 수학 연산 관점 — 바닥 연산은 lerp / slerp

각 node가 pose에 적용하는 실제 수식.

**(a) Source node — 키프레임 보간**

시간 `t`를 감싸는 두 키프레임 `tA`, `tB` 사이에서:

```
alpha = (t - tA) / (tB - tA)
```

- 위치(translation), 스케일(scale): 선형 보간
  `lerp(A, B, alpha) = A + alpha * (B - A)`
- 회전(rotation): 쿼터니언 구면 보간 `slerp(qA, qB, alpha)`
  (회전은 단위 쿼터니언이라 단순 lerp는 각속도가 불균일 → slerp 사용)

**(b) Blend node — pose 합성**

두 pose를 weight `w`로 섞을 때, 본(bone)마다:

```
T_result = lerp(T0, T1, w)        // 위치/스케일
q_result = slerp(q0, q1, w)       // 회전
```

N개 pose의 다중 blend는 정규화된 가중치 `wi` (`sum(wi) = 1`)로
2-pose blend를 누적:

```
result = pose[0]
accumW = w[0]
for i in 1..N-1:
    accumW = accumW + w[i]
    t      = w[i] / accumW
    result = blendPoses(result, pose[i], t)
```

**(c) State Machine node — transition blend**

transition 진행도 `alpha = transitionTime / duration`을 weight로 하여
출발 state pose와 도착 state pose를 (b)와 동일한 방식으로 blend.
→ `blendPoses`를 Blend node와 공유한다.

**(d) Modifier node — 부분 수정**

IK 등은 보간이 아니라 입력 pose의 특정 본 transform만 목표값으로 교체/조정.
전체 pose 중 일부만 건드린다.

**(e) 로컬 → 컴포넌트 공간 변환** (graph 평가 이후 단계)

위 (a)~(d)는 모두 **로컬 공간(local space)** pose 연산이다.
최종 렌더링용 컴포넌트 공간 행렬은 본 계층을 루트부터 따라 누적:

```
M_child_component = M_parent_component * M_child_local
```

이 단계는 보통 AnimGraph 평가가 끝난 뒤 별도로 처리된다.

---

## 요약

- **Node** = 런타임에 pose를 생성/변형/합성하는 행위 단위. `Update`(status 갱신) + `Evaluate`(pose 계산) 두 패스로 동작. 대원칙: 결정은 Update, 실행은 Evaluate.
- **종류**는 입력 pose 개수로 갈림: Source(0) · Modifier(1) · Blend(2+) · Output(1) · StateMachine(내부 sub-graph).
- **State / Transition**: state는 instance가 가질 수 있는 상태. transition은 state를 잇는 *정적 연결선*(rule + duration)이며, 발동되면 *활성 transition*(동적 status)이 된다.
- **State Machine node**도 동일 인터페이스를 따르는 하나의 node — 특이한 건 "소유"가 아니라 "자식 선택이 동적"이라는 점(selectivity). Update에서 transition을 판정·진행해 모드를 결정하고, Evaluate는 그 모드를 읽어 안정이면 현재 state pose를, 전환 중이면 두 state pose를 alpha로 blend.
- **연산 관계**: Root에서 시작하는 후위 순회로 잎(Source)부터 pose가 생성·전파되며, 바닥 연산은 항상 이항 `lerp`/`slerp`.
- **Graph** = node들 + pose 연결. graph 자신은 합성 로직을 갖지 않는다 ("합성하는 node가 graph 안에 있다").
