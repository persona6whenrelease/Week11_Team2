/**
 * 애니메이션 인스턴스의 시간 갱신과 포즈 적용을 구현한다.
 *
 * 단일 노드 인스턴스는 현재 재생 시간을 진행시키고, AnimationRuntime으로 로컬 포즈를 샘플링한 뒤
 * 대상 SkeletalMeshComponent에 전달한다. 시퀀스 길이, 루프 여부, 재생 속도를 함께 고려해 프리뷰와
 * 런타임에서 같은 방식으로 시간이 흐르도록 한다.
 */

#include "Asset/Animation/Runtime/AnimInstance.h"

#include "Asset/Animation/Runtime/AnimationRuntime.h"
#include "Component/SkeletalMeshComponent.h"
#include "Object/ObjectFactory.h"

#include <cmath>

IMPLEMENT_CLASS(UAnimInstance, UObject)
IMPLEMENT_CLASS(UAnimSingleNodeInstance, UAnimInstance)

void UAnimInstance::InitializeAnimation(USkeletalMeshComponent* InOwnerComponent)
{
    OwnerComponent = InOwnerComponent;
}

void UAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    (void)DeltaSeconds;
}

void UAnimSingleNodeInstance::SetAnimation(UAnimSequence* InSequence, bool bInLooping)
{
    Sequence = InSequence;
    bLooping = bInLooping;
    CurrentTime = 0.0f;
    PreviousTime = 0.0f;
}

void UAnimSingleNodeInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    if (!OwnerComponent || !Sequence || !Sequence->IsValidSequence())
    {
        return;
    }

    const float Length = Sequence->GetPlayLength();
    if (bPlaying)
    {
        PreviousTime = CurrentTime;
        CurrentTime += DeltaSeconds * PlayRate;
        if (bLooping && Length > 0.0f)
        {
            CurrentTime = std::fmod(CurrentTime, Length);
            if (CurrentTime < 0.0f)
            {
                CurrentTime += Length;
            }
        }
        else if (CurrentTime > Length)
        {
            CurrentTime = Length;
            bPlaying = false;
        }
        else if (CurrentTime < 0.0f)
        {
            CurrentTime = 0.0f;
            bPlaying = false;
        }
    }

    OwnerComponent->EvaluateAnimationPose(Sequence, CurrentTime);
}
