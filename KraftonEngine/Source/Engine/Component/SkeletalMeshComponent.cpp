#include "Component/SkeletalMeshComponent.h"

#include "Core/Log.h"
#include "Asset/Animation/Core/AnimGraphInstance.h"
#include "Asset/Animation/Core/AnimInstance.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Core/AnimSingleNodeInstance.h"
#include "Asset/Animation/Core/AnimStateMachineInstance.h"
#include "Asset/Animation/Core/Skeleton.h"
#include "Asset/Import/FBX/Types/FBXSceneAsset.h"
#include "Asset/Import/MeshManager.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Object/Object.h"
#include "Object/ObjectFactory.h"
#include "Profiling/PlatformTime.h"
#include "Profiling/SkinningStats.h"
#include "Serialization/Archive.h"

#include <cmath>

REGISTER_FACTORY(USkeletalMeshComponent)

namespace
{
    const char* GAnimationModeNames[] = {
        "Single Node",
        "State Machine",
        "Graph"
    };

    /**
     * SkeletalMesh -> USkeleton 경로 helper. 메시 또는 스켈레톤이 비었으면 nullptr.
     */
    USkeleton *ResolveSkeletonFromMesh(USkeletalMesh *Mesh)
    {
        return Mesh ? Mesh->GetSkeleton() : nullptr;
    }

    UFBXSceneAsset* ResolveSceneAssetFromMesh(const USkeletalMesh* Mesh)
    {
        return Mesh ? Mesh->GetTypedOuter<UFBXSceneAsset>() : nullptr;
    }

    bool TryBuildAnimReferencePath(const USkeletalMesh* Mesh, const UAnimSequence* Sequence, FString& OutPath)
    {
        OutPath.clear();

        UFBXSceneAsset* SceneAsset = ResolveSceneAssetFromMesh(Mesh);
        if (!SceneAsset || !Mesh || !Sequence)
        {
            return false;
        }

        const int32 SequenceCount = FMeshManager::GetAnimSequenceCountForSkeletalMesh(SceneAsset, Mesh);
        for (int32 SequenceIndex = 0; SequenceIndex < SequenceCount; ++SequenceIndex)
        {
            FString CandidatePath;
            UAnimSequence* Candidate = FMeshManager::FindAnimSequenceForSkeletalMesh(SceneAsset, Mesh, SequenceIndex, &CandidatePath);
            if (Candidate == Sequence)
            {
                OutPath = CandidatePath;
                return !OutPath.empty();
            }
        }

        return false;
    }

    UAnimSequence* ResolveCompatibleAnimSequence(USkeletalMesh* Mesh, const FString& AnimPath)
    {
        if (!Mesh || AnimPath.empty() || AnimPath == "None")
        {
            return nullptr;
        }

        UAnimSequence* Sequence = FMeshManager::ResolveAnimSequenceReference(AnimPath);
        if (!Sequence)
        {
            return nullptr;
        }

        const FSkeletalMesh* MeshAsset = Mesh->GetSkeletalMeshAsset();
        if (!MeshAsset || MeshAsset->SkeletonAssetPath.empty())
        {
            return nullptr;
        }

        return Sequence->GetSkeletonAssetPath() == MeshAsset->SkeletonAssetPath ? Sequence : nullptr;
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
    case EAnimationMode::AnimationGraph:
        AnimInstance = UObjectManager::Get().CreateObject<UAnimGraphInstance>(this);
        break;
    case EAnimationMode::AnimationSingleNode:
    default:
        AnimInstance = UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>(this);
        break;
    }
    AnimInstance->InitializeAnimation(ResolveSkeletonFromMesh(SkeletalMesh));
    AnimInstance->SetLooping(bBakedAnimLooping);
    AnimInstance->SetPlaybackSpeed(BakedAnimPlaybackSpeed);
    AnimInstance->SetPaused(bBakedAnimPaused);
    AnimInstance->SetEvaluationTime(BakedAnimTime);
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
    AnimToPlayPath = "None";

    if (const UAnimSequence* Sequence = Cast<UAnimSequence>(NewAnimToPlay))
    {
        FString ResolvedPath;
        if (TryBuildAnimReferencePath(GetSkeletalMesh(), Sequence, ResolvedPath))
        {
            AnimToPlayPath = ResolvedPath;
        }
    }

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

void USkeletalMeshComponent::BeginPlay()
{
    UActorComponent::BeginPlay();

    if (AnimationMode != EAnimationMode::AnimationSingleNode || !AnimToPlay)
    {
        return;
    }

    PlayAnimation(AnimToPlay, bBakedAnimLooping);
    SetBakedAnimPlaybackSpeed(BakedAnimPlaybackSpeed);
}

void USkeletalMeshComponent::Serialize(FArchive& Ar)
{
    USkinnedMeshComponent::Serialize(Ar);
    Ar << AnimToPlayPath;
    int32 AnimationModeValue = static_cast<int32>(AnimationMode);
    Ar << AnimationModeValue;
    if (Ar.IsLoading())
    {
        AnimationMode = static_cast<EAnimationMode>(AnimationModeValue);
    }
}

void USkeletalMeshComponent::PostDuplicate()
{
    USkinnedMeshComponent::PostDuplicate();

    if (AnimInstance)
    {
        UObjectManager::Get().DestroyObject(AnimInstance);
        AnimInstance = nullptr;
    }

    if (UAnimSequence* Sequence = ResolveCompatibleAnimSequence(GetSkeletalMesh(), AnimToPlayPath))
    {
        SetAnimation(Sequence);
        SetBakedAnimTime(0.0f);
        SetBakedAnimPaused(true);
    }
    else
    {
        SetAnimation(nullptr);
    }
}

void USkeletalMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    USkinnedMeshComponent::GetEditableProperties(OutProps);

    static const FPropertyTypeDesc AnimationModeEnumType{
        EPropertyType::Enum,
        nullptr,
        GAnimationModeNames,
        static_cast<uint32>(std::size(GAnimationModeNames))
    };

    static const FPropertyTypeDesc AnimSequenceObjectRefType{
        EPropertyType::ObjectRef,
        nullptr,
        nullptr,
        0,
        &UAnimSequence::StaticClassInstance
    };

    FPropertyDescriptor AnimationModeProp;
    AnimationModeProp.ValuePtr = &AnimationMode;
    AnimationModeProp.SyntheticTypeDesc = &AnimationModeEnumType;
    AnimationModeProp.DynamicName = "Animation Mode";
    OutProps.push_back(std::move(AnimationModeProp));

    FPropertyDescriptor AnimationProp;
    AnimationProp.ValuePtr = &AnimToPlayPath;
    AnimationProp.SyntheticTypeDesc = &AnimSequenceObjectRefType;
    AnimationProp.DynamicName = "Animation";
    OutProps.push_back(std::move(AnimationProp));
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
    USkinnedMeshComponent::PostEditProperty(PropertyName);

    if (std::strcmp(PropertyName, "Animation Mode") == 0)
    {
        if (AnimInstance)
        {
            UObjectManager::Get().DestroyObject(AnimInstance);
            AnimInstance = nullptr;
        }

        if (AnimationMode == EAnimationMode::AnimationSingleNode)
        {
            SetAnimation(ResolveCompatibleAnimSequence(GetSkeletalMesh(), AnimToPlayPath));
        }
        else
        {
            AnimToPlay = nullptr;
            EnsureAnimInstance();
        }

        SetBakedAnimTime(0.0f);
        SetBakedAnimPaused(true);
        return;
    }

    if (std::strcmp(PropertyName, "Animation") == 0)
    {
        SetAnimation(ResolveCompatibleAnimSequence(GetSkeletalMesh(), AnimToPlayPath));
        SetBakedAnimTime(0.0f);
        SetBakedAnimPaused(true);
        return;
    }

    if (std::strcmp(PropertyName, "Skeletal Mesh") == 0)
    {
        UAnimSequence* CompatibleSequence = ResolveCompatibleAnimSequence(GetSkeletalMesh(), AnimToPlayPath);
        SetAnimation(CompatibleSequence);
        SetBakedAnimTime(0.0f);
        SetBakedAnimPaused(true);
    }
}

void USkeletalMeshComponent::SetRootGraph(std::unique_ptr<FAnimGraphNode_Base> InRoot)
{
    // 정책 (ii): 모드 일치 강제. AnimationGraph가 아니면 외부가 모드를 먼저 set해야 한다 —
    // silent mode swap을 컴포넌트 API가 하지 않는다.
    if (AnimationMode != EAnimationMode::AnimationGraph)
    {
        UE_LOG("SetRootGraph called but AnimationMode is not AnimationGraph — ignored.");
        return;
    }

    EnsureAnimInstance();

    auto *Graph = Cast<UAnimGraphInstance>(AnimInstance);
    if (!Graph)
    {
        UE_LOG("SetRootGraph: AnimInstance is not UAnimGraphInstance — ignored.");
        return;
    }

    Graph->SetRootGraph(std::move(InRoot));
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
    const uint64 PoseSamplingStartCycles = FPlatformTime::Cycles64();
	AnimInstance->EvaluateGraph();
#if STATS
    FSkinningStats::AddPoseSamplingTime(FPlatformTime::ToMilliseconds(FPlatformTime::Cycles64() - PoseSamplingStartCycles) * 0.001);
#endif

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

    const uint64 PoseSamplingStartCycles = FPlatformTime::Cycles64();
	AnimInstance->EvaluateGraph();
#if STATS
    FSkinningStats::AddPoseSamplingTime(FPlatformTime::ToMilliseconds(FPlatformTime::Cycles64() - PoseSamplingStartCycles) * 0.001);
#endif
	ApplyEvaluatedPose(AnimInstance->GetOutputLocalPose());
}
