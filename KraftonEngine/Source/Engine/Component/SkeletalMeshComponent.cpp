#include "Component/SkeletalMeshComponent.h"

#include "Asset/Animation/Core/AnimInstance.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Core/AnimSingleNodeInstance.h"
#include "Asset/Animation/Core/AnimStateMachineInstance.h"
#include "Asset/Animation/Core/Skeleton.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Object/Object.h"
#include "Object/ObjectFactory.h"

#include <cmath>

IMPLEMENT_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

namespace
{
    /**
     * SkeletalMesh -> USkeleton 경로 helper. 메시 또는 스켈레톤이 비었으면 nullptr.
     */
    USkeleton *ResolveSkeletonFromMesh(USkeletalMesh *Mesh)
    {
        return Mesh ? Mesh->GetSkeleton() : nullptr;
    }
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    if (AnimInstance)
    {
        UObjectManager::Get().DestroyObject(AnimInstance);
        AnimInstance = nullptr;
    }
}

void USkeletalMeshComponent::EnsureAnimInstance()
{
    if (AnimInstance) return;
    switch (AnimationMode)
    {
    case EAnimationMode::AnimationStateMachine:
        AnimInstance = UObjectManager::Get().CreateObject<UAnimStateMachineInstance>(this);
        break;
    case EAnimationMode::AnimationSingleNode:
    default:
        AnimInstance = UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>(this);
        break;
    }
    AnimInstance->InitializeAnimation(ResolveSkeletonFromMesh(SkeletalMesh));
}

void USkeletalMeshComponent::PlayAnimation(UAnimationAsset *NewAnimToPlay, bool bLooping)
{
    AnimToPlay = NewAnimToPlay;

    EnsureAnimInstance();

	SetAnimation(AnimToPlay);

    AnimInstance->SetLooping(bLooping);
    AnimInstance->ResetTime();
    AnimInstance->SetPaused(false);

    // 외부 호환 mirror 멤버 동기화.
    bBakedAnimLooping  = bLooping;
    BakedAnimTime      = 0.0f;
    bBakedAnimPaused   = false;
}

void USkeletalMeshComponent::SetAnimation(UAnimationAsset *NewAnimToPlay)
{
    AnimToPlay = NewAnimToPlay;

    EnsureAnimInstance();

    if (auto *Single = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        Single->SetAnimation(Cast<UAnimSequence>(NewAnimToPlay));
    }
}

void USkeletalMeshComponent::Play(bool bLooping)
{
    if (AnimInstance)
    {
        AnimInstance->SetLooping(bLooping);
        AnimInstance->SetPaused(false);
    }
    bBakedAnimLooping = bLooping;
    bBakedAnimPaused  = false;
}

void USkeletalMeshComponent::Stop()
{
    if (AnimInstance)
    {
        AnimInstance->SetPaused(true);
    }
    bBakedAnimPaused = true;
}

bool USkeletalMeshComponent::EvaluateAnimationPose(const UAnimSequence *Sequence, float TimeSeconds)
{
    // OQ3 선택지 B: 시그니처는 유지하고 내부에서 AnimInstance->EvaluateGraph()에 위임.
    if (!Sequence)
    {
        return false;
    }

    EnsureAnimInstance();

    auto *Single = Cast<UAnimSingleNodeInstance>(AnimInstance);
    if (!Single)
    {
        return false;
    }

    // 시퀀스가 다르면 갈아끼우기. UAnimSingleNodeInstance::SetAnimation은 ref만 잡으므로
    // 외부 const를 보존하기 위한 const_cast가 불가피하다(엔진 관습상 비 const 포인터 보관).
    if (Single->GetAnimation() != Sequence)
    {
        Single->SetAnimation(const_cast<UAnimSequence *>(Sequence));
    }

    Single->SetPaused(true);                 // 평가만 — 시간 누적은 회피.
    Single->SetEvaluationTime(TimeSeconds);  // CurrentTime/PreviousTime 직접 세팅.
	RefreshAnimationPose();
    return true;
}

void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType,
										   FActorComponentTickFunction &ThisTickFunction)
{
	if (!AnimInstance)
	{
		return;
	}

	AnimInstance->Update(DeltaTime);
	AnimInstance->EvaluateGraph();

	// 시퀀스 평가 결과를 적용. override 마스크가 켜진 본은 사용자 값을 유지
	ApplyEvaluatedPose(AnimInstance->GetOutputLocalPose());

	// 외부 호환 mirror.
	BakedAnimTime = AnimInstance->GetCurrentTime();
}

void USkeletalMeshComponent::RefreshAnimationPose()
{
	if (!AnimInstance)
	{
		return;
	}

	AnimInstance->EvaluateGraph();
	ApplyEvaluatedPose(AnimInstance->GetOutputLocalPose());
}
