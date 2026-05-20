#include "Asset/Animation/Core/AnimInstance.h"

#include "Asset/Animation/Core/AnimPoseUtils.h"
#include "Asset/Animation/Core/Skeleton.h"
#include "Camera/PlayerCameraManager.h"
#include "Component/SkeletalMeshComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/World.h"
#include "Object/ObjectFactory.h"
#include "Sound/SoundManager.h"

#include <cmath>

REGISTER_FACTORY(UAnimInstance)

UAnimInstance::UAnimInstance() = default;
UAnimInstance::~UAnimInstance() = default;

void UAnimInstance::InitializeAnimation(USkeleton *InSkeleton)
{
    Skeleton = InSkeleton;
    ResetTime();
    TriggeredNotifiesThisFrame.clear();

    FillBindPose();
}

void UAnimInstance::Update(float DeltaTime)
{
    LastDeltaTime = bPaused ? 0.0f : DeltaTime;
    TriggeredNotifiesThisFrame.clear();

    const float Length = GetEffectivePlayLength();
    if (Length <= 0.0f)
    {
        PreviousTime = CurrentTime = 0.0f;
        return;
    }

    if (bPaused)
    {
        // Paused 상태에서도 PreviousTime을 정렬해 두면 외부에서 시간을 강제 변경한 직후
        // Notify 판정에 잘못된 prev/curr 간격이 잡히지 않는다.
        PreviousTime = CurrentTime;
        return;
    }

    PreviousTime = CurrentTime;
    float NewTime = CurrentTime + DeltaTime * PlaybackSpeed;

    if (bLooping)
    {
        float Wrapped = std::fmod(NewTime, Length);
        if (Wrapped < 0.0f)
        {
            Wrapped += Length;
        }
        CurrentTime = Wrapped;
    }
    else
    {
        if (NewTime >= Length)
        {
            CurrentTime = Length;
            bPaused = true;
        }
        else if (NewTime < 0.0f)
        {
            CurrentTime = 0.0f;
            bPaused = true;
        }
        else
        {
            CurrentTime = NewTime;
        }
    }

    // Notify 판정 — IsTriggeredBetween이 prev > curr 루프 wrap 케이스를 처리한다.
    // LocalTriggered는 함수-로컬: Notifies 배열은 Update 도중 재할당되지 않으므로 element 주소 보관 안전.
    const TArray<FAnimNotifyEvent> *Notifies = GetActiveNotifies();
    TArray<const FAnimNotifyEvent *> LocalTriggered;
    if (Notifies)
    {
        for (const FAnimNotifyEvent &Notify : *Notifies)
        {
            if (Notify.IsTriggeredBetween(PreviousTime, CurrentTime, Length))
            {
                TriggeredNotifiesThisFrame.push_back(Notify.NotifyName); // 기존 동작 보존 (D8, Lua)
                LocalTriggered.push_back(&Notify);
            }
        }
    }

    if (!LocalTriggered.empty())
    {
        DispatchTriggeredNotifies(LocalTriggered);
    }
}

void UAnimInstance::DispatchTriggeredNotifies(const TArray<const FAnimNotifyEvent *> &InTriggered)
{
    for (const FAnimNotifyEvent *NotifyPtr : InTriggered)
    {
        if (!NotifyPtr) continue;
        const FAnimNotifyEvent &Notify = *NotifyPtr;

        switch (Notify.Type)
        {
        case EAnimNotifyType::None:
            // D3: v3 백필 항목 — skip
            continue;

        case EAnimNotifyType::Sound:
            // D6: 미등록/empty SoundId는 SoundManager 측에서 UE_LOG + no-op. 사전 검증 없음.
            FSoundManager::Get().PlayEffect(Notify.SoundId);
            break;

        case EAnimNotifyType::CameraShake:
        {
            APlayerCameraManager *CamMgr = ResolveCameraManager();
            if (CamMgr)
            {
                CamMgr->StartCameraShake(Notify.ShakeParams);
            }
            // CamMgr == nullptr 이면 silent no-op (R2, R5)
            break;
        }

        default:
            // 알 수 없는 type (forward-compat): 무시.
            break;
        }
    }
}

APlayerCameraManager *UAnimInstance::ResolveCameraManager() const
{
    // 단계 1: AnimInstance → SkeletalMeshComponent (Outer 체인)
    USkeletalMeshComponent *Comp = GetTypedOuter<USkeletalMeshComponent>();
    if (!Comp) return nullptr;

    // 단계 2: Component → Actor
    AActor *Owner = Comp->GetOwner();
    if (!Owner) return nullptr;

    // 단계 3: Actor → World
    UWorld *World = Owner->GetWorld();
    if (!World) return nullptr;

    // 단계 4: World → PlayerController (index 0)
    APlayerController *PC = World->GetPlayerController(0);
    if (!PC) return nullptr;

    // 단계 5: PC → CameraManager (nullable)
    return PC->GetCameraManagerPtr();
}

void UAnimInstance::EvaluateGraph()
{
    if (!Skeleton || !AnimGraphPtr)
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

    AnimGraphPtr->Evaluate(Ctx, OutputLocalPose);
}

void UAnimInstance::FillBindPose()
{
    AnimPoseUtils::FillBindPoseTransforms(Skeleton, OutputLocalPose);
}
