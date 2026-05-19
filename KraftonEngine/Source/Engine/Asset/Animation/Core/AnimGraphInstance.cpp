#include "Asset/Animation/Core/AnimGraphInstance.h"

#include "Asset/Animation/Core/AnimGraph.h"
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
