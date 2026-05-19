#include "Asset/Animation/Core/AnimSingleNodeInstance.h"

#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Core/Skeleton.h"
#include "Core/Log.h"
#include "Object/ObjectFactory.h"

IMPLEMENT_CLASS(UAnimSingleNodeInstance, UAnimInstance)

UAnimSingleNodeInstance::UAnimSingleNodeInstance() = default;

void UAnimSingleNodeInstance::SetAnimation(UAnimSequence *InSequence)
{
    CurrentSequence = InSequence;
    SequencePlayer.SetSequence(Skeleton, CurrentSequence);
    ResetTime();
}

void UAnimSingleNodeInstance::InitializeAnimation(USkeleton *InSkeleton)
{
    UAnimInstance::InitializeAnimation(InSkeleton);
    // 시퀀스가 이미 set 된 상태에서 스켈레톤이 늦게 들어온 경우를 위해 노드에 다시 setting한다.
    if (CurrentSequence)
    {
        SequencePlayer.SetSequence(Skeleton, CurrentSequence);
    }
}

void UAnimSingleNodeInstance::EvaluateGraph()
{
    // graph 트리를 우회해 단일 SequencePlayer를 직접 호출한다. base AnimGraphPtr은 미사용.
    if (!Skeleton)
    {
        OutputLocalPose.clear();
        return;
    }

    const size_t BoneCount = Skeleton->GetBones().size();
    if (OutputLocalPose.size() != BoneCount)
    {
        OutputLocalPose.resize(BoneCount);
    }

    FAnimEvalContext Ctx;
    Ctx.Skeleton       = Skeleton;
    Ctx.TimeSeconds    = CurrentTime;
    Ctx.DeltaTime      = LastDeltaTime;
    Ctx.OwningInstance = this;

    SequencePlayer.Evaluate(Ctx, OutputLocalPose);

    // [TEMP DIAG — root_rotation_coordsys_verification] 검증 후 제거할 것.
    {
        static bool bLogged = false;
        if (!bLogged && !OutputLocalPose.empty() && Skeleton && !Skeleton->GetBones().empty())
        {
            const FBoneInfo& Root = Skeleton->GetBones()[0];
            const FVector BindEuler = FQuat::FromMatrix(Root.LocalBindPose).GetNormalized().ToRotator().ToVector();
            const FVector AnimEuler = OutputLocalPose[0].Rotation.GetNormalized().ToRotator().ToVector();
            UE_LOG("[DIAG][root_rotation_coordsys_verification] Bone0='%s' "
                   "bind_euler_xyz=(%.3f, %.3f, %.3f) anim_t0_euler_xyz=(%.3f, %.3f, %.3f)",
                   Root.Name.c_str(),
                   BindEuler.X, BindEuler.Y, BindEuler.Z,
                   AnimEuler.X, AnimEuler.Y, AnimEuler.Z);
            bLogged = true;
        }
    }
}

float UAnimSingleNodeInstance::GetEffectivePlayLength() const
{
    return CurrentSequence ? CurrentSequence->GetPlayLength() : 0.0f;
}

const TArray<FAnimNotifyEvent> *UAnimSingleNodeInstance::GetActiveNotifies() const
{
    return CurrentSequence ? &CurrentSequence->GetNotifies() : nullptr;
}
