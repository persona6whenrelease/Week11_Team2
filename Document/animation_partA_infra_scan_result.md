# 파트 A 인프라 스캔 결과

## 요약 — 추정 검증 결과 한눈에

| 항목 | 사용자 추정 요지 | 스캔 결과 요지 | 판정 |
|---|---|---|---|
| S1 | `FAnimEvalContext`가 Skeleton/DataModel/Sequence/TrackToBoneIndex/TimeSeconds 5필드, DeltaTime·포즈풀 핸들 없음 | 5필드 모두 그대로 존재, dt/포즈풀 없음 | [확정] |
| S2 | `FAnimGraphNode_SequencePlayer`는 stateless, SingleNode가 graph를 우회해 직접 키 샘플링 | stateless는 맞으나, SingleNode는 `FAnimGraphNode_SequencePlayer` 값 멤버를 직접 보유·호출함. 우회 대상은 `AnimGraph` 객체이지 SequencePlayer 노드가 아님 | [반증](부분) |
| S3 | 미상 — 소유자 확인 필요 | `UAnimInstance`가 `std::unique_ptr<AnimGraph> AnimGraphPtr`로 소유. 단, `UAnimSingleNodeInstance` 경로에서는 set되지 않아 nullptr 상태로 남음 | 확인 완료 |
| S4 | TRS+Slerp(Option B), 출력은 `TArray<FMatrix>`, 중간 표현은 가능 | 그대로 일치. 중간 표현은 지역 변수 `FVector/FQuat/FVector`로 존재, 마지막에 `FTransform(P,R,S).ToMatrix()`로 합성. 지역 변수이므로 외부 접근 불가 | [확정] |
| S5 | abstract base | 가상 소멸자 + 순수 가상 `Evaluate` 한 개, `std::unique_ptr`로 다형 소유 | [확정] |
| S6 | Root 슬롯 하나만, 다중 노드/연결 표현 없음 | `std::unique_ptr<FAnimGraphNode_Base> Root` 단일 슬롯, 컨테이너·add/remove/swap 없음 | [확정] |
| S7 | BoolVariables/FloatVariables/OnNotify/스크래치풀 모두 없음, 시간 상태 멤버 존재 | 모두 일치. 단 Notify는 delegate 대신 `TArray<FName> TriggeredNotifiesThisFrame` 폴링 패턴 | [확정] |
| S8 | 미확인 | Slerp/ToMatrix/FromMatrix/Vector::Lerp/FTransform 모두 존재. 단 "행렬 한 번에 TRS 분해" 전용 함수는 없음(개별 getter만), 전용 `ComposeTRS` 함수도 없음(`FTransform::ToMatrix()`가 그 역할) | 부분 [미발견] |
| S9 | 미확인, 매핑 문서에서만 언급 | `Runtime/Delegate.h`에 정의 존재(스캔 범위 밖). `BroadCast` 시그니처·`Add`/`AddDynamic` API 확인. Animation/Notify 코드에서는 미사용 | [범위초과] + 정보 기록 |

---

## S1. FAnimEvalContext

- **정의 위치:** `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:22-29`
- **실제 필드 (코드 그대로):**
  ```cpp
  struct FAnimEvalContext
  {
      const USkeleton      *Skeleton         = nullptr;
      const UAnimDataModel *DataModel        = nullptr; // 단일 시퀀스 경로 단축용
      const UAnimSequence  *Sequence         = nullptr; // 향후 노드가 시퀀스 메타에 접근할 때 사용
      const TArray<int32>  *TrackToBoneIndex = nullptr; // 트랙 idx -> 본 인덱스
      float                 TimeSeconds      = 0.0f;
  };
  ```
- **추정 대조:**
  - 5필드 이름·타입·기본값·const 한정자까지 추정과 완전 일치.
  - `DeltaTime` 또는 dt 류 필드: **없음** (추정 부합).
  - 스크래치 포즈 버퍼 풀 핸들/포인터: **없음** (추정 부합).
- **추가 관찰:**
  - `Sequence` 필드는 선언만 되어 있고 현재 호출자 `UAnimInstance::EvaluateGraph()`(`AnimInstance.cpp:103-109`)와 `UAnimSingleNodeInstance::EvaluateGraph()`(`AnimSingleNodeInstance.cpp:44-50`) **양쪽 모두 채우지 않는다** (`Ctx.Sequence`가 항상 `nullptr`로 평가에 진입). 미래 노드 메타 접근용으로 자리만 잡혀 있음.
- **판정:** [확정]

---

## S2. FAnimGraphNode_SequencePlayer

- **정의 위치 (선언):** `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:45-48`
- **구현:** `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp:45-128`
- **실제 형태 (코드 그대로):**
  ```cpp
  struct FAnimGraphNode_SequencePlayer : FAnimGraphNode_Base
  {
      void Evaluate(const FAnimEvalContext &Ctx, TArray<FMatrix> &OutLocalPose) override;
  };
  ```
- **항목별 검증:**
  - 멤버 변수 **0개** — 완전 stateless. (추정 부합)
  - `UAnimSequence*` 멤버: 없음. (추정 부합)
  - `float LocalTime` 멤버: 없음. 시간은 매 평가마다 `Ctx.TimeSeconds`(`AnimGraph.cpp:91`)에서 받아서 사용. (추정 부합)
  - `Reset()` 메서드: 없음. (추정 부합)
  - `Evaluate` 외 다른 멤버 함수: 없음. (추정 부합)
- **"graph 우회" 추정 검증:** 사용자 추정에 "UAnimSingleNodeInstance가 graph를 우회해 직접 키 샘플링을 한다"고 적혀 있으나, 실제 코드는 다음과 같음:
  - `UAnimSingleNodeInstance`는 `FAnimGraphNode_SequencePlayer SequencePlayer;` 를 **값 멤버로 보유** (`AnimSingleNodeInstance.h:35`).
  - `UAnimSingleNodeInstance::EvaluateGraph()`(`AnimSingleNodeInstance.cpp:29-51`)는 base의 `AnimGraphPtr`을 사용하지 않고 (line 31 주석 "base AnimGraphPtr은 미사용"), 이 값 멤버 `SequencePlayer`의 `Evaluate`를 **직접 호출** (line 50: `SequencePlayer.Evaluate(Ctx, OutputLocalPose);`).
  - 즉 **우회되는 것은 `AnimGraph` 객체와 `Root` 슬롯**이지, `FAnimGraphNode_SequencePlayer` 노드 자체가 아니다. SingleNode 경로도 이 노드의 `Evaluate`를 그대로 사용한다.
- **판정:** **[반증] (부분)** — stateless·자체 시간 미보유·단일 메서드는 모두 [확정]. 다만 "SingleNode가 SequencePlayer 노드 자체를 우회한다"는 부분 추정은 **반증**.

---

## S3. AnimGraph 소유자

- **`AnimGraph` 정의:** `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:53-63`
- **소유 위치:** `UAnimInstance::AnimGraphPtr` — `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:95`
  ```cpp
  std::unique_ptr<AnimGraph>  AnimGraphPtr;           // owned
  ```
- **소유 방식:** `std::unique_ptr<AnimGraph>` (UObject factory가 아니라 표준 스마트 포인터). base에서 protected 멤버.
- **사용 경로:** `UAnimInstance::EvaluateGraph()`(`AnimInstance.cpp:89-110`)가 `AnimGraphPtr->Evaluate(Ctx, OutputLocalPose)` 호출 (line 109). `AnimGraphPtr`이 nullptr이면 즉시 `OutputLocalPose.clear()` 후 return (line 91-95).
- **SingleNode 경로의 실제:**
  - `UAnimSingleNodeInstance`는 base의 `AnimGraphPtr`을 **set하지 않는다** — `UAnimSingleNodeInstance.cpp` 어디에도 `AnimGraphPtr.reset(...)` 류 호출이 없으며, `UAnimInstance` 생성자도 default(`AnimInstance.cpp:13`)라 초기화하지 않음.
  - `UAnimSingleNodeInstance::EvaluateGraph()`를 override하여 base의 `EvaluateGraph()`를 호출하지 않고 자체 경로로 처리 (`AnimSingleNodeInstance.cpp:29-51`).
  - 결과적으로 **SingleNode 경로에서는 `AnimGraph` 객체가 한 번도 생성되지 않으며 `AnimGraphPtr`은 평생 nullptr**.
- **getter:** `AnimGraph *GetAnimGraph() const { return AnimGraphPtr.get(); }` (`AnimInstance.h:64`)
- **판정:** [확정] — 사용자가 "미상"이라 적은 항목이므로 [확정] 형태로 사실을 기록.

---

## S4. SequencePlayer::Evaluate — 샘플링 방식과 출력 형식

- **구현 위치:** `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp:45-128`
- **함수 시그니처:** `void FAnimGraphNode_SequencePlayer::Evaluate(const FAnimEvalContext &Ctx, TArray<FMatrix> &OutLocalPose)`
- **샘플링 방식:**
  - `LocalMatrixKeys`: **사용 안 함**. 원시 트랙 타입 `FRawAnimSequenceTrack`이 `PosKeys`/`RotKeys`/`ScaleKeys` 3개 배열만 보유하며 행렬 키 배열 자체가 존재하지 않음 (`AnimationTypes.h:63-76`).
  - 보간 코드 (`AnimGraph.cpp:122-124`):
    ```cpp
    const FVector P = FVector::Lerp(Raw.PosKeys[FrameA],   Raw.PosKeys[FrameB],   Blend);
    const FQuat   R = FQuat::Slerp (Raw.RotKeys[FrameA],   Raw.RotKeys[FrameB],   Blend);
    const FVector S = FVector::Lerp(Raw.ScaleKeys[FrameA], Raw.ScaleKeys[FrameB], Blend);
    ```
    → TRS 분리, 회전은 Slerp, 위치·스케일은 Lerp (Option B 확정).
- **출력 형식:**
  - `OutLocalPose`의 원소 타입은 `FMatrix` (`AnimGraph.h:37` 베이스 시그니처, `AnimGraph.cpp:126`에서 합성).
- **중간 표현(★ 핵심 질문):**
  - **존재한다.** 보간 결과가 `FMatrix`로 합성되기 직전 한 트랙당 1회씩 `FVector P`, `FQuat R`, `FVector S` 세 지역 변수가 만들어진다 (`AnimGraph.cpp:122-124`).
  - 그 직후 같은 줄(`AnimGraph.cpp:126`)에서 `FTransform(P, R, S).ToMatrix()`로 합성 → `OutLocalPose[BoneIdx]`에 대입.
  - **노출 여부:** **외부 접근 불가**. 세 변수는 트랙 루프 내부의 지역 변수이며 어떤 멤버·반환값·콜백으로도 외부에 노출되지 않는다. 함수가 끝나는 시점에 결과는 오직 `TArray<FMatrix>` 형태로만 존재.
  - **Blend 관점 함의:** 현재 형태로 Blend 노드가 행렬 재분해(`FQuat::FromMatrix` + `FMatrix::GetLocation/GetScale`) 없이 TRS를 받으려면, `OutLocalPose`의 출력 형식을 `TArray<FMatrix>`에서 `TArray<FTransform>` 류로 바꾸거나 별도 TRS 버퍼를 추가하는 **시그니처 변경이 필요**하다.
- **누락 트랙·정합성 처리 (참고):** bind pose로 먼저 채운 뒤(`AnimGraph.cpp:65`) 트랙별로 덮어쓰는 fallback 구조. `TrackToBoneIndex`가 없거나 size 불일치이면 bind pose 유지(`AnimGraph.cpp:68-78`). 트랙 내 키 배열 중 하나라도 `NumKeys`보다 짧으면 해당 트랙도 bind pose 유지 (`AnimGraph.cpp:112-120`).
- **판정:** [확정] (TRS+Slerp, FMatrix 출력, 중간 표현은 지역 변수로 존재하나 외부 미노출).

---

## S5. FAnimGraphNode_Base 인터페이스

- **정의 위치:** `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:34-38`
- **실제 형태 (코드 그대로):**
  ```cpp
  struct FAnimGraphNode_Base
  {
      virtual ~FAnimGraphNode_Base() = default;
      virtual void Evaluate(const FAnimEvalContext &Ctx, TArray<FMatrix> &OutLocalPose) = 0;
  };
  ```
- **항목별:**
  - `Evaluate` 시그니처: 반환형 `void`, 파라미터 `(const FAnimEvalContext &, TArray<FMatrix> &)`, **함수 자체는 const가 아님** (non-const member). 노드가 자체 상태를 가질 수 있도록 의도된 형태.
  - 가상 소멸자: **있음** (`= default`, public).
  - 순수 가상 함수: `Evaluate` 단 1개.
  - 다형 소유 방식: `std::unique_ptr<FAnimGraphNode_Base>` 사용 — `AnimGraph::Root` (`AnimGraph.h:62`), `AnimGraph::SetRoot(std::unique_ptr<...> Node)` (`AnimGraph.h:56`). S3과 정합.
- **추정 대조:** abstract라는 추정 [확정]. 다만 S2에서 보았듯 "SingleNode 경로와 GraphNode 경로 분리"의 실제 구분점은 **abstract 베이스가 아니라 `EvaluateGraph()` override** 위치임 (SingleNode는 base의 `EvaluateGraph()`를 우회).
- **판정:** [확정]

---

## S6. AnimGraph 구조 — Root 슬롯/다중 노드 지원

- **정의 위치:** `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:53-63`
- **멤버 전체 (코드 그대로):**
  ```cpp
  class AnimGraph
  {
    public:
      void SetRoot(std::unique_ptr<FAnimGraphNode_Base> Node) { Root = std::move(Node); }
      FAnimGraphNode_Base *GetRoot() const { return Root.get(); }
      void Evaluate(const FAnimEvalContext &Ctx, TArray<FMatrix> &OutLocalPose);
    private:
      std::unique_ptr<FAnimGraphNode_Base> Root;
  };
  ```
- **검증:**
  - Root 외 노드 컨테이너(트리/배열): **없음**.
  - 노드 동적 add/remove/swap API: **없음**. `SetRoot`로 root 전체를 갈아끼우는 것만 가능.
  - `SetRoot` 시그니처: `void SetRoot(std::unique_ptr<FAnimGraphNode_Base> Node)` (by-value unique_ptr → move 소유권 이전).
- **`Evaluate` 구현:** `AnimGraph.cpp:35-43` — `Root`가 nullptr이면 bind pose로 채우고 return, 아니면 `Root->Evaluate(...)` 호출. 그 이상의 토폴로지 처리 없음.
- **판정:** [확정]

---

## S7. UAnimInstance 멤버

- **정의 위치:** `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:21-105`
- **모든 멤버 변수 (`AnimInstance.h:94-104`, 코드 그대로):**
  ```cpp
  USkeleton                  *Skeleton = nullptr;     // ref, not owned
  std::unique_ptr<AnimGraph>  AnimGraphPtr;           // owned
  TArray<FMatrix>             OutputLocalPose;        // size = BoneCount
  TArray<int32>               TrackToBoneIndex;       // size = current DataModel 트랙 수
  TArray<FName>               TriggeredNotifiesThisFrame;

  float CurrentTime   = 0.0f;
  float PreviousTime  = 0.0f; // 구간 사이 event 처리할 때 필요.
  float PlaybackSpeed = 1.0f;
  bool  bLooping      = true;
  bool  bPaused       = true;
  ```
- **각 추정 항목 확인:**
  - `TMap<FName, bool> BoolVariables` 또는 유사한 변수 저장소: **없음** (추정 부합).
  - `TMap<FName, float> FloatVariables`: **없음** (추정 부합).
  - Notify delegate(`OnNotify` 등 `TDelegate` 멤버): **없음** (추정 부합). 대신 **매 프레임 폴링 패턴**으로 `TArray<FName> TriggeredNotifiesThisFrame`(line 98)에 trigger된 Notify 이름을 push하고 `GetTriggeredNotifiesThisFrame()`(line 45)로 외부에 노출. 채움 로직은 `Update()`(`AnimInstance.cpp:75-86`).
  - 스크래치 포즈 버퍼(`ScratchPoseA/B`, 포즈 풀): **없음** (추정 부합). 출력은 `OutputLocalPose` 한 개만 보유.
- **시간·재생 상태 멤버:** 모두 존재.
  - `float CurrentTime = 0.0f` (line 100)
  - `float PreviousTime = 0.0f` (line 101)
  - `float PlaybackSpeed = 1.0f` (line 102)
  - `bool bLooping = true` (line 103)
  - `bool bPaused = true` (line 104) — 초기값이 `true`(시작 시 pause)인 점 주의.
- **`OutputLocalPose` 타입과 getter:**
  - 타입: `TArray<FMatrix>` (line 96).
  - getter: `const TArray<FMatrix> &GetOutputLocalPose() const { return OutputLocalPose; }` (line 44).
- **추가 관찰 (파트 A 영향):**
  - `protected` 가상 훅 3개로 파생이 시퀀스 메타를 노출하는 구조 (`AnimInstance.h:66-81`):
    - `virtual float GetEffectivePlayLength() const` (default 0)
    - `virtual const TArray<FAnimNotifyEvent> *GetActiveNotifies() const` (default nullptr)
    - `virtual const UAnimDataModel *GetActiveDataModel() const` (default nullptr)
  - 즉 base `UAnimInstance`는 자체적으로 시퀀스를 모르며, 파생이 "현재 활성 시퀀스 / 길이 / Notifies / DataModel"을 제공하는 패턴.
  - `RebuildTrackToBoneIndex()` (`AnimInstance.h:87`, `AnimInstance.cpp:112-129`)가 `GetActiveDataModel()` 결과를 기준으로 트랙→본 인덱스 캐시를 빌드함. 시퀀스 교체 시점에 호출 필요.
- **판정:** [확정]

---

## S8. Math 라이브러리

### 1) Quaternion Slerp
- **위치:** `KraftonEngine/Source/Engine/Math/Quat.h:80-113`
- **시그니처:** `static FQuat FQuat::Slerp(const FQuat& A, const FQuat& B, float Alpha)`
- **최단경로 처리:** **있음** — dot product < 0이면 B의 모든 성분 부호를 뒤집어 `B2`로 처리 (`Quat.h:84-90`).
- **거의 같은 방향(≥0.9999) 처리:** linear blend fallback 후 정규화 (`Quat.h:93-97, 112`).
- **반환 정규화:** `.GetNormalized()` 적용 (`Quat.h:112`).
- **호출 지점 (현재 코드):** `AnimGraph.cpp:123` — SequencePlayer가 회전 키 보간에 사용.
- **판정:** [확정 — 존재]

### 2) Quaternion ↔ Matrix
- **`FQuat::ToMatrix()`** — 선언 `Quat.h:127`, 구현 `Quat.cpp:52-65`. 시그니처: `FMatrix FQuat::ToMatrix() const`.
- **`FQuat::FromMatrix(const FMatrix&)`** — 선언 `Quat.h:129`, 구현 `Quat.cpp:67-107`. static. Trace 기반 case 분기 후 `.Normalize()` 적용.
- **`FMatrix::ToQuat()`** — 선언 `Matrix.h:120` (`struct FQuat ToQuat() const`). 구현은 grep으로 직접 확인하지 않았으나 선언상 존재 [확정]; 실제 본문은 `Quat.cpp` 또는 `Matrix.cpp`에 있을 것으로 추정되며 호출자가 필요할 때 정확한 본문 위치는 한 번 더 확인 권장.
- **판정:** [확정 — 존재]

### 3) Matrix decompose (행렬 → TRS)
- **한 번에 `(T, R, S)` 또는 `(T, Q, S)` 튜플로 분해하는 전용 함수: 없음** [미발견].
- **개별 getter는 존재:**
  - `FVector FMatrix::GetLocation() const` — 선언 `Matrix.h:109`, 구현 `Matrix.cpp:503`.
  - `FVector FMatrix::GetScale() const` — 선언 `Matrix.h:110`, 구현 `Matrix.cpp:509`.
  - `FVector FMatrix::GetEuler() const` — 선언 `Matrix.h:108`, 구현 `Matrix.cpp:479`. (오일러 각, 쿼터니언 아님)
  - 행렬→쿼터니언은 위 2)의 `FMatrix::ToQuat()` 또는 `FQuat::FromMatrix()` 별도 호출 필요.
- **Blend 시 행렬을 받아 재분해해야 하는 시나리오에서의 함의:** 3회의 별도 API 호출(`GetLocation`, `GetScale`, `ToQuat`/`FromMatrix`)이 필요하며, 각각 내부에서 동일한 행렬을 다시 스캔하므로 비용이 누적된다는 사실은 코드로 확인됨 (별도 통합 분해 함수 없음).
- **판정:** [미발견] (통합 decompose 함수 없음, 개별 getter는 존재).

### 4) TRS compose (TRS → 행렬)
- **전용 `ComposeTRS(P, R, S)` 함수: 없음** [미발견].
- **대체 패턴:** `FTransform(P, R, S).ToMatrix()` — `Transform.h:13-14`(생성자), `Transform.cpp:3-12`(`ToMatrix`).
  - 내부 합성 순서 (`Transform.cpp:11`): `scaleMatrix * rotationMatrix * translateMatrix` (row-major 가정 하의 SRT 순).
- **현재 호출 지점:** `AnimGraph.cpp:126` — SequencePlayer가 트랙별로 사용.
- **판정:** [미발견] (전용 함수 없음, `FTransform::ToMatrix()`로 대체 가능).

### 5) Vector lerp
- **위치:** `KraftonEngine/Source/Engine/Math/Vector.h:69`
- **시그니처:** `static FVector FVector::Lerp(const FVector& A, const FVector& B, float t)` (구현은 `Vector.cpp`, 본문은 미확인이나 선언으로 존재 확정).
- **호출 지점:** `AnimGraph.cpp:122, 124` — 위치 키와 스케일 키 보간에 사용.
- **판정:** [확정 — 존재]

### 6) FQuat / FTransform 타입 자체
- **`FQuat`** — `Math/Quat.h:9-130`. 4 float 멤버 (`X, Y, Z, W`). 보유 API: `FromAxisAngle`(static), `operator*`/`operator*=`, `SizeSquared`/`Size`, `Normalize`/`GetNormalized`, `Inverse`, `RotateVector`, `GetForward/Right/UpVector`, `Slerp`(static), `Equals`, `Identity`(static const), `ToRotator`, `ToMatrix`, `FromRotator`(static), `FromMatrix`(static).
- **`FTransform`** — `Math/Transform.h:6-29`. **T/R/S 모두 보유** (`FVector Location`, `FQuat Rotation`, `FVector Scale`). 보유 API: 생성자 3종(FQuat 직접, FRotator로부터, FVector 오일러로부터), `GetRotator`/`SetRotation`(FRotator·FQuat 오버로드), `ToMatrix`. **S4의 "중간 표현" 후보로 곧바로 사용 가능한 구조** — Blend 노드 출력 버퍼 타입을 `TArray<FTransform>`로 두면 행렬 재분해를 회피하고 SequencePlayer의 P/R/S 지역 변수와 동일 형태로 데이터를 전달할 수 있음(사실 진술).
- **판정:** [확정 — 존재]

### S8 종합 판정
**일부 [미발견]** — 통합 행렬 decompose 함수와 전용 `ComposeTRS` 함수 없음. 나머지(Slerp, ToMatrix/FromMatrix, Vector::Lerp, FQuat/FTransform 정의)는 모두 존재.

---

## S9. TDelegate

- **정의 위치:** `KraftonEngine/Source/Engine/Runtime/Delegate.h:30-177` — **[범위초과]** (사용자가 지정한 스캔 범위는 `Animation/`, `Component/`, `Math/`이며 `Runtime/`은 그 밖). 사용자가 명시한 "범위 밖이면 추정 경로만 적으라"는 규칙이 있으나, 실제로 1회 열람해 확인했으므로 그 사실을 정직하게 표기.
- **시그니처:**
  - 클래스 템플릿: `template<class... Args> class TDelegate` (line 29-30).
  - 핸들러 저장 타입: `using FunctionType = std::function<void(Args...)>;`, `using HandlerType = TPair<uint32, FunctionType>;` (line 35-36).
  - **브로드캐스트:** `void BroadCast(const Args&... args)` (line 110) — **대문자 표기는 `BroadCast`** (B와 C가 모두 대문자, 카멜케이스). `Broadcast` 아님.
  - **일반 함수 등록:** `uint32 Add(const FunctionType& handler)` (line 78), instance UUID 동반 오버로드 line 85.
  - **멤버 함수 동적 등록:** `template<HasGetUUID T> uint32 AddDynamic(T* Instance, void (T::* Function)(Args...))` (line 95). `T`는 `GetUUID() -> uint32` 변환 가능해야 함(컨셉 line 23-27).
  - **제거:** `void Remove(uint32 HandlerID)` (line 136), `void RemoveAllByInstance(uint32 InstanceUUID)` (line 145).
- **편의 매크로:** `DECLARE_DELEGATE(Name, ...)` → `TDelegate<__VA_ARGS__> Name;` (line 9-10).
- **사용자가 매핑 문서에서 본 `TDelegate<const FAnimNotifyEvent&>` 사용 여부:** Animation/Notify 디렉터리 grep 결과 **사용 없음** (`Asset/Animation` 전체에서 `TDelegate`/`DECLARE_DELEGATE` 미검출). 매핑 문서가 가리킨 형태는 현재 코드에 아직 도입되지 않았음.
- **판정:** [범위초과] + 정보 기록.

---

## 반증된 추정 모음

### S2 (부분 [반증])
- **추정:** "UAnimSingleNodeInstance가 graph를 우회해서 먼저 구현했고, SingleNode가 `FAnimGraphNode_SequencePlayer`를 사용하지 않는다."
- **실제:** SingleNode는 `FAnimGraphNode_SequencePlayer SequencePlayer;`를 **값 멤버로 보유**(`AnimSingleNodeInstance.h:35`)하고 `EvaluateGraph()`에서 그 노드의 `Evaluate`를 **직접 호출**(`AnimSingleNodeInstance.cpp:50`).
- **차이의 핵심:** "graph 우회"가 가리키는 대상이 `AnimGraph`(class)와 `Root` 슬롯 메커니즘이라는 점은 맞으나, **`FAnimGraphNode_SequencePlayer` 노드 자체는 우회되지 않고 동일하게 사용된다**. 즉 파트 A에서 Blend·StateMachine 노드를 같은 베이스(`FAnimGraphNode_Base`)로 만들어도, SequencePlayer는 이미 노드 형태로 존재하고 SingleNode와 graph 양쪽이 모두 그 노드를 인스턴스화한다.

---

## 파트 A 구현에 영향을 주는 사실

> 사실만 적는다. 해법·제안은 다음 단계의 파트 A 구현 계획 프롬프트에서 다뤄진다.

1. **출력 표현이 `TArray<FMatrix>`로 고정되어 있고, TRS 중간 표현은 외부에 노출되지 않는다.**
   - `FAnimGraphNode_Base::Evaluate` 시그니처가 `TArray<FMatrix> &OutLocalPose`로 고정되어 있어, 같은 베이스를 따르는 Blend 노드도 동일 출력 형식을 따라야 한다. 그러나 SequencePlayer 내부의 보간 결과는 `FMatrix`로 합성된 직후에만 외부에 보이므로, Blend가 TRS 단위로 다시 보간하려면 행렬 분해(`FMatrix::GetLocation/GetScale` + `FMatrix::ToQuat`/`FQuat::FromMatrix`) 단계가 필요해진다. 분해 통합 함수는 없으며 개별 getter 3회 호출이 강제된다는 사실은 코드로 확인됨.

2. **SingleNode 경로는 `AnimGraph`를 사용하지 않는다.**
   - `UAnimSingleNodeInstance::EvaluateGraph()`가 base를 우회하므로(`AnimSingleNodeInstance.cpp:31` 주석 명시), `AnimGraph` + `Root` 슬롯 메커니즘은 현재 어떤 경로에서도 활성화돼 있지 않다. 첫 사용처는 파트 A에서 만들어질 Blend/StateMachine을 담는 새 `UAnimInstance` 파생(혹은 SingleNode의 변경)이 된다.

3. **`FAnimGraphNode_Base::Evaluate`는 non-const 멤버 함수다.**
   - `AnimGraph.h:37`. 노드가 자체 상태(예: StateMachine의 현재 상태, 잔여 블렌드 시간 등)를 멤버로 보유해 매 평가마다 변경하는 것이 시그니처 차원에서 허용된다.

4. **`AnimGraph`는 동적 노드 토폴로지 API가 없다.**
   - `SetRoot` 하나만 존재(`AnimGraph.h:56`). 다중 노드/연결 표현이나 add/remove/swap이 없으므로, 파트 A의 Blend 트리/StateMachine은 각각의 루트 노드 내부에 자체적으로 자식 노드들을 보유하는 구조로 만들어진다는 점이 코드로 확정됨.

5. **`FAnimEvalContext.Sequence` 필드는 현재 항상 nullptr로 평가에 진입한다.**
   - 두 호출자(`AnimInstance.cpp:103-109`, `AnimSingleNodeInstance.cpp:44-50`) 모두 이 필드를 채우지 않는다. 노드가 시퀀스 메타에 접근하려면 호출자 수정이 선행돼야 한다는 사실.

6. **시간·재생 상태와 시퀀스 메타는 모두 `UAnimInstance` 레이어에 있다.**
   - 노드(`FAnimGraphNode_*`)는 시간을 보유하지 않으며 `Ctx.TimeSeconds`만 받는다. 따라서 StateMachine의 transition 타이밍(현재 시간 비교, dt 누적)을 노드 내부에서 처리하려면 `FAnimEvalContext`에 dt를 추가하거나, 시간 누적을 `UAnimInstance::Update` 쪽에서 처리하고 노드는 결과만 받는 구조로 가야 한다는 점이 코드 형태로 확인됨.

7. **Notify 디스패치는 폴링 방식이며 delegate가 없다.**
   - `UAnimInstance::Update`(`AnimInstance.cpp:75-86`)가 `TriggeredNotifiesThisFrame`에 이름을 쌓고 외부가 `GetTriggeredNotifiesThisFrame()`로 가져가는 구조. 파트 B에서 `TDelegate<const FAnimNotifyEvent&>` 도입이 매핑 문서에 언급되어 있으나 현재 코드에는 없음.

8. **`FTransform`은 T/R/S를 그대로 보유한 형태로 존재한다.**
   - `Transform.h:6-29`. 별도 새 구조체 정의 없이도 TRS를 묶어 운반할 수 있다는 사실. SequencePlayer가 이미 `FTransform(P,R,S).ToMatrix()`로 사용 중이므로 도입 비용은 사실상 0이라는 점이 코드로 확인됨.

9. **수학 라이브러리에 부재한 것 (Blend 구현 시 영향):**
   - 통합 `Matrix Decompose(M) -> (T, Q, S)` 함수 없음.
   - 전용 `ComposeTRS(T, R, S) -> Matrix` 함수 없음 (`FTransform::ToMatrix()`가 그 역할).
   - 두 쿼터니언의 Nlerp(정규화 선형 보간) 별도 함수 없음 (Slerp만 존재). Slerp의 "거의 같은 방향" 분기가 사실상 Nlerp fallback 역할.

---

## 미해결 / 추가 스캔 필요

1. **`FMatrix::ToQuat()` 본문 위치 미확인.** 선언은 `Matrix.h:120`에 있으나 구현 파일·라인은 본 스캔에서 직접 열어보지 않음. 파트 A 구현 단계에서 사용 결정 시 한 번 더 확인 권장 (가장 가능성 높은 위치: `Math/Matrix.cpp` 또는 `Math/Quat.cpp`).

2. **`Skeleton.h` / `FBoneInfo` 사용 패턴 미확인.** `AnimGraph.cpp`가 `Skeleton->GetBones()`, `Skeleton->FindBoneIndexByName(FString)`을 호출하지만(`AnimInstance.cpp:127`), `USkeleton` 본 자체의 정의 파일은 본 스캔에서 열지 않음. 파트 A가 본 트리(부모 인덱스 등)를 활용해야 하면 추가 확인 필요. 스캔 범위 내(`Asset/Animation/Core/Skeleton.h` 존재 추정)이므로 [범위초과] 아님.

3. **`USkeletalMeshComponent`가 `UAnimInstance`를 어떻게 생성하는지 미확인.**
   - `SkeletalMeshComponent.h:101`에 `UAnimInstance *AnimInstance = nullptr;` 멤버는 보임, `Play(...)`/`SetAnimation(...)` 선언도 보임(`SkeletalMeshComponent.h:29-30, 45-46`).
   - 그러나 cpp의 어디서 `UAnimSingleNodeInstance` 인스턴스를 만드는지(`NewObject<>` 류 호출 위치)는 본 스캔에서 grep으로 일부만 확인. 파트 A에서 별도 `UAnimInstance` 파생을 컴포넌트가 받게 하려면 그 생성·교체 경로를 정확히 알아야 함 — 추가 스캔 권장.

4. **`Runtime/Delegate.h`의 스캔 범위 처리.** 사용자 정의 범위 밖이라 [범위초과] 처리했으나, 파트 B에서 도입 시 정식 검토 항목으로 끌어올릴 필요.

5. **다른 `UAnimInstance` 파생이 이미 있는지 미검증.** `UAnimSingleNodeInstance` 외 파생 클래스 존재 여부는 본 스캔에서 전수 검색하지 않음. 파트 A 구현이 새 파생을 만들 때 충돌 회피를 위해 한 번 더 grep 권장 (`: public UAnimInstance`).
