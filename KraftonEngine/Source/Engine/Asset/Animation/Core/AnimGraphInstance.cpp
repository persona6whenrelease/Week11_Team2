#include "Asset/Animation/Core/AnimGraphInstance.h"

#include "Asset/Animation/Core/AnimGraph.h"
#include "Asset/Animation/Core/AnimStateMachineInstance.h"  // ResolveActive*FromNode 헬퍼 재사용
#include "Object/ObjectFactory.h"

IMPLEMENT_CLASS(UAnimGraphInstance, UAnimInstance)

UAnimGraphInstance::UAnimGraphInstance() = default;

void UAnimGraphInstance::SetRootGraph(std::unique_ptr<FAnimGraphNode_Base> InRoot)
{
    if (!AnimGraphPtr)
    {
        AnimGraphPtr = std::make_unique<AnimGraph>();
    }
    AnimGraphPtr->SetRoot(std::move(InRoot));
}

// I1 STEP 7 + STEP 12: SequencePlayer/Blend2/BlendN/nested StateMachine 분기는
// UAnimStateMachineInstance의 static 헬퍼 재사용.
const TArray<FAnimNotifyEvent> *UAnimGraphInstance::GetActiveNotifies() const
{
    if (!AnimGraphPtr) return nullptr;
    return UAnimStateMachineInstance::ResolveActiveNotifiesFromNode(AnimGraphPtr->GetRoot());
}

float UAnimGraphInstance::GetEffectivePlayLength() const
{
    if (!AnimGraphPtr) return 0.0f;
    return UAnimStateMachineInstance::ResolveActivePlayLengthFromNode(AnimGraphPtr->GetRoot());
}
