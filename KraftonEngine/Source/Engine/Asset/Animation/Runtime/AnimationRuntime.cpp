/**
 * 애니메이션 샘플링과 포즈 블렌딩 계산을 구현한다.
 *
 * 현재 구현은 프레임 단위로 저장된 로컬 행렬 샘플을 시간에 맞춰 선형 보간한다. 유효하지 않은 트랙은
 * 스켈레톤의 로컬 바인드 포즈로 대체되므로, 일부 본에 애니메이션 데이터가 없어도 전체 포즈 배열은
 * 항상 스켈레톤 본 개수에 맞춰 생성된다.
 */

#include "Asset/Animation/Runtime/AnimationRuntime.h"

#include <algorithm>
#include <cmath>

namespace
{
    FMatrix LerpMatrix(const FMatrix& A, const FMatrix& B, float Alpha)
    {
        FMatrix Result;
        const float InvAlpha = 1.0f - Alpha;
        for (int32 Row = 0; Row < 4; ++Row)
        {
            for (int32 Col = 0; Col < 4; ++Col)
            {
                Result.M[Row][Col] = A.M[Row][Col] * InvAlpha + B.M[Row][Col] * Alpha;
            }
        }
        return Result;
    }
}

bool FAnimationRuntime::SampleLocalPose(const FAnimationClip& Clip, float TimeSeconds, const TArray<FBoneInfo>& Bones, TArray<FMatrix>& OutLocalPose)
{
    if (Clip.FrameCount <= 0 || Clip.Duration <= 0.0f || Clip.Tracks.empty() || Bones.empty())
    {
        return false;
    }

    float LoopedTime = std::fmod(TimeSeconds, Clip.Duration);
    if (LoopedTime < 0.0f)
    {
        LoopedTime += Clip.Duration;
    }

    const float Alpha = LoopedTime / Clip.Duration;
    const float FrameFloat = Alpha * static_cast<float>(Clip.FrameCount - 1);
    int32 FrameA = static_cast<int32>(std::floor(FrameFloat));
    FrameA = std::clamp(FrameA, 0, Clip.FrameCount - 1);
    int32 FrameB = std::clamp(FrameA + 1, 0, Clip.FrameCount - 1);
    const float Blend = FrameFloat - static_cast<float>(FrameA);

    OutLocalPose.resize(Bones.size(), FMatrix::Identity);
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones.size()); ++BoneIndex)
    {
        OutLocalPose[BoneIndex] = Bones[BoneIndex].LocalBindPose;
        if (BoneIndex >= static_cast<int32>(Clip.Tracks.size()))
        {
            continue;
        }

        const FBoneAnimTrack& Track = Clip.Tracks[BoneIndex];
        if (FrameA >= static_cast<int32>(Track.Samples.size()) || FrameB >= static_cast<int32>(Track.Samples.size()))
        {
            continue;
        }

        OutLocalPose[BoneIndex] = LerpMatrix(Track.Samples[FrameA].LocalMatrix, Track.Samples[FrameB].LocalMatrix, Blend);
    }

    return true;
}

bool FAnimationRuntime::BlendLocalPoses(const TArray<FMatrix>& PoseA, const TArray<FMatrix>& PoseB, float Alpha, TArray<FMatrix>& OutPose)
{
    if (PoseA.empty() || PoseA.size() != PoseB.size())
    {
        return false;
    }

    Alpha = std::clamp(Alpha, 0.0f, 1.0f);
    OutPose.resize(PoseA.size(), FMatrix::Identity);
    for (int32 Index = 0; Index < static_cast<int32>(PoseA.size()); ++Index)
    {
        OutPose[Index] = LerpMatrix(PoseA[Index], PoseB[Index], Alpha);
    }
    return true;
}
