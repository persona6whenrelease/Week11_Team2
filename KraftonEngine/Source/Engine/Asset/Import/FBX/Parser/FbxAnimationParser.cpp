/**
 * FBX 애니메이션 데이터를 프레임 샘플 기반 클립으로 변환한다.
 *
 * FBX animation stack과 layer를 기준으로 본의 로컬 변환을 일정 프레임 간격으로 평가한다. 결과는
 * 스켈레톤별 FAnimationClip 배열로 저장되어 AnimSequence 에셋 생성에 사용된다.
 */

#include "Asset/Import/FBX/Parser/FbxAnimationParser.h"

#include "Core/Log.h"
#include "Asset/Import/FBX/Core/FBXUtil.h"

#include <fbxsdk.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    template <typename T> bool IsValidIndex(const TArray<T> &Items, int32 Index)
    {
        return Index >= 0 && static_cast<size_t>(Index) < Items.size();
    }
}

void FFbxAnimationParser::ParseSkeletonAnimations(fbxsdk::FbxScene       *Scene,
                                                  const FFbxSkeletonMeta &SkeletonMeta,
                                                  const TArray<FBoneInfo>& SkeletonBones,
                                                  TArray<FAnimationClip>& OutAnimationClips,
                                                  float SampleRate) const
{
    OutAnimationClips.clear();

    if (!Scene || !SkeletonMeta.bValid || SkeletonMeta.BoneIds.empty())
    {
        return;
    }

    if (SampleRate <= 0.0f)
    {
        SampleRate = 30.0f;
    }

    const int32       BoneCount = static_cast<int32>(SkeletonMeta.BoneIds.size());
    TArray<FbxNode *> BoneNodes;
    BoneNodes.resize(BoneCount, nullptr);
    for (int32 SkeletonBoneIndex = 0; SkeletonBoneIndex < BoneCount; ++SkeletonBoneIndex)
    {
        const int32 BoneId = SkeletonMeta.BoneIds[SkeletonBoneIndex];
        if (IsValidIndex(ImportMeta.Bones, BoneId))
        {
            BoneNodes[SkeletonBoneIndex] = ImportMeta.Bones[BoneId].Node;
        }
    }

    const int32 TotalSrcObjectCount = Scene->GetSrcObjectCount();
    for (int32 ObjIndex = 0; ObjIndex < TotalSrcObjectCount; ++ObjIndex)
    {
        FbxObject *Obj = Scene->GetSrcObject(ObjIndex);
        if (!Obj)
        {
            continue;
        }
        const char *ClassName = Obj->GetClassId().GetName();
        if (!ClassName)
        {
            continue;
        }

        if (std::strcmp(ClassName, "AnimStack") != 0 && std::strcmp(ClassName, "FbxAnimStack") != 0)
        {
            continue;
        }
        FbxAnimStack *AnimStack = static_cast<FbxAnimStack *>(Obj);

        Scene->SetCurrentAnimationStack(AnimStack);

        FbxTimeSpan  TimeSpan;
        FbxTakeInfo *TakeInfo = Scene->GetTakeInfo(AnimStack->GetName());
        if (TakeInfo)
        {
            TimeSpan = TakeInfo->mLocalTimeSpan;
        }
        else
        {
            TimeSpan = AnimStack->GetLocalTimeSpan();
        }

        const FbxTime StartTime = TimeSpan.GetStart();
        const FbxTime StopTime = TimeSpan.GetStop();
        const double  DurationSeconds = std::max(0.0, (StopTime - StartTime).GetSecondDouble());
        if (DurationSeconds <= 0.0)
        {
            continue;
        }

        const int32 FrameCount =
            std::max(2, static_cast<int32>(std::round(DurationSeconds * SampleRate)) + 1);

        FAnimationClip Clip;
        Clip.Name = AnimStack->GetName() ? AnimStack->GetName() : "Anim";
        Clip.Duration = static_cast<float>(DurationSeconds);
        Clip.FrameRate = SampleRate;
        Clip.FrameCount = FrameCount;
        Clip.Tracks.resize(BoneCount);
        for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
        {
            Clip.Tracks[BoneIndex].BoneIndex = BoneIndex;
            Clip.Tracks[BoneIndex].Samples.resize(FrameCount);
        }

        TArray<FMatrix> BoneGlobals;
        BoneGlobals.resize(BoneCount, FMatrix::Identity);

        for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
        {
            const double FrameAlpha = (FrameCount > 1) ? static_cast<double>(FrameIndex) /
                                                             static_cast<double>(FrameCount - 1)
                                                       : 0.0;
            const double FrameSeconds = FrameAlpha * DurationSeconds;
            FbxTime      SampleTime;
            SampleTime.SetSecondDouble(StartTime.GetSecondDouble() + FrameSeconds);

            for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
            {
                FbxNode *Node = BoneNodes[BoneIndex];
                if (!Node)
                {
                    BoneGlobals[BoneIndex] = FMatrix::Identity;
                    continue;
                }
                BoneGlobals[BoneIndex] =
                    FBXUtil::ConvertFbxMatrix(Node->EvaluateGlobalTransform(SampleTime));
            }

            for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
            {
                const int32 ParentIndex = (BoneIndex < static_cast<int32>(SkeletonBones.size()))
                                              ? SkeletonBones[BoneIndex].ParentIndex
                                              : -1;

                FMatrix LocalMatrix;
                if (ParentIndex >= 0 && ParentIndex < BoneCount)
                {
                    LocalMatrix = BoneGlobals[BoneIndex] * BoneGlobals[ParentIndex].GetInverse();
                }
                else
                {
                    LocalMatrix = BoneGlobals[BoneIndex];
                }

                Clip.Tracks[BoneIndex].Samples[FrameIndex].LocalMatrix = LocalMatrix;
            }
        }

        UE_LOG("[FBXImporter] Baked animation clip. Skeleton=%d Name=%s Duration=%.3fs Frames=%d "
               "Bones=%d",
               SkeletonMeta.SkeletonId, Clip.Name.c_str(), Clip.Duration, Clip.FrameCount,
               BoneCount);

        OutAnimationClips.push_back(std::move(Clip));
    }
}
