#include "Component/SkeletalMeshComponent.h"

#include "Asset/Animation/Core/AnimInstance.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Core/AnimSingleNodeInstance.h"
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

void USkeletalMeshComponent::PlayAnimation(UAnimationAsset *NewAnimToPlay, bool bLooping)
{
    AnimToPlay = NewAnimToPlay;

    if (!AnimInstance)
    {
        // 현재 enum에는 AnimationSingleNode 한 가지만 존재 — 그대로 SingleNodeInstance 생성.
        AnimInstance = UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>(this);
        AnimInstance->InitializeAnimation(ResolveSkeletonFromMesh(SkeletalMesh));
    }

    if (auto *Single = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        Single->SetAnimation(Cast<UAnimSequence>(NewAnimToPlay));
    }

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

    if (!AnimInstance)
    {
        AnimInstance = UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>(this);
        AnimInstance->InitializeAnimation(ResolveSkeletonFromMesh(SkeletalMesh));
    }

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
    Single->EvaluateGraph();

    // 결과를 부모 컴포넌트의 LocalBonePoseMatrices 버퍼로 반영.
    LocalBonePoseMatrices = Single->GetOutputLocalPose();
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

    // AnimInstance가 산출한 로컬 포즈를 부모 버퍼로 복사 — RebuildMeshSpaceBoneMatrices가 이 버퍼를 읽는다.
    LocalBonePoseMatrices = AnimInstance->GetOutputLocalPose();
    RebuildMeshSpaceBoneMatrices();

    // 외부 호환 mirror.
    BakedAnimTime = AnimInstance->GetCurrentTime();
}

void USkeletalMeshComponent::ApplyDebugRandomBoneAnimation(float /*DeltaTime*/)
{
    // 디버그 helper — 파트 2 본문에서는 구현하지 않는다.
}

bool USkeletalMeshComponent::ApplyBakedAnimation(float /*DeltaTime*/)
{
    // TickComponent가 AnimInstance 경로로 대체. 외부 호환을 위해 시그니처만 유지.
    return false;
}
