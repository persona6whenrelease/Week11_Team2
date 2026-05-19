# C — AnimInstance 노출 / Root Graph 주입 통로 진단

> 진단 전용. 코드 변경 없음. 모든 file:line은 feature/graphnode 작업 트리를
> 직접 열어 확인. 미확인은 "확인 필요", 추측은 "추정"으로 표시.
> 상위 결정(재귀 트리 / generic 주입 / C 우선 / imgui-node-editor)은
> 전제이며 재논의하지 않는다.

## TL;DR — 통로 신설의 현재 성숙도와 핵심 미지수

- **이미 있는 것** — `AnimGraph::SetRoot`는 이미 `unique_ptr<FAnimGraphNode_Base>`를 받는 generic 시그니처([AnimGraph.h:109](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:109)). 베이스 `UAnimInstance`는 이미 `AnimGraphPtr` 슬롯과 `virtual EvaluateGraph()`를 보유([AnimInstance.h:43](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:43), [AnimInstance.h:91](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:91)). 베이스 평가 함수는 `AnimGraphPtr->Evaluate`로 그대로 위임([AnimInstance.cpp:87-108](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:87)).
- **비어 있는 것** — (1) generic 시그니처(`FAnimGraphNode_Base` 베이스 받음)의 주입 메서드. 현 유일 진입점 `SetStateMachineGraph`는 concrete `FAnimGraphNode_StateMachine` 한정([AnimStateMachineInstance.h:31](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h:31)). (2) `SkeletalMeshComponent`의 외부 → `AnimInstance` 통로(멤버가 `protected`이고 public 접근자 없음, [SkeletalMeshComponent.h:116](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:116)).
- **사람이 정해야 할 것** — Q1: 새 메서드를 베이스 `UAnimInstance`에 둘 것인지, 신규 파생 `UAnimGraphInstance`로 분리할 것인지. Q2: 컴포넌트 외부 통로를 `GetAnimInstance()` getter로 둘 것인지, `SetRootGraph(...)` 위임 메서드로 둘 것인지.

---

## 1. AnimGraphPtr lazy 진입점 위치        [Q1]

### 확인된 사실

- 베이스 `UAnimInstance`가 `AnimGraphPtr` 슬롯을 직접 보유 — [AnimInstance.h:91](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:91)
  ```
  std::unique_ptr<AnimGraph>  AnimGraphPtr;           // owned
  ```
  접근 수준은 `protected`([AnimInstance.h:73](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:73)). 베이스 ctor는 `= default`라 할당하지 않음 — [AnimInstance.cpp:11](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:11).
- 베이스에 read-only getter `GetAnimGraph()`는 이미 public — [AnimInstance.h:65](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:65). 단 `Source/` 트리에서 호출자 0건 (`grep -rn GetAnimGraph` 결과 그래프 진입점이 아닌 `Actor::GetRootComponent` 류만 매치, AnimInstance의 `GetAnimGraph`는 [AnimInstance.h:65](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:65)와 자기 자신 외 호출 없음).
- generic 주입에 필요한 lazy 패턴 `if (!AnimGraphPtr) AnimGraphPtr = make_unique<AnimGraph>(); AnimGraphPtr->SetRoot(move(InRoot));`은 현재 **`UAnimStateMachineInstance::SetStateMachineGraph` 한 곳에만 존재** — [AnimStateMachineInstance.cpp:11-36](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp:11). 이 메서드 시그니처는 concrete `FAnimGraphNode_StateMachine` 한정 — [AnimStateMachineInstance.h:31](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h:31).
- `UAnimSingleNodeInstance`는 베이스의 `AnimGraphPtr`을 **사용하지 않고 의도적으로 우회** — 값 멤버 `SequencePlayer`를 두고 `EvaluateGraph()`를 override해 직접 평가([AnimSingleNodeInstance.h:37](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.h:37), [AnimSingleNodeInstance.cpp:29-50](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:29)). cpp 주석에 "graph 트리를 우회해 단일 SequencePlayer를 직접 호출한다. base AnimGraphPtr은 미사용" 명시 — [AnimSingleNodeInstance.cpp:31](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:31).
- `UAnimStateMachineInstance`는 `EvaluateGraph`를 **override하지 않음** — 베이스 구현이 그대로 사용되어 `AnimGraphPtr->Evaluate`로 평가. `.h`/`.cpp` 어디에도 `EvaluateGraph` 선언/정의 없음([AnimStateMachineInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h), [AnimStateMachineInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp)).

### 후보 A — 베이스 `UAnimInstance`에 generic 메서드 추가

건드릴 파일/심볼:
- [AnimInstance.h:73-91](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:73) — 새 public 메서드 선언 (예: `SetRootGraph(std::unique_ptr<FAnimGraphNode_Base>)`).
- [AnimInstance.cpp:11-13](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:11) 부근 — 본문 정의. `if (!AnimGraphPtr) AnimGraphPtr = std::make_unique<AnimGraph>(); AnimGraphPtr->SetRoot(std::move(InRoot));`.

트레이드오프:
- (+) `AnimGraphPtr` 슬롯이 베이스에 이미 있으므로 메서드가 같은 캡슐 안에 위치. 자연스러움.
- (+) 모든 파생이 즉시 사용 가능. 클래스 등록 매크로 변경 0건(Q4 참조).
- (+) `UAnimStateMachineInstance::SetStateMachineGraph`(concrete 한정 시그니처)와 공존 가능 — 후자는 `SubLengthHint` 자동 도출이라는 부수효과([AnimStateMachineInstance.cpp:22-33](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp:22))를 가지므로 StateMachine 사용자는 기존 메서드를 계속 쓰면 됨.
- (−) `UAnimSingleNodeInstance`에도 메서드가 노출됨. 이 파생은 `AnimGraphPtr`을 우회하도록 설계됨([AnimSingleNodeInstance.cpp:31](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:31))이므로, 베이스 통로로 root를 넣어도 `EvaluateGraph()` override가 `SequencePlayer`만 보기 때문에 **주입한 그래프는 평가되지 않는다**. 호출자가 모드와 메서드를 잘못 매칭하면 silent dead injection이 됨. (확인 필요: 이 위험을 막을 가드를 베이스에 두는지 — 예: derived가 `bUsesAnimGraphPtr() == false`면 assert. 현재 그런 훅 없음.)
- (−) 베이스 인터페이스가 늘어남 — 모든 파생이 의미적으로 책임을 짊어진 것처럼 보임.

### 후보 B — 새 AnimInstance 파생(가칭 `UAnimGraphInstance`) 신설

건드릴 파일/심볼:
- 신규 `UAnimGraphInstance.h` / `UAnimGraphInstance.cpp` 한 쌍 — generic 그래프 전용 파생. `EvaluateGraph()` override 없음(베이스 구현이 그대로 `AnimGraphPtr->Evaluate` 호출).
- [SkeletalMeshComponent.h:14-18](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:14) — `EAnimationMode`에 새 case 추가(예: `AnimationGraph`).
- [SkeletalMeshComponent.cpp:98-116](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:98) — `EnsureAnimInstance` switch에 새 case 추가.
- 클래스 등록 매크로 — Q4 참조.

트레이드오프:
- (+) `UAnimSingleNodeInstance`에 generic 메서드가 새지 않음. 의미 분리가 강제됨.
- (+) `UAnimStateMachineInstance::SetStateMachineGraph`(concrete 한정 + `SubLengthHint` 도출 부수효과)와 명확히 구분됨 — 사용자는 모드로 의도를 표명.
- (−) 새 파생 한 쌍 추가 비용. 등록 매크로 결정 필요(Q4).
- (−) `EAnimationMode`/switch 분기가 늘어남. 외부에서 모드 enum까지 알아야 통로를 쓸 수 있음.
- (−) 모드 전환 시 `EnsureAnimInstance`의 single-shot 제약 때문에 PostEditProperty의 Destroy+재생성 경로([SkeletalMeshComponent.cpp:257-282](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:257))를 새 case도 따라야 함(Q3 참조).

---

## 2. SkeletalMeshComponent 외부 통로      [Q2]

### 확인된 사실

- `AnimInstance` 멤버는 `protected`, public getter/setter 없음 — [SkeletalMeshComponent.h:116](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:116)
  ```
  UAnimInstance *AnimInstance = nullptr;
  ```
  접근 수준은 [SkeletalMeshComponent.h:96](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:96)의 `protected:` 블록에 속함.
- 현재 컴포넌트가 `AnimInstance`를 외부에 노출하는 public 경로는 없음. 외부에서 직접 root graph를 주입할 수단 자체가 부재. (`SetAnimation` / `PlayAnimation` 등 시퀀스-한정 helper만 존재 — [SkeletalMeshComponent.h:32-37](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:32).)

### 후보 A — public getter 신설 (`GetAnimInstance()`)

건드릴 파일/심볼:
- [SkeletalMeshComponent.h:96](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:96) 이전 public 블록 — `UAnimInstance* GetAnimInstance() const { return AnimInstance; }` 한 줄.

트레이드오프:
- (+) 최소 노출. 한 줄.
- (+) 호출자가 `Cast<UAnimGraphInstance>(Comp->GetAnimInstance())->SetRootGraph(...)` 식으로 자유롭게 조작 가능. 새 AnimInstance 타입이 추가돼도 컴포넌트는 영향 없음.
- (−) 호출자가 `EnsureAnimInstance` 선행 호출 의무를 짊어짐 — `EnsureAnimInstance`가 `protected`([SkeletalMeshComponent.h:103](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:103))라 외부에서 트리거 불가. `nullptr` 반환 시 호출자가 대응 책임. (확인 필요: 외부가 트리거할 수 있는 다른 경로가 있는지 — 현재 `SetAnimation`, `PlayAnimation`, `EvaluateAnimationPose`, `PostEditProperty`가 내부적으로 `EnsureAnimInstance`를 호출하지만 모두 SingleNode 진입 시나리오임. [SkeletalMeshComponent.cpp:122,150,276,309](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:122).)
- (−) 캡슐화 약화. `AnimInstance`의 소유권/수명이 컴포넌트에 있다는 불변이 외부로 새기 쉬움.

### 후보 B — 위임 메서드 `SetRootGraph(...)`

건드릴 파일/심볼:
- [SkeletalMeshComponent.h:96](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:96) 이전 public 블록 — 시그니처 선언.
- [SkeletalMeshComponent.cpp](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp) — `EnsureAnimInstance()` 호출 후 cast하여 새 메서드(Q1) 호출하는 본문 추가.

트레이드오프:
- (+) `EnsureAnimInstance` 자동 트리거 가능 — 호출자는 nullptr 케이스를 신경 안 써도 됨.
- (+) `AnimInstance` 멤버는 여전히 protected. 캡슐화 유지.
- (+) Q1 후보 B(새 파생)와 결합하면 컴포넌트가 적절한 모드로 전환하는 책임까지 흡수 가능 (모드를 강제 swap + Destroy + 재생성).
- (−) 컴포넌트 인터페이스가 graph 개념(`FAnimGraphNode_Base`)을 직접 import해야 함 — [SkeletalMeshComponent.h:4](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:4)에 이미 `AnimInstance.h`를 include하지만 graph 노드 베이스는 추가 include 필요.
- (−) 새 AnimInstance 타입이 추가될 때마다 컴포넌트에 위임 메서드를 늘리거나, 한 메서드 안에서 분기해야 함.

---

## 3. EnsureAnimInstance / 모드 정합성     [Q3]

### 확인된 사실

- `EnsureAnimInstance`는 idempotent — [SkeletalMeshComponent.cpp:98-100](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:98)
  ```
  void USkeletalMeshComponent::EnsureAnimInstance()
  {
      if (AnimInstance) return;
      switch (AnimationMode) ...
  ```
  주석도 명시: "이미 생성됐으면 no-op. 런타임 모드 변경 후 자동 재생성은 미지원 — 별도 처리 필요." ([SkeletalMeshComponent.h:99-102](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:99))
- switch case는 `AnimationStateMachine` → `CreateObject<UAnimStateMachineInstance>(this)`, default(`AnimationSingleNode` 포함) → `CreateObject<UAnimSingleNodeInstance>(this)` — [SkeletalMeshComponent.cpp:101-110](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:101).
- 생성 직후 `InitializeAnimation(ResolveSkeletonFromMesh(SkeletalMesh))` + 재생 상태(Looping/Speed/Paused/EvaluationTime) 전파 — [SkeletalMeshComponent.cpp:111-115](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:111).
- 모드 변경 후 재생성은 `PostEditProperty` 안에서만 수동 처리됨: "Animation Mode" property 변경 시 `DestroyObject(AnimInstance) → AnimInstance = nullptr → EnsureAnimInstance()` — [SkeletalMeshComponent.cpp:261-281](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:261). 동일 destroy+null 패턴이 `PostDuplicate`에서도 사용 — [SkeletalMeshComponent.cpp:206-211](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:206).
- `SetAnimationMode` 자체는 enum만 갱신 — [SkeletalMeshComponent.h:39-46](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:39). 호출만으로는 AnimInstance 교체가 일어나지 않음.
- 비호출 시점에서 `AnimInstance`가 null로 남아 있으면 `TickComponent`는 즉시 return — [SkeletalMeshComponent.cpp:333-336](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:333).

### Q1 후보별 정합성

- **Q1-A (베이스 확장)**: 새 `EAnimationMode` case 불필요. 기존 모드(StateMachine) 안에서 generic 통로를 호출 가능 — 베이스에 메서드를 두기 때문에 `UAnimStateMachineInstance` 인스턴스든 다른 파생이든 호출 가능. 단, `UAnimSingleNodeInstance` 인스턴스에 호출하면 `EvaluateGraph` override가 `AnimGraphPtr`을 보지 않으므로 silent dead injection 위험(Q1-A의 (−) 항목 재확인).
- **Q1-B (새 파생)**: `EAnimationMode`에 새 case 추가 필수. `EnsureAnimInstance` switch에도 case 추가. PostEditProperty의 mode swap 경로([SkeletalMeshComponent.cpp:261-281](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:261))도 새 모드를 처리해야 함(예: graph 모드로 들어갈 때 `AnimToPlay = nullptr` 처리 등 — 현재 StateMachine 진입 분기와 유사 [SkeletalMeshComponent.cpp:273-277](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:273)).

### 호출 순서 의존성

- generic 통로(Q2 후보 어느 쪽이든)는 `AnimInstance`가 존재해야 의미가 있음. `AnimInstance == nullptr` 시점에 root graph만 들고 있어도 채울 곳이 없음.
- Q2-A(getter): 외부가 통로 호출 **이전에** `EnsureAnimInstance`-를-트리거하는 다른 API(예: `SetAnimation`, `PlayAnimation`, `EvaluateAnimationPose`)를 먼저 부르거나, 컴포넌트가 외부에 lazy-ensure 진입점을 신설해야 함. 현재 외부가 직접 `EnsureAnimInstance`를 부를 통로 0건([SkeletalMeshComponent.h:103](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:103)의 protected).
- Q2-B(위임 메서드): 메서드 본문에서 `EnsureAnimInstance()` 선호출 후 root 주입 — 순서 의존성을 메서드가 흡수.

---

## 4. 클래스 등록 매크로 정합성            [Q4]

### 매크로 정의처

- `REGISTER_FACTORY(TypeName)` — [ObjectFactory.h:7-19](KraftonEngine/Source/Engine/Object/ObjectFactory.h:7)
  TU 시작 시 익명 namespace 전역 객체로 `FObjectFactory::Get().Register(#TypeName, lambda)`를 실행. 람다는 `UObjectManager::Get().CreateObject<TypeName>(InOuter)`를 호출. 즉 **문자열 이름 → 인스턴스** 경로를 등록.
- `IMPLEMENT_CLASS(ClassName, ParentClass)` — [ObjectFactory.h:21-23](KraftonEngine/Source/Engine/Object/ObjectFactory.h:21)
  ```
  #define IMPLEMENT_CLASS(ClassName, ParentClass)                        \
      DEFINE_CLASS(ClassName, ParentClass)                               \
      REGISTER_FACTORY(ClassName)
  ```
  즉 **RTTI 정의(StaticClassInstance) + 등록 람다** 한 묶음.
- `DECLARE_CLASS(ClassName, ParentClass)` — [Object.h:15-20](KraftonEngine/Source/Engine/Object/Object.h:15). 헤더에서 `Super` typedef + `static UClass StaticClassInstance` 선언 + `static FClassRegistrar s_Registrar` + `StaticClass()` / `GetClass()` 정의.
- `DEFINE_CLASS(ClassName, ParentClass)` — [Object.h:22-32](KraftonEngine/Source/Engine/Object/Object.h:22). cpp에서 `StaticClassInstance` 초기화 + registrar 인스턴스화.
- 자동 codegen 경로: `UCLASS()` + `GENERATED_BODY()` 매크로 — 예: [AnimInstance.h:21-25](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:21), [AnimInstance.generated.h:11-17](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.generated.h:11), [AnimInstance.generated.cpp:10-16](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.generated.cpp:10). `Scripts/GenerateReflectionHeaders.py`가 `DECLARE_CLASS` 등가 본문을 `.generated.h`에 생성하고 `.generated.cpp`에 `DEFINE_CLASS` 등가(StaticClassInstance/registrar)를 생성. cpp에는 별도로 `REGISTER_FACTORY(TypeName)`만 손으로 호출하면 됨.

### `UObjectManager::CreateObject<T>`의 의존성

- 정의: [Object.h:121-134](KraftonEngine/Source/Engine/Object/Object.h:121)
  ```
  template<typename T>
  T* CreateObject(UObject* InOuter = nullptr)
  {
      static_assert(std::is_base_of<UObject, T>::value, "T must derive from UObject");
      T* Obj = new T();
      Obj->SetOuter(InOuter);
      const char* ClassName = T::StaticClass()->GetName();
      ...
      return Obj;
  }
  ```
- 의존성: **`T::StaticClass()`만 필요**. `FObjectFactory::Register`/`Create`(문자열 경로)에 의존하지 않음. 즉 **`REGISTER_FACTORY` 없이도 `CreateObject<T>`는 동작**.
  - `FObjectFactory::Create(const std::string&, UObject*)` ([ObjectFactory.h:46-49](KraftonEngine/Source/Engine/Object/ObjectFactory.h:46))는 별도 경로. 문자열 이름으로 생성하는 경우(예: 직렬화에서 type name 복원)에만 `REGISTER_FACTORY`가 필수.

### AnimInstance 계열 등록 분포

| 클래스 | RTTI 출처 | Factory 등록 |
|---|---|---|
| `UAnimInstance` | `UCLASS()+GENERATED_BODY()` codegen — [AnimInstance.h:21-25](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:21), [AnimInstance.generated.cpp:10-16](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.generated.cpp:10) | `REGISTER_FACTORY(UAnimInstance)` — [AnimInstance.cpp:9](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:9) |
| `UAnimSingleNodeInstance` | `UCLASS()+GENERATED_BODY()` codegen — [AnimSingleNodeInstance.h:16-20](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.h:16) | `REGISTER_FACTORY(UAnimSingleNodeInstance)` — [AnimSingleNodeInstance.cpp:8](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:8) |
| `UAnimStateMachineInstance` | 수동 `DECLARE_CLASS` — [AnimStateMachineInstance.h:21](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h:21) | `IMPLEMENT_CLASS(UAnimStateMachineInstance, UAnimInstance)` = `DEFINE_CLASS` + `REGISTER_FACTORY` — [AnimStateMachineInstance.cpp:7](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp:7). 동 클래스는 `.generated.h`/`.generated.cpp` 짝이 없음(grep 0건).

따라서 두 매크로의 **차이는 형식상**: `UCLASS+GENERATED_BODY` 경로는 codegen이 `DEFINE_CLASS` 등가를 자동 생성하므로 cpp에서 `REGISTER_FACTORY`만 손으로 부르면 충분. 수동 `DECLARE_CLASS` 경로는 cpp에서 `IMPLEMENT_CLASS`(=`DEFINE_CLASS`+`REGISTER_FACTORY`) 한 줄로 두 책임을 모두 처리. 최종 산출물(`StaticClassInstance` + factory lambda 등록)은 동일.

### Q1 후보별 비용

- **Q1-A (베이스 확장)**: 등록 매크로 변경 0건. 베이스 `UAnimInstance`는 이미 RTTI/factory 등록 완료 — 새 메서드만 추가하면 됨.
- **Q1-B (새 파생 `UAnimGraphInstance`)**: 새 파생을 등록해야 함. 두 옵션 모두 가능:
  - `UCLASS()+GENERATED_BODY()` + `REGISTER_FACTORY` — codegen 산출물 동반(스크립트 재실행 필요). 다른 AnimInstance류와 형식 일치.
  - 수동 `DECLARE_CLASS` + `IMPLEMENT_CLASS` — codegen 없이 1파일 헤더로 끝. `UAnimStateMachineInstance` 형식과 일치.
  - 어느 쪽이든 `CreateObject<UAnimGraphInstance>(this)` 호출은 **`StaticClass()`만 있으면 동작**하므로(위 의존성 분석) factory lambda는 문자열 경로를 쓸 때만 필요. C 단계에서 문자열 경로 사용 예정 없음 → `REGISTER_FACTORY` 없이도 C는 닫힘(확인 필요: 추후 A 단계에서 직렬화 type name 복원에 필요해질 수 있음 — 범위 외).
- **결론**: Q4는 Q1 후보 선택의 **결정적 비용 요인이 아님**. 두 후보 모두 등록 매크로 측면에서는 처리 가능.

---

## 5. 평가 경로 검증                       [Q5]

### Tick 경로 (file:line)

- `USkeletalMeshComponent::TickComponent` — [SkeletalMeshComponent.cpp:330-350](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:330)
  ```
  if (!AnimInstance) return;
  AnimInstance->Update(DeltaTime);
  AnimInstance->EvaluateGraph();
  ApplyEvaluatedPose(AnimInstance->GetOutputLocalPose());
  BakedAnimTime = AnimInstance->GetCurrentTime();
  ```
- 외부 강제 평가 경로 `RefreshAnimationPose` — [SkeletalMeshComponent.cpp:352-365](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:352). 동일하게 `AnimInstance->EvaluateGraph()` → `ApplyEvaluatedPose(AnimInstance->GetOutputLocalPose())`.
- `ApplyEvaluatedPose`는 부모 `USkinnedMeshComponent`에 정의 — [SkinnedMeshComponent.h:84](KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.h:84), [SkinnedMeshComponent.cpp:691](KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.cpp:691). 평가 결과(`TArray<FTransform>`)를 skinning matrix로 반영.

### 베이스 `EvaluateGraph`가 AnimGraphPtr을 통해 평가하는가

- 베이스 본문 — [AnimInstance.cpp:87-108](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:87)
  ```
  void UAnimInstance::EvaluateGraph()
  {
      if (!Skeleton || !AnimGraphPtr) { OutputLocalPose.clear(); return; }
      ... // Ctx 세팅
      AnimGraphPtr->Evaluate(Ctx, OutputLocalPose);
  }
  ```
  `AnimGraphPtr->Evaluate`는 [AnimGraph.cpp:13-21](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp:13)에서 `Root->Evaluate(Ctx, OutLocalPose)` 가상 디스패치(또는 Root null 시 bind pose).
- `EvaluateGraph`는 `virtual` — [AnimInstance.h:43](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:43).

### 파생별 override 상태

- `UAnimSingleNodeInstance::EvaluateGraph()` **override됨** — [AnimSingleNodeInstance.h:29](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.h:29), [AnimSingleNodeInstance.cpp:29-68](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:29). 본문은 `SequencePlayer.Evaluate(Ctx, OutputLocalPose)`만 호출 — **`AnimGraphPtr`을 보지 않음**.
- `UAnimStateMachineInstance::EvaluateGraph()` **override 없음** — [AnimStateMachineInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h), [AnimStateMachineInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp) 모두 선언/정의 0건. 베이스 구현이 그대로 사용 → `AnimGraphPtr->Evaluate` 호출됨.

### Q1 후보별 평가 회로 정합성

- **Q1-A (베이스 확장)**: generic 통로로 `AnimGraphPtr`에 root를 주입한다 해도, 실제 평가가 그것을 보는지는 **인스턴스의 동적 타입에 따라 다름**.
  - `UAnimStateMachineInstance` 인스턴스 → override 없음 → 베이스 `EvaluateGraph` → `AnimGraphPtr->Evaluate` → 주입한 root 평가됨. ✅
  - `UAnimSingleNodeInstance` 인스턴스 → override가 `SequencePlayer`만 봄 → **주입한 root는 절대 평가되지 않음**. ❌ silent dead injection.
  - `UAnimInstance` 베이스 인스턴스 자체 → 베이스 `EvaluateGraph` → 평가됨. (그러나 `EnsureAnimInstance` switch는 베이스 인스턴스를 만들지 않음 — [SkeletalMeshComponent.cpp:101-110](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:101). 베이스 인스턴스가 컴포넌트에 붙는 경로는 현재 없음.)
- **Q1-B (새 파생 `UAnimGraphInstance`)**: 새 파생이 `EvaluateGraph`를 **override하지 않으면** 베이스 구현이 그대로 `AnimGraphPtr->Evaluate`로 평가 — `UAnimStateMachineInstance`와 동일 패턴. 가장 단순한 형태이며 회로가 명시적으로 닫힘. ✅
- 즉, Q1-A를 택하면 "generic 메서드는 베이스에 있지만 실제 평가 성공 여부는 파생에 따라 다르다"는 미묘함이 남음. Q1-B는 그런 케이스 분기 없음(전용 파생만 사용).

---

## 6. 소유권 / 수명                        [Q6]

### 소유 사슬

1. `unique_ptr<FAnimGraphNode_Base>` root → `AnimGraph::Root`로 move-in — [AnimGraph.h:109](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:109). `AnimGraph::Root`는 `std::unique_ptr<FAnimGraphNode_Base>` — [AnimGraph.h:115](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:115).
2. `AnimGraph` 인스턴스 → `UAnimInstance::AnimGraphPtr`이 `std::unique_ptr<AnimGraph>`로 소유 — [AnimInstance.h:91](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:91).
3. `UAnimInstance*` → `USkeletalMeshComponent::AnimInstance`(raw pointer)가 보유 — [SkeletalMeshComponent.h:116](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:116). 컴포넌트가 생성 책임을 갖고(`CreateObject` 후 멤버에 저장) 파괴 책임도 가짐.
4. 컴포넌트 dtor에서 `DestroyObject` 호출 — [SkeletalMeshComponent.cpp:89-96](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:89)
   ```
   USkeletalMeshComponent::~USkeletalMeshComponent()
   {
       if (AnimInstance) { UObjectManager::Get().DestroyObject(AnimInstance); AnimInstance = nullptr; }
   }
   ```
   `UObjectManager::DestroyObject` 본문은 단순 `delete Obj` — [Object.h:136-143](KraftonEngine/Source/Engine/Object/Object.h:136).

### 해제 순서

- `delete AnimInstance` → `UAnimInstance::~UAnimInstance()` (`= default`, [AnimInstance.cpp:12](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:12)) → 베이스 멤버 `AnimGraphPtr` 자동 해제 (`unique_ptr<AnimGraph>` dtor) → `AnimGraph::~AnimGraph()`(컴파일러 합성) → 베이스 `Root`(`unique_ptr<FAnimGraphNode_Base>`) 자동 해제 → 노드의 `virtual ~FAnimGraphNode_Base()`([AnimGraph.h:40](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:40)) 가상 dtor 호출 → 노드가 보유한 자식 `unique_ptr` 트리(예: `FAnimGraphNode_Blend2::ChildA/ChildB`, `FAnimGraphNode_BlendN::Children`)도 재귀 해제. 누수/이중 해제 없음.

### 외부 자산 ref(non-owning)

- `FAnimGraphNode_SequencePlayer::Sequence`, `DataModel` — non-owning raw pointer ("ref, not owned" 주석, [AnimGraph.h:60-61](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:60)). 소유 사슬에 포함되지 않음 — 노드 dtor가 외부 자산을 해제하지 않음.
- `UAnimInstance::Skeleton` — non-owning("ref, not owned", [AnimInstance.h:90](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:90)).
- 즉 컴포넌트가 파괴돼도 외부 자산(`USkeleton`, `UAnimSequence`)은 그대로 살아 있음. `UObjectManager`/`GUObjectArray` 측 수명 관리는 자산 별도 책임 — 범위 외.

### 모드 swap 경로의 수명

- `PostEditProperty`([SkeletalMeshComponent.cpp:261-281](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:261)) 또는 `PostDuplicate`([SkeletalMeshComponent.cpp:206-211](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:206))가 `DestroyObject(AnimInstance); AnimInstance = nullptr; EnsureAnimInstance();`를 호출하면 기존 AnimInstance와 그 안의 AnimGraph/Root 트리는 모두 해제됨. 새 인스턴스에는 root가 비어 있으므로, generic 통로 호출자가 이 시점 이후 다시 root를 주입해야 함(silent — UI 상태로는 표 안 남음). (확인 필요: Q1-B 새 파생을 택할 경우, 이 swap 경로가 graph 모드 진입에서 root 재주입을 강제하는 가드를 갖춰야 하는지는 정책 결정 사항.)
- 이중 해제 위험: `DestroyObject` 후 즉시 `AnimInstance = nullptr`로 클리어 — [SkeletalMeshComponent.cpp:94, 210, 266](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:94). 이후 dtor가 다시 호출돼도 `if (AnimInstance)` 가드로 stop. 안전.

---

## 설계 선택지 요약

> 구현하지 않음. Q1(베이스 확장 vs 새 파생)과 Q2(getter vs 위임)의
> 조합별 트레이드오프를 표로. 어느 조합으로 단정하지 말 것.

| 조합 | Q1 (메서드 위치) | Q2 (컴포넌트 통로) | 핵심 장점 | 핵심 위험 |
|---|---|---|---|---|
| (A,A) | 베이스 `UAnimInstance` | `GetAnimInstance()` getter | 변경 최소(헤더 두 줄). 기존 모드/매크로 손대지 않음. | SingleNode 인스턴스에 주입 시 silent dead injection (Q5). `EnsureAnimInstance` 외부 트리거 부재(Q3). |
| (A,B) | 베이스 `UAnimInstance` | `SetRootGraph` 위임 | 외부는 컴포넌트만 보면 됨. `EnsureAnimInstance` 자동 트리거 가능. | SingleNode 인스턴스에 대고 호출하는 silent dead injection 가능성은 동일(Q5). 컴포넌트가 모드/타입 가드 책임을 가지면 복잡도↑. |
| (B,A) | 새 파생 `UAnimGraphInstance` | `GetAnimInstance()` getter | 평가 회로 명확(Q5). SingleNode와의 의미 분리 강제. | `EAnimationMode`/switch/PostEditProperty mode swap 확장 필요(Q3). 외부가 모드 enum까지 알아야 함. `EnsureAnimInstance` 외부 트리거 부재(Q3). |
| (B,B) | 새 파생 `UAnimGraphInstance` | `SetRootGraph` 위임 | 평가 회로 명확. 외부는 컴포넌트의 단일 진입점만 알면 됨. 컴포넌트가 모드 swap까지 흡수 가능. | 추가 파일/매크로/모드 case 등 변경 면적 최대. |

추가 관찰:
- Q5 분석에 따르면 **Q1-A는 "메서드는 통과시키지만 실제 평가는 인스턴스 타입에 따라 다르다"는 미묘함**을 안고 가야 함. Q1-B는 그 미묘함이 없음.
- Q4 분석에 따르면 **두 후보 모두 등록 매크로 처리 가능** — 비용 차이는 미미.
- Q2는 Q1 선택과 독립적으로 평가 가능하지만, Q1-B를 택했을 때 Q2-B가 컴포넌트 인터페이스를 한 점으로 좁혀 사용 편의가 가장 좋음(주관 — 사람이 정함).

---

## C 구현 시 건드릴 파일 목록 (예상)

> 선택된 조합과 무관하게 공통으로 건드릴 파일 + 조합별로 갈리는 파일을 구분해 나열. file 단위까지만, 코드는 쓰지 말 것.

### 공통 (모든 조합)

- [Source/Engine/Asset/Animation/Core/AnimGraph.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h) — 변경 가능성 낮음(`SetRoot`는 이미 generic). include 추가 정도.
- [Source/Engine/Component/SkeletalMeshComponent.h](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h) — Q2 통로 선언 위치.
- [Source/Engine/Component/SkeletalMeshComponent.cpp](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp) — Q2 통로 본문 위치.

### Q1-A (베이스 확장)에서만 갈리는 파일

- [Source/Engine/Asset/Animation/Core/AnimInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h) — 새 public 메서드 선언.
- [Source/Engine/Asset/Animation/Core/AnimInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) — 본문 정의.

### Q1-B (새 파생)에서만 갈리는 파일

- 신규 `Source/Engine/Asset/Animation/Core/AnimGraphInstance.h` / `.cpp` (가칭). UCLASS+GENERATED_BODY 형식이면 `.generated.h`/`.generated.cpp`도 codegen 산출됨.
- [Source/Engine/Component/SkeletalMeshComponent.h](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h) — `EAnimationMode`에 새 case 추가.
- [Source/Engine/Component/SkeletalMeshComponent.cpp](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp) — `EnsureAnimInstance` switch, `PostEditProperty` mode swap 분기, `GAnimationModeNames` 배열([SkeletalMeshComponent.cpp:23-26](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:23))에 새 표시명.

### Q2-A (getter)와 Q2-B (위임)의 차이

- Q2-A: SkeletalMeshComponent.h에 한 줄 inline getter.
- Q2-B: SkeletalMeshComponent.h에 선언 + SkeletalMeshComponent.cpp에 본문(EnsureAnimInstance 호출 + cast + Q1 메서드 호출).

---

## 확인 필요 / 정책 결정 항목

특히 다음은 사람이 정해야 함:

1. **Q1 후보 선택**: 베이스 확장 vs 새 파생. 평가 회로 명확성(Q1-B 우위)과 변경 면적(Q1-A 우위)의 트레이드오프.
2. **Q2 후보 선택**: getter vs 위임. 캡슐화/사용 편의 트레이드오프. Q1-B를 택한 경우 Q2-B가 사용 편의 측면에서 유리(주관).
3. **Q1-A 채택 시 silent dead injection 가드 정책**: SingleNode 인스턴스에 generic 메서드를 호출했을 때 — assert로 차단할지, 디버그 로그만 남길지, 무가드로 둘지.
4. **Q1-B 채택 시 등록 매크로 형식**: `UCLASS+GENERATED_BODY`(codegen 재실행 필요) vs 수동 `DECLARE_CLASS+IMPLEMENT_CLASS`(`UAnimStateMachineInstance` 형식 일치). 기능 차이 없음 — 일관성 정책 결정.
5. **Q1-B 채택 시 mode swap UX**: `PostEditProperty`에서 graph 모드로 전환할 때 root가 비어 있는 상태로 들어가는 것을 허용할지(현 StateMachine 진입과 동일 패턴, [SkeletalMeshComponent.cpp:273-277](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:273)). UX 정책.
6. **외부의 `EnsureAnimInstance` 트리거 통로**: Q2-A를 택할 경우 외부가 `AnimInstance`를 nullptr 아닌 상태로 끌어올릴 방법이 필요. 새 public lazy-ensure를 신설할지, Q2-B로 흡수할지.
7. **C 단계 검증 픽스처 위치**: "코드로 `make_unique` 조립한 그래프를 컴포넌트에 주입해 평가가 닫힘"을 어디서(테스트 / 임시 콘솔 커맨드 / SkeletalEditor 임시 UI 등) 확인할지. (확인 필요 — 본 진단의 범위는 검증 픽스처 설계가 아니라 통로 설계까지.)

---

## 범위 외 관찰 사항 (기록만, 진단하지 않음)

- `AnimGraph`/노드의 `Serialize` 구현은 부재 — A 단계 몫.
- `.asset`/`EAssetType` 확장 부재 — A 단계 몫.
- imgui-node-editor 연동 부재 — B 단계 몫.
- 노드 타입 식별 enum / factory 설계 부재 — A 단계 몫.
- `UAnimStateMachineInstance::SetStateMachineGraph`의 `SubLengthHint` 자동 도출 부수효과([AnimStateMachineInstance.cpp:22-33](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp:22))는 generic 통로로 옮기면 사라짐 — 사용자가 직접 set하거나 별도 helper로 분리해야 함(정책 결정 사항, 본 진단에서는 답하지 않음).
- `UAnimInstance::GetAnimGraph()` getter는 정의돼 있으나 `Source/` 트리에서 호출자 0건(grep) — 미사용 read-only 진입점.
- `UAnimStateMachineInstance::SetStateMachineGraph`도 `Source/` 트리에서 호출자 0건(grep) — 메서드 본문만 존재. C 단계 통로가 첫 호출자가 될 수 있음.
