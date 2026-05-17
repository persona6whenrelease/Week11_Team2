/**
 * FBX 애니메이션 데이터를 UAnimSequence 에셋으로 변환한다.
 *
 * FBX animation stack마다 하나의 UAnimSequence를 생성한다. 각 프레임 시간에서 FBX SDK의
 * EvaluateLocalTransform() 결과를 샘플링하여 본별 local transform track으로 bake한다.
 *
 * LclTranslation, LclRotation, LclScaling curve를 직접 조합하지 않고 SDK 평가 결과를 사용하므로
 * RotationOrder, PreRotation, PostRotation, RotationPivot, ScalingPivot 같은 FBX node transform stack이
 * 반영된 local matrix를 얻을 수 있다.
 *
 * Runtime은 이미 bake된 UAnimDataModel만 재생하므로 FBX animation layer, pivot, pre-rotation 같은
 * FBX 전용 컨텍스트를 알 필요가 없다.
 */

// TODO: 다중 레이어를 직접 제어해야 하는 경우, SDK evaluator 대신 layer별 curve 합성 경로를 별도로 구현한다.

#include "Asset/Import/FBX/Parser/FbxAnimationParser.h"

#include "Asset/Import/FBX/Core/FBXUtil.h"
#include "Core/Log.h"
#include "Object/ObjectFactory.h"

#include <fbxsdk.h>

#include <algorithm>
#include <cmath>

namespace
{
    template <typename T>
    bool IsValidIndex(const TArray<T>& Items, int32 Index)
    {
        return Index >= 0 && static_cast<size_t>(Index) < Items.size();
    }

    double SampleCurveOrDefault(FbxAnimCurve* Curve, FbxTime Time, double DefaultValue)
    {
        return Curve ? Curve->Evaluate(Time) : DefaultValue;
    }

    void SampleEvaluatedLocalTransform(FbxNode* Node,
                                       FbxTime Time,
                                       FVector& OutTranslation,
                                       FQuat& OutRotation,
                                       FVector& OutScale)
    {
        if (!Node)
        {
            OutTranslation = FVector(0.0f, 0.0f, 0.0f);
            OutRotation = FQuat::Identity;
            OutScale = FVector(1.0f, 1.0f, 1.0f);
            return;
        }

        // EvaluateLocalTransform(): 현재 animation stack 기준의 local matrix를 평가
        const FbxAMatrix LocalMatrix = Node->EvaluateLocalTransform(Time);

        OutTranslation = FBXUtil::ConvertFbxVector(LocalMatrix.GetT());
        // RotationOrder, PreRotation, PostRotation, Pivot 설정도 반영
        OutRotation = FBXUtil::ConvertFbxMatrix(LocalMatrix).ToQuat().GetNormalized();
        OutScale = FBXUtil::ConvertFbxVector(LocalMatrix.GetS());
    }
}

void FFbxAnimationParser::ParseSkeletonAnimations(fbxsdk::FbxScene* Scene,
                                                  const FFbxSkeletonMeta& SkeletonMeta,
                                                  const TArray<FBoneInfo>& SkeletonBones,
                                                  TArray<UAnimSequence*>& OutAnimSequences,
                                                  float SampleRate) const
{
    OutAnimSequences.clear();

    if (!Scene || !SkeletonMeta.bValid || SkeletonMeta.BoneIds.empty())
    {
        return;
    }

    if (SampleRate <= 0.0f)
    {
        SampleRate = 30.0f;
    }

    const int32 BoneCount = static_cast<int32>(SkeletonMeta.BoneIds.size());

    TArray<FbxNode*> BoneNodes;
    BoneNodes.resize(BoneCount, nullptr);
    for (int32 SkeletonBoneIndex = 0; SkeletonBoneIndex < BoneCount; ++SkeletonBoneIndex)
    {
        const int32 BoneId = SkeletonMeta.BoneIds[SkeletonBoneIndex];
        if (IsValidIndex(ImportMeta.Bones, BoneId))
        {
            BoneNodes[SkeletonBoneIndex] = ImportMeta.Bones[BoneId].Node;
        }

        if (!BoneNodes[SkeletonBoneIndex])
        {
            UE_LOG("[FBXImporter] Missing bone node. SkeletonBoneIndex=%d BoneId=%d",
                   SkeletonBoneIndex,
                   BoneId);
        }
    }

    const int32 AnimStackCount = Scene->GetSrcObjectCount<FbxAnimStack>();
    for (int32 StackIndex = 0; StackIndex < AnimStackCount; ++StackIndex)
    {
        FbxAnimStack* AnimStack = Scene->GetSrcObject<FbxAnimStack>(StackIndex);
        if (!AnimStack)
        {
            continue;
        }

        const int32 LayerCount = AnimStack->GetMemberCount<FbxAnimLayer>();
        if (LayerCount <= 0)
        {
            UE_LOG("[FBXImporter] Skip animation stack. Stack=%s Reason=NoAnimLayer",
                   AnimStack->GetName() ? AnimStack->GetName() : "Unnamed");
            continue;
        }

        if (LayerCount > 1)
        {
            UE_LOG("[FBXImporter] Animation stack has %d layers. SDK evaluated result will be baked. Stack=%s",
                   LayerCount,
                   AnimStack->GetName() ? AnimStack->GetName() : "Unnamed");
        }

        Scene->SetCurrentAnimationStack(AnimStack);

        FbxTimeSpan TimeSpan;
        FbxTakeInfo* TakeInfo = Scene->GetTakeInfo(AnimStack->GetName());
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
        const double DurationSeconds = std::max(0.0, (StopTime - StartTime).GetSecondDouble());
        if (DurationSeconds <= 0.0)
        {
            continue;
        }

        const int32 FrameCount =
            std::max(2, static_cast<int32>(std::round(DurationSeconds * SampleRate)) + 1);

        UAnimSequence* Sequence = UObjectManager::Get().CreateObject<UAnimSequence>();
        UAnimDataModel* DataModel = UObjectManager::Get().CreateObject<UAnimDataModel>(Sequence);

        const FString SequenceName = AnimStack->GetName() ? AnimStack->GetName() : "Anim";
        Sequence->SetSequenceName(SequenceName);
        Sequence->SetDataModel(DataModel);

        DataModel->SetPlayLength(static_cast<float>(DurationSeconds));

        FFrameRate FrameRate;
        FrameRate.Numerator = static_cast<int32>(std::round(SampleRate));
        FrameRate.Denominator = 1;
        DataModel->SetFrameRate(FrameRate);
        DataModel->SetNumberOfFrames(FrameCount);
        DataModel->SetNumberOfKeys(FrameCount * BoneCount);

        TArray<FBoneAnimationTrack> Tracks;
        Tracks.resize(BoneCount);
        // 본마다 저장 공간을 준비한다. Track 이름은 BoneNodes와 같은 SkeletonMeta.BoneIds 기준으로 맞춘다.
        for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
        {
            const int32 BoneId = SkeletonMeta.BoneIds[BoneIndex];

            if (IsValidIndex(SkeletonBones, BoneIndex))
            {
                Tracks[BoneIndex].Name = FName(SkeletonBones[BoneIndex].Name.c_str());
            }
            else if (IsValidIndex(ImportMeta.Bones, BoneId))
            {
                Tracks[BoneIndex].Name = FName(ImportMeta.Bones[BoneId].Name.c_str());
            }
            else
            {
                Tracks[BoneIndex].Name = FName();
            }

            Tracks[BoneIndex].InternalTrack.PosKeys.resize(FrameCount, FVector(0.0f, 0.0f, 0.0f));
            Tracks[BoneIndex].InternalTrack.RotKeys.resize(FrameCount, FQuat::Identity);
            Tracks[BoneIndex].InternalTrack.ScaleKeys.resize(FrameCount, FVector(1.0f, 1.0f, 1.0f));
        }

        // 각 프레임 시간마다 SDK가 평가한 local transform을 bake한다.
        for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
        {
            const double FrameAlpha = (FrameCount > 1)
                                          ? static_cast<double>(FrameIndex) / static_cast<double>(FrameCount - 1)
                                          : 0.0;
            const double FrameSeconds = FrameAlpha * DurationSeconds;

            FbxTime SampleTime;
            SampleTime.SetSecondDouble(StartTime.GetSecondDouble() + FrameSeconds);

            for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
            {
                FbxNode* Node = BoneNodes[BoneIndex];
                if (!Node)
                {
                    continue;
                }

                FVector Translation;
                FQuat Rotation;
                FVector Scale;
                SampleEvaluatedLocalTransform(Node, SampleTime, Translation, Rotation, Scale);

                Tracks[BoneIndex].InternalTrack.PosKeys[FrameIndex] = Translation;
                Tracks[BoneIndex].InternalTrack.RotKeys[FrameIndex] = Rotation;
                Tracks[BoneIndex].InternalTrack.ScaleKeys[FrameIndex] = Scale;
            }
        }

        DataModel->SetBoneAnimationTracks(std::move(Tracks));

        UE_LOG("[FBXImporter] Baked animation sequence. Skeleton=%d Name=%s Duration=%.3fs Frames=%d "
               "Bones=%d Layers=%d",
               SkeletonMeta.SkeletonId,
               SequenceName.c_str(),
               static_cast<float>(DurationSeconds),
               FrameCount,
               BoneCount,
               LayerCount);

        OutAnimSequences.push_back(Sequence);
    }
}


// legacy: FbxAnimCurve에서 직접 샘플링하는 코드
namespace
{
    FbxVector4 SampleDouble3PropertyFromLayer(FbxPropertyT<FbxDouble3>& Property,
                                              FbxAnimLayer* Layer,
                                              FbxTime Time)
    {
        const FbxDouble3 DefaultValue = Property.Get();

        FbxAnimCurve* CurveX = Property.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_X);
        FbxAnimCurve* CurveY = Property.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_Y);
        FbxAnimCurve* CurveZ = Property.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_Z);

        return FbxVector4(
            SampleCurveOrDefault(CurveX, Time, DefaultValue[0]),
            SampleCurveOrDefault(CurveY, Time, DefaultValue[1]),
            SampleCurveOrDefault(CurveZ, Time, DefaultValue[2]),
            0.0);
    }

    FQuat SampleLocalRotationQuatFromLayer(FbxNode* Node, FbxAnimLayer* Layer, FbxTime Time)
    {
        FbxAMatrix RotationMatrix;
        RotationMatrix.SetIdentity();

        if (!Node || !Layer)
        {
            return FQuat::Identity;
        }

        const FbxVector4 RotationEuler = SampleDouble3PropertyFromLayer(Node->LclRotation, Layer, Time);
        RotationMatrix.SetR(RotationEuler);
        return FBXUtil::ConvertFbxMatrix(RotationMatrix).ToQuat().GetNormalized();
    }
}