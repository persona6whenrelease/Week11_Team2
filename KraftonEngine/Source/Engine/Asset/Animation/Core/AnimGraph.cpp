#include "Asset/Animation/Core/AnimGraph.h"

#include "Asset/Animation/Core/AnimPoseUtils.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Core/AnimationTypes.h"
#include "Asset/Animation/Core/Skeleton.h"
#include "Math/Quat.h"
#include "Math/Vector.h"

#include <algorithm>
#include <cmath>

void AnimGraph::Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose)
{
    if (!Root)
    {
        AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, OutLocalPose);
        return;
    }
    Root->Evaluate(Ctx, OutLocalPose);
}

void FAnimGraphNode_SequencePlayer::SetSequence(const USkeleton *InSkeleton, const UAnimSequence *InSequence)
{
    Sequence  = InSequence;
    DataModel = InSequence ? InSequence->GetDataModel() : nullptr;

    TrackToBoneIndex.clear();

    if (!InSkeleton || !Sequence || !Sequence->IsValidSequence())
    {
        return; // 캐시 빌드 불가 — 평가 시 bind pose.
    }

    const TArray<FBoneAnimationTrack> &Tracks = DataModel->GetBoneAnimationTracks();
    TrackToBoneIndex.resize(Tracks.size());
    for (size_t TIdx = 0; TIdx < Tracks.size(); ++TIdx)
    {
        const FString BoneName = Tracks[TIdx].Name.ToString();
        TrackToBoneIndex[TIdx] = InSkeleton->FindBoneIndexByName(BoneName);
    }
}

void FAnimGraphNode_SequencePlayer::Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose)
{
    const UAnimDataModel *Model    = this->DataModel;
    const USkeleton      *Skeleton = Ctx.Skeleton;

    if (!Model || !Skeleton)
    {
        AnimPoseUtils::FillBindPoseTransforms(Skeleton, OutLocalPose);
        return;
    }

    const int32 NumKeys = Model->GetNumberOfFrames();
    const float FPS     = Model->GetFrameRate().AsDecimal();
    if (NumKeys <= 0 || FPS <= 0.0f)
    {
        AnimPoseUtils::FillBindPoseTransforms(Skeleton, OutLocalPose);
        return;
    }

    // 1) 누락 트랙 대비 — 먼저 bind pose로 채운다.
    AnimPoseUtils::FillBindPoseTransforms(Skeleton, OutLocalPose);
    const TArray<FBoneInfo> &Bones = Skeleton->GetBones();

    if (TrackToBoneIndex.empty())
    {
        return; // 캐시 없음 — bind pose 유지.
    }
    const TArray<int32> &Track2Bone = this->TrackToBoneIndex;

    const TArray<FBoneAnimationTrack> &Tracks = Model->GetBoneAnimationTracks();
    if (Track2Bone.size() != Tracks.size())
    {
        return; // 캐시 정합성 깨짐 — bind pose 유지.
    }

    // 2) FrameA / FrameB / Blend 계산.
    int32 FrameA = 0;
    int32 FrameB = 0;
    float Blend  = 0.0f;
    if (NumKeys == 1)
    {
        FrameA = FrameB = 0;
        Blend = 0.0f;
    }
    else
    {
        const float FrameFloat = Ctx.TimeSeconds * FPS;
        int32       FrameAInt  = static_cast<int32>(std::floor(FrameFloat));
        FrameAInt              = std::max(0, std::min(FrameAInt, NumKeys - 1));
        FrameA                 = FrameAInt;
        FrameB                 = std::min(FrameA + 1, NumKeys - 1);
        Blend                  = FrameFloat - static_cast<float>(FrameA);
        if (Blend < 0.0f) Blend = 0.0f;
        if (Blend > 1.0f) Blend = 1.0f;
    }

    // 3) 트랙별 TRS 분리 키 보간 (Option B — LocalMatrixKeys 미사용).
    for (size_t TIdx = 0; TIdx < Tracks.size(); ++TIdx)
    {
        const int32 BoneIdx = Track2Bone[TIdx];
        if (BoneIdx < 0 || static_cast<size_t>(BoneIdx) >= Bones.size())
        {
            continue;
        }

        const FRawAnimSequenceTrack &Raw = Tracks[TIdx].InternalTrack;

        const bool bHasPos   = Raw.PosKeys.size()   >= static_cast<size_t>(NumKeys);
        const bool bHasRot   = Raw.RotKeys.size()   >= static_cast<size_t>(NumKeys);
        const bool bHasScale = Raw.ScaleKeys.size() >= static_cast<size_t>(NumKeys);

        // 세 키 배열 중 하나라도 NumKeys를 못 채우면 bind pose 유지(step 1).
        if (!bHasPos || !bHasRot || !bHasScale)
        {
            continue;
        }

        const FVector P = FVector::Lerp(Raw.PosKeys[FrameA],   Raw.PosKeys[FrameB],   Blend);
        const FQuat   R = FQuat::Slerp (Raw.RotKeys[FrameA],   Raw.RotKeys[FrameB],   Blend);
        const FVector S = FVector::Lerp(Raw.ScaleKeys[FrameA], Raw.ScaleKeys[FrameB], Blend);

        OutLocalPose[BoneIdx] = FTransform(P, R, S);
    }
}

void FAnimGraphNode_Blend2::Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose)
{
    const size_t N = Ctx.Skeleton ? Ctx.Skeleton->GetBones().size() : 0;
    if (N == 0)
    {
        OutLocalPose.clear();
        return;
    }

    OutLocalPose.resize(N);
    ScratchA.resize(N);
    ScratchB.resize(N);

    if (ChildA) ChildA->Evaluate(Ctx, ScratchA);
    else        AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, ScratchA);

    if (ChildB) ChildB->Evaluate(Ctx, ScratchB);
    else        AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, ScratchB);

    float A = Alpha;
    if (A < 0.0f) A = 0.0f;
    if (A > 1.0f) A = 1.0f;

    for (size_t i = 0; i < N; ++i)
    {
        OutLocalPose[i] = AnimPoseUtils::BlendTransform(ScratchA[i], ScratchB[i], A);
    }
}

void FAnimGraphNode_BlendN::Evaluate(const FAnimEvalContext &Ctx, TArray<FTransform> &OutLocalPose)
{
    const size_t N           = Ctx.Skeleton ? Ctx.Skeleton->GetBones().size() : 0;
    const size_t NumChildren = Children.size();
    if (N == 0 || NumChildren == 0)
    {
        AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, OutLocalPose);
        return;
    }

    OutLocalPose.resize(N);
    if (ChildScratches.size() != NumChildren)
    {
        ChildScratches.resize(NumChildren);
    }

    // 1) 자식 평가 (nullptr이면 bind pose 안전망)
    for (size_t c = 0; c < NumChildren; ++c)
    {
        ChildScratches[c].resize(N);
        if (Children[c]) Children[c]->Evaluate(Ctx, ChildScratches[c]);
        else             AnimPoseUtils::FillBindPoseTransforms(Ctx.Skeleton, ChildScratches[c]);
    }

    const TArray<FBoneInfo> &Bones = Ctx.Skeleton->GetBones();

    // 2) 본별 누적
    for (size_t i = 0; i < N; ++i)
    {
        const float W0 = (0 < Weights.size()) ? std::max(Weights[0], 0.0f) : 0.0f;

        float   SumW     = W0;
        FVector PosAcc   = ChildScratches[0][i].Location * W0;
        FVector ScaleAcc = ChildScratches[0][i].Scale    * W0;
        FQuat   RotAcc   = ChildScratches[0][i].Rotation;

        for (size_t c = 1; c < NumChildren; ++c)
        {
            const float w = (c < Weights.size()) ? std::max(Weights[c], 0.0f) : 0.0f;
            if (w <= 0.0f) continue;

            SumW     += w;
            PosAcc   += ChildScratches[c][i].Location * w;
            ScaleAcc += ChildScratches[c][i].Scale    * w;

            const float SlerpAlpha = w / SumW;
            RotAcc = FQuat::Slerp(RotAcc, ChildScratches[c][i].Rotation, SlerpAlpha);
        }

        if (SumW <= 0.0f)
        {
            OutLocalPose[i] = AnimPoseUtils::BindPoseToTransform(Bones[i].LocalBindPose);
        }
        else
        {
            const float Inv = 1.0f / SumW;
            OutLocalPose[i] = FTransform(PosAcc * Inv, RotAcc, ScaleAcc * Inv);
        }
    }
}
