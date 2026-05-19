#include "Asset/Animation/Core/AnimGraph_StateMachine.h"

#include "Asset/Animation/Core/AnimInstance.h"
#include "Asset/Animation/Core/AnimPoseUtils.h"
#include "Asset/Animation/Core/Skeleton.h"

#include <algorithm>
#include <cmath>

void FAnimGraphNode_StateMachine::ApplyLoopOrClamp(int32 StateIdx)
{
    const FAnimState &S = States[StateIdx];
    if (!S.bLooping)             return; // wrap 안 함 — sub-graph가 자체 clamp(SequencePlayer는 마지막 프레임)
    if (S.SubLengthHint <= 0.0f) return; // 길이 미지정 — wrap 불가
    float &T = StateLocalTimes[StateIdx];
    T = std::fmod(T, S.SubLengthHint);
    if (T < 0.0f) T += S.SubLengthHint;
}

bool FAnimGraphNode_StateMachine::EvaluateConditions(UAnimInstance *Owning,
                                                    const TArray<FAnimTransitionCondition> &Conds,
                                                    float ActiveStateTime)
{
    if (Conds.empty()) return false; // 빈 조건 transition은 발화 안 함 (안전)

    for (const FAnimTransitionCondition &C : Conds)
    {
        switch (C.Kind)
        {
        case EAnimTransitionConditionKind::TimeElapsed:
            if (ActiveStateTime < C.TimeThreshold) return false;
            break;
        case EAnimTransitionConditionKind::BoolVariable:
            if (!Owning) return false;
            if (Owning->GetBoolVariable(C.VarName, false) != C.bExpectedValue) return false;
            break;
        case EAnimTransitionConditionKind::OnNotify:
        case EAnimTransitionConditionKind::Custom:
            return false; // 파트 B 대기
        }
    }
    return true; // AND 결합 — 모든 조건 만족
}

void FAnimGraphNode_StateMachine::Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose)
{
    const size_t N = Ctx.Skeleton ? Ctx.Skeleton->GetBones().size() : 0;
    if (States.empty() || N == 0)
    {
        OutLocalPose.clear();
        return;
    }

    // 0) 첫 평가 — initial state로 진입 + StateLocalTimes 초기화
    if (ActiveStateIndex < 0)
    {
        const int32 InitClamped = std::clamp(InitialStateIndex, 0, static_cast<int32>(States.size()) - 1);
        ActiveStateIndex = InitClamped;
        StateLocalTimes.assign(States.size(), 0.0f);
    }

    // 1) 시간 누적 (paused면 Ctx.DeltaTime이 호출자에서 이미 0)
    StateLocalTimes[ActiveStateIndex] += Ctx.DeltaTime;
    ApplyLoopOrClamp(ActiveStateIndex);
    if (ActiveTransitionIndex >= 0)
    {
        const FAnimTransition &T = Transitions[ActiveTransitionIndex];
        TransitionElapsed += Ctx.DeltaTime;
        StateLocalTimes[T.ToStateIndex] += Ctx.DeltaTime;
        ApplyLoopOrClamp(T.ToStateIndex);
    }

    // 2) 진행 중 transition 완료 판정
    if (ActiveTransitionIndex >= 0 &&
        TransitionElapsed >= Transitions[ActiveTransitionIndex].BlendDuration)
    {
        const int32 NewActive = Transitions[ActiveTransitionIndex].ToStateIndex;
        ActiveStateIndex = NewActive;
        if (States[NewActive].bResetTimeOnEnter)
        {
            StateLocalTimes[NewActive] = 0.0f;
        }
        ActiveTransitionIndex = -1;
        TransitionElapsed     = 0.0f;
    }

    // 3) 진행 중 transition 없으면 발화 조건 검사
    if (ActiveTransitionIndex < 0)
    {
        for (int32 i = 0; i < static_cast<int32>(Transitions.size()); ++i)
        {
            const FAnimTransition &T = Transitions[i];
            if (T.FromStateIndex != ActiveStateIndex) continue;
            if (T.ToStateIndex < 0 || T.ToStateIndex >= static_cast<int32>(States.size())) continue;
            if (EvaluateConditions(Ctx.OwningInstance, T.Conditions, StateLocalTimes[ActiveStateIndex]))
            {
                ActiveTransitionIndex = i;
                TransitionElapsed     = 0.0f;
                if (States[T.ToStateIndex].bResetTimeOnEnter)
                {
                    StateLocalTimes[T.ToStateIndex] = 0.0f;
                }
                break;
            }
        }
    }

    // 4) 자식 평가 + 합성
    auto MakeChildCtx = [&](int32 StateIdx) {
        FAnimEvalContext C = Ctx;
        C.TimeSeconds = StateLocalTimes[StateIdx];
        return C;
    };

    if (ActiveTransitionIndex < 0)
    {
        // 단일 상태 평가
        FAnimEvalContext Cx = MakeChildCtx(ActiveStateIndex);
        if (States[ActiveStateIndex].Sub)
        {
            States[ActiveStateIndex].Sub->Evaluate(Cx, OutLocalPose);
        }
        else
        {
            AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, OutLocalPose);
        }
    }
    else
    {
        // From/To 두 상태 평가 후 BlendTransform 합성
        ScratchFrom.resize(N);
        ScratchTo.resize(N);
        OutLocalPose.resize(N);

        const FAnimTransition &T = Transitions[ActiveTransitionIndex];
        FAnimEvalContext CxFrom = MakeChildCtx(T.FromStateIndex);
        FAnimEvalContext CxTo   = MakeChildCtx(T.ToStateIndex);

        if (States[T.FromStateIndex].Sub) States[T.FromStateIndex].Sub->Evaluate(CxFrom, ScratchFrom);
        else                              AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, ScratchFrom);

        if (States[T.ToStateIndex].Sub)   States[T.ToStateIndex].Sub->Evaluate(CxTo, ScratchTo);
        else                              AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, ScratchTo);

        const float Alpha = (T.BlendDuration > 0.0f)
            ? std::clamp(TransitionElapsed / T.BlendDuration, 0.0f, 1.0f)
            : 1.0f;

        for (size_t i = 0; i < N; ++i)
        {
            OutLocalPose[i] = AnimPoseUtils::BlendTransform(ScratchFrom[i], ScratchTo[i], Alpha);
        }
    }
}
