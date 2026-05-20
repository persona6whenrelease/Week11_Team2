/**
 * AnimGraph 스테이트 머신 노드와 그 부속 데이터 구조.
 *
 * B1 범위: 정적 표현(상태·전이·조건 + 노드 정의)만 도입.
 * 평가 알고리즘은 B2 (FAnimGraphNode_StateMachine::Evaluate 본문).
 *
 * 인스턴스 컨테이너(UAnimStateMachineInstance)와 BoolVariables 평가는 B3.
 */

#pragma once

#include "Asset/Animation/Core/AnimGraph.h"
#include "Core/CoreTypes.h"
#include "Object/FName.h"

#include <memory>

/**
 * StateMachine의 한 상태. Sub는 보통 SequencePlayer 또는 Blend 노드 트리.
 * - bResetTimeOnEnter: 상태 진입 시 StateLocalTimes[idx]를 0으로 리셋할지.
 * - bLooping + SubLengthHint: 상태 시간을 fmod로 wrap할지 (0이면 clamp 동작).
 */
struct FAnimState
{
    FName                                Name;
    std::unique_ptr<FAnimGraphNode_Base> Sub;
    bool                                 bResetTimeOnEnter = true;
    bool                                 bLooping          = true;
    float                                SubLengthHint     = 0.0f;
};

enum class EAnimTransitionConditionKind : uint8
{
    TimeElapsed,   // 활성 상태의 누적 체류 시간 >= TimeThreshold
    BoolVariable,  // OwningInstance(UAnimStateMachineInstance)의 BoolVariables[VarName] == bExpectedValue
    OnNotify,      // 파트 B 대기 — B2에서 평가 false
    Custom         // 파트 B 대기 — B2에서 평가 false
};

struct FAnimTransitionCondition
{
    EAnimTransitionConditionKind Kind          = EAnimTransitionConditionKind::TimeElapsed;
    float                        TimeThreshold = 0.0f;
    FName                        VarName;
    bool                         bExpectedValue = true;
    FName                        NotifyName;
};

struct FAnimTransition
{
    int32                            FromStateIndex = -1;
    int32                            ToStateIndex   = -1;
    float                            BlendDuration  = 0.2f;
    TArray<FAnimTransitionCondition> Conditions; // AND 결합
};

/**
 * 스테이트 머신 노드. States/Transitions는 외부(에디터·Lua)가 구성해 set한다.
 * 평가 시 자체 시간 누적 + 조건 발화 + transition 진행 중 BlendTransform 합성.
 */
struct FAnimGraphNode_StateMachine : FAnimGraphNode_Base
{
    TArray<FAnimState>      States;
    TArray<FAnimTransition> Transitions;
    int32                   InitialStateIndex = 0;

    void Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose) override;

    // Notify firing 용 — UAnimStateMachineInstance::Update override 가 사용 (D4 §G1.3 + I1 STEP 2).
    int32 GetActiveStateIndex() const { return ActiveStateIndex; }
    int32 GetActiveTransitionIndex() const { return ActiveTransitionIndex; }
    float GetStateLocalTime(int32 StateIdx) const
    {
        if (StateIdx < 0 || StateIdx >= static_cast<int32>(StateLocalTimes.size())) return 0.0f;
        return StateLocalTimes[StateIdx];
    }
    // scrub/seek 시 활성 state의 time을 외부에서 set (I1 STEP 9).
    void SetStateLocalTime(int32 StateIdx, float Time)
    {
        if (StateIdx < 0 || StateIdx >= static_cast<int32>(StateLocalTimes.size())) return;
        StateLocalTimes[StateIdx] = Time;
    }

  private:
    void        ApplyLoopOrClamp(int32 StateIdx);
    static bool EvaluateConditions(class UAnimInstance *Owning,
                                   const TArray<FAnimTransitionCondition> &Conds,
                                   float ActiveStateTime);

    int32              ActiveStateIndex      = -1;
    int32              ActiveTransitionIndex = -1;
    float              TransitionElapsed     = 0.0f;
    TArray<float>      StateLocalTimes; // size == States.size()
    TArray<FTransform> ScratchFrom;
    TArray<FTransform> ScratchTo;
};
