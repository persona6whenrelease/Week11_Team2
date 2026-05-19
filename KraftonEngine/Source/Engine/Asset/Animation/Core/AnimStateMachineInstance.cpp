#include "Asset/Animation/Core/AnimStateMachineInstance.h"

#include "Asset/Animation/Core/AnimGraph.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "Object/ObjectFactory.h"

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

    AnimGraphPtr->SetRoot(std::move(InRoot));
}

bool UAnimStateMachineInstance::GetBoolVariable(const FName &Name, bool Default) const
{
    const auto It = BoolVariables.find(Name);
    return (It != BoolVariables.end()) ? It->second : Default;
}
