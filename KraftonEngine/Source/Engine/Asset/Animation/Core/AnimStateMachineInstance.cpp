#include "Asset/Animation/Core/AnimStateMachineInstance.h"

#include "Asset/Animation/Core/AnimGraph.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "Object/ObjectFactory.h"

#include <algorithm>
#include <cmath>

IMPLEMENT_CLASS(UAnimStateMachineInstance, UAnimInstance)

UAnimStateMachineInstance::UAnimStateMachineInstance() = default;

void UAnimStateMachineInstance::SetStateMachineGraph(std::unique_ptr<FAnimGraphNode_StateMachine> InRoot)
{
    if (!AnimGraphPtr)
    {
        AnimGraphPtr = std::make_unique<AnimGraph>();
    }

    if (InRoot)
    {
        // SubLengthHint 자동 도출 — Sub가 SequencePlayer면 Sequence->GetPlayLength().
        // 사용자가 SubLengthHint를 명시(>0) 한 경우는 우선.
        for (FAnimState &S : InRoot->States)
        {
            if (S.SubLengthHint > 0.0f) continue;
            if (auto *SeqPlayer = dynamic_cast<FAnimGraphNode_SequencePlayer *>(S.Sub.get()))
            {
                if (SeqPlayer->Sequence)
                {
                    S.SubLengthHint = SeqPlayer->Sequence->GetPlayLength();
                }
            }
        }
    }

    // 그래프 교체 시 PrevState 추적 reset — 새 그래프의 ActiveStateIndex와 동기화.
    PrevActiveStateIndex = -1;
    PrevStateLocalTime   = 0.0f;

    AnimGraphPtr->SetRoot(std::move(InRoot));
}

bool UAnimStateMachineInstance::GetBoolVariable(const FName &Name, bool Default) const
{
    const auto It = BoolVariables.find(Name);
    return (It != BoolVariables.end()) ? It->second : Default;
}

// =================================================================
// I1 STEP 4~6, 9, 12 — PIE silent 해소 (D3/D4 옵션 1 + 후보 b)
// =================================================================

FAnimGraphNode_StateMachine *UAnimStateMachineInstance::ResolveStateMachineRoot() const
{
    if (!AnimGraphPtr) return nullptr;
    FAnimGraphNode_Base *Root = AnimGraphPtr->GetRoot();
    if (!Root) return nullptr;
    return dynamic_cast<FAnimGraphNode_StateMachine *>(Root);
}

// I1 STEP 5 + STEP 12: SequencePlayer / 중첩 StateMachine / Blend2 / BlendN 분기.
const TArray<FAnimNotifyEvent> *
UAnimStateMachineInstance::ResolveActiveNotifiesFromNode(FAnimGraphNode_Base *Node)
{
    if (!Node) return nullptr;

    if (auto *SP = dynamic_cast<FAnimGraphNode_SequencePlayer *>(Node))
    {
        if (!SP->Sequence) return nullptr;
        return &SP->Sequence->GetNotifies();
    }

    if (auto *SM = dynamic_cast<FAnimGraphNode_StateMachine *>(Node))
    {
        const int32 ActiveIdx = SM->GetActiveStateIndex();
        if (ActiveIdx < 0 || ActiveIdx >= static_cast<int32>(SM->States.size())) return nullptr;
        return ResolveActiveNotifiesFromNode(SM->States[ActiveIdx].Sub.get());
    }

    if (auto *B2 = dynamic_cast<FAnimGraphNode_Blend2 *>(Node))
    {
        FAnimGraphNode_Base *Active = (B2->Alpha < 0.5f) ? B2->ChildA.get() : B2->ChildB.get();
        return ResolveActiveNotifiesFromNode(Active);
    }

    if (auto *BN = dynamic_cast<FAnimGraphNode_BlendN *>(Node))
    {
        if (BN->Children.empty() || BN->Children.size() != BN->Weights.size()) return nullptr;
        int32 MaxIdx = 0;
        float MaxW   = BN->Weights[0];
        for (int32 i = 1; i < static_cast<int32>(BN->Weights.size()); ++i)
        {
            if (BN->Weights[i] > MaxW)
            {
                MaxW   = BN->Weights[i];
                MaxIdx = i;
            }
        }
        return ResolveActiveNotifiesFromNode(BN->Children[MaxIdx].get());
    }

    return nullptr;
}

// I1 STEP 7 + STEP 12 — UAnimGraphInstance 도 재사용.
float UAnimStateMachineInstance::ResolveActivePlayLengthFromNode(FAnimGraphNode_Base *Node)
{
    if (!Node) return 0.0f;

    if (auto *SP = dynamic_cast<FAnimGraphNode_SequencePlayer *>(Node))
    {
        return SP->Sequence ? SP->Sequence->GetPlayLength() : 0.0f;
    }

    if (auto *SM = dynamic_cast<FAnimGraphNode_StateMachine *>(Node))
    {
        const int32 ActiveIdx = SM->GetActiveStateIndex();
        if (ActiveIdx < 0 || ActiveIdx >= static_cast<int32>(SM->States.size())) return 0.0f;
        // 부모 state의 SubLengthHint를 우선 사용. 0이면 재귀로 내려가서 자식 SequencePlayer length 사용.
        const float Hint = SM->States[ActiveIdx].SubLengthHint;
        if (Hint > 0.0f) return Hint;
        return ResolveActivePlayLengthFromNode(SM->States[ActiveIdx].Sub.get());
    }

    if (auto *B2 = dynamic_cast<FAnimGraphNode_Blend2 *>(Node))
    {
        FAnimGraphNode_Base *Active = (B2->Alpha < 0.5f) ? B2->ChildA.get() : B2->ChildB.get();
        return ResolveActivePlayLengthFromNode(Active);
    }

    if (auto *BN = dynamic_cast<FAnimGraphNode_BlendN *>(Node))
    {
        if (BN->Children.empty() || BN->Children.size() != BN->Weights.size()) return 0.0f;
        int32 MaxIdx = 0;
        float MaxW   = BN->Weights[0];
        for (int32 i = 1; i < static_cast<int32>(BN->Weights.size()); ++i)
        {
            if (BN->Weights[i] > MaxW)
            {
                MaxW   = BN->Weights[i];
                MaxIdx = i;
            }
        }
        return ResolveActivePlayLengthFromNode(BN->Children[MaxIdx].get());
    }

    return 0.0f;
}

float UAnimStateMachineInstance::GetEffectivePlayLength() const
{
    FAnimGraphNode_StateMachine *SM = ResolveStateMachineRoot();
    if (!SM) return 0.0f;
    const int32 ActiveIdx = SM->GetActiveStateIndex();
    if (ActiveIdx < 0 || ActiveIdx >= static_cast<int32>(SM->States.size())) return 0.0f;
    // 부모 SubLengthHint 우선, 없으면 자식 노드 깊이 재귀.
    const float Hint = SM->States[ActiveIdx].SubLengthHint;
    if (Hint > 0.0f) return Hint;
    return ResolveActivePlayLengthFromNode(SM->States[ActiveIdx].Sub.get());
}

const TArray<FAnimNotifyEvent> *UAnimStateMachineInstance::GetActiveNotifies() const
{
    FAnimGraphNode_StateMachine *SM = ResolveStateMachineRoot();
    if (!SM) return nullptr;
    const int32 ActiveIdx = SM->GetActiveStateIndex();
    if (ActiveIdx < 0 || ActiveIdx >= static_cast<int32>(SM->States.size())) return nullptr;
    return ResolveActiveNotifiesFromNode(SM->States[ActiveIdx].Sub.get());
}

// I1 STEP 6: state-local time 기반 notify firing.
// base UAnimInstance::Update의 책임을 모두 재현 (LastDeltaTime / TriggeredNotifiesThisFrame.clear /
// notify trigger / Dispatch). 단 PrevTime/CurrTime은 AnimInstance level이 아니라 state-local time 사용.
void UAnimStateMachineInstance::Update(float DeltaTime)
{
    // base 책임 (a)(b) — paused 분기 + clear.
    LastDeltaTime = bPaused ? 0.0f : DeltaTime;
    TriggeredNotifiesThisFrame.clear();
    if (bPaused) return;

    FAnimGraphNode_StateMachine *SM = ResolveStateMachineRoot();
    if (!SM)
    {
        // 그래프 미구성 — base의 Length<=0 early return과 동등 처리.
        PreviousTime = CurrentTime = 0.0f;
        PrevActiveStateIndex = -1;
        PrevStateLocalTime   = 0.0f;
        return;
    }

    const int32 ActiveIdx = SM->GetActiveStateIndex();
    if (ActiveIdx < 0 || ActiveIdx >= static_cast<int32>(SM->States.size()))
    {
        PreviousTime = CurrentTime = 0.0f;
        PrevActiveStateIndex = -1;
        PrevStateLocalTime   = 0.0f;
        return;
    }

    const TArray<FAnimNotifyEvent> *Notifies = GetActiveNotifies();
    const float Length = GetEffectivePlayLength();
    if (!Notifies || Length <= 0.0f)
    {
        // 활성 sequence 없거나 length 미정 — 이번 frame notify 없음.
        // 다음 frame을 위해 state 추적은 갱신.
        PrevActiveStateIndex = ActiveIdx;
        PrevStateLocalTime   = SM->GetStateLocalTime(ActiveIdx);
        PreviousTime         = PrevStateLocalTime;
        CurrentTime          = PrevStateLocalTime;
        return;
    }

    // I1 STEP 11: 1-frame 지연 보정 — 본 frame의 Evaluate가 진행할 예상 state-local time.
    // base StateMachine::Evaluate(line 63)이 본 frame에 StateLocalTimes[ActiveIdx] += DeltaTime 수행 예정.
    float PredictedCurrStateTime = SM->GetStateLocalTime(ActiveIdx) + DeltaTime;
    if (SM->States[ActiveIdx].bLooping)
    {
        // base Evaluate의 ApplyLoopOrClamp와 동일 의미 — fmod wrap.
        PredictedCurrStateTime = std::fmod(PredictedCurrStateTime, Length);
        if (PredictedCurrStateTime < 0.0f) PredictedCurrStateTime += Length;
    }
    // 비-loop 케이스: clamp는 SequencePlayer가 처리. PrevTime<TriggerTime<=CurrTime 평가만 영향.

    // I1 STEP 8.5: state 전환 감지. bResetTimeOnEnter true/false 양쪽 호환.
    // 전환 직후 첫 frame은 "한 DeltaTime 만큼 새 state에서 진행" 으로 의미 통일.
    if (ActiveIdx != PrevActiveStateIndex)
    {
        PrevStateLocalTime   = std::max(0.0f, PredictedCurrStateTime - DeltaTime);
        PrevActiveStateIndex = ActiveIdx;
    }

    // notify firing.
    TArray<const FAnimNotifyEvent *> LocalTriggered;
    for (const FAnimNotifyEvent &N : *Notifies)
    {
        if (N.IsTriggeredBetween(PrevStateLocalTime, PredictedCurrStateTime, Length))
        {
            TriggeredNotifiesThisFrame.push_back(N);
            LocalTriggered.push_back(&N);
        }
    }
    if (!LocalTriggered.empty() && ShouldFallbackToCppDispatch())
    {
        // PlayerController 부재 환경(editor preview)에서만 C++ fallback dispatch.
        DispatchTriggeredNotifies(LocalTriggered);
    }

    // 다음 frame 을 위해 갱신.
    PrevStateLocalTime = PredictedCurrStateTime;

    // base mirror — SkeletalMeshComponent::BakedAnimTime 등 외부 caller 호환 (I1 STEP 8.3).
    PreviousTime = PrevStateLocalTime;
    CurrentTime  = PredictedCurrStateTime;
}

// I1 STEP 9: scrub/seek 시 활성 state의 state-local time을 set.
// base 동작도 보존 (PreviousTime=CurrentTime=Time mirror) 하여 외부 caller 호환.
void UAnimStateMachineInstance::SetEvaluationTime(float Time)
{
    FAnimGraphNode_StateMachine *SM = ResolveStateMachineRoot();
    if (SM)
    {
        const int32 ActiveIdx = SM->GetActiveStateIndex();
        if (ActiveIdx >= 0 && ActiveIdx < static_cast<int32>(SM->States.size()))
        {
            SM->SetStateLocalTime(ActiveIdx, Time);
        }
    }
    // scrub-suppress-notify 안전망 (P-Fix5 V5 정합) — PrevStateLocalTime=Time → 다음 Update에서
    // PrevTime==PredictedCurrTime 이 되어 IsTriggeredBetween 평가가 zero interval로 회귀.
    PrevStateLocalTime = Time;
    PreviousTime       = Time;
    CurrentTime        = Time;
}
