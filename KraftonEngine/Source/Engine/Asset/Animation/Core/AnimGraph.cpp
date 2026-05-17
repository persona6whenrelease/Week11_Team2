#include "Asset/Animation/Core/AnimGraph.h"

#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Core/AnimationTypes.h"
#include "Asset/Animation/Core/Skeleton.h"
#include "Math/Quat.h"
#include "Math/Transform.h"
#include "Math/Vector.h"

#include <algorithm>
#include <cmath>

namespace
{
    /**
     * Skeleton 본 수에 맞춰 OutLocalPose를 bind pose로 초기화한다.
     * 트랙이 누락된 본도 자연스러운 fallback을 가지도록 평가 시작 전에 항상 호출한다.
     */
    void FillBindPose(const USkeleton *Skeleton, TArray<FMatrix> &OutLocalPose)
    {
        if (!Skeleton)
        {
            OutLocalPose.clear();
            return;
        }
        const TArray<FBoneInfo> &Bones = Skeleton->GetBones();
        OutLocalPose.resize(Bones.size());
        for (size_t i = 0; i < Bones.size(); ++i)
        {
            OutLocalPose[i] = Bones[i].LocalBindPose;
        }
    }
}

void AnimGraph::Evaluate(const FAnimEvalContext &Ctx, TArray<FMatrix> &OutLocalPose)
{
    if (!Root)
    {
        FillBindPose(Ctx.Skeleton, OutLocalPose);
        return;
    }
    Root->Evaluate(Ctx, OutLocalPose);
}

void FAnimGraphNode_SequencePlayer::Evaluate(const FAnimEvalContext &Ctx, TArray<FMatrix> &OutLocalPose)
{
    const UAnimDataModel *Model    = Ctx.DataModel;
    const USkeleton      *Skeleton = Ctx.Skeleton;

    if (!Model || !Skeleton)
    {
        FillBindPose(Skeleton, OutLocalPose);
        return;
    }

    const int32 NumKeys = Model->GetNumberOfKeys();
    const float FPS     = Model->GetFrameRate().AsDecimal();
    if (NumKeys <= 0 || FPS <= 0.0f)
    {
        FillBindPose(Skeleton, OutLocalPose);
        return;
    }

    // 1) 누락 트랙 대비 — 먼저 bind pose로 채운다.
    FillBindPose(Skeleton, OutLocalPose);
    const TArray<FBoneInfo> &Bones = Skeleton->GetBones();

    if (!Ctx.TrackToBoneIndex)
    {
        return; // 캐시 없음 — bind pose 유지.
    }
    const TArray<int32> &Track2Bone = *Ctx.TrackToBoneIndex;

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

        OutLocalPose[BoneIdx] = FTransform(P, R, S).ToMatrix();
    }
}
