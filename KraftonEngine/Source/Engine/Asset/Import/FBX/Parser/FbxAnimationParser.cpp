/**
 * FBX 애니메이션 데이터를 UAnimSequence 에셋으로 변환한다.
 *
 * FBX animation stack마다 하나의 UAnimSequence를 생성한다. 단, stack 내부에 여러 animation layer가
 * 있더라도 현재 importer는 0번 layer, 즉 base layer만 사용한다. FBX SDK의 EvaluateGlobalTransform()
 * 기반 평가는 stack 전체 layer가 합성될 수 있으므로 사용하지 않고, 0번 layer의 LclTranslation,
 * LclRotation, LclScaling curve만 직접 샘플링한다.
 *
 * 이 파일은 animation blending layer를 지원하지 않는다. 여러 layer가 존재하면 로그만 남기고 첫 번째
 * layer만 bake한다. Runtime은 이미 bake된 UAnimDataModel만 재생하므로 layer 개념을 알 필요가 없다.
 */

// TODO: 다중 레이어 합성 처리

#include "Asset/Import/FBX/Parser/FbxAnimationParser.h"

#include "Core/Log.h"
#include "Asset/Import/FBX/Core/FBXUtil.h"
#include "Object/ObjectFactory.h"

#include <fbxsdk.h>

#include <algorithm>
#include <cmath>
#include <cstring>

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

    FbxAMatrix SampleLocalMatrixFromBaseLayer(FbxNode* Node, FbxAnimLayer* BaseLayer, FbxTime Time)
    {
        FbxAMatrix LocalMatrix;
        LocalMatrix.SetIdentity();

        if (!Node || !BaseLayer)
        {
            return LocalMatrix;
        }

        const FbxVector4 Translation = SampleDouble3PropertyFromLayer(Node->LclTranslation, BaseLayer, Time);
        const FbxVector4 Rotation = SampleDouble3PropertyFromLayer(Node->LclRotation, BaseLayer, Time);
        const FbxVector4 Scaling = SampleDouble3PropertyFromLayer(Node->LclScaling, BaseLayer, Time);

        LocalMatrix.SetT(Translation);
        LocalMatrix.SetR(Rotation);
        LocalMatrix.SetS(Scaling);

        return LocalMatrix;
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
    }

    const int32 TotalSrcObjectCount = Scene->GetSrcObjectCount();
    for (int32 ObjIndex = 0; ObjIndex < TotalSrcObjectCount; ++ObjIndex)
    {
        FbxObject* Obj = Scene->GetSrcObject(ObjIndex);
        if (!Obj)
        {
            continue;
        }

        const char* ClassName = Obj->GetClassId().GetName();
        if (!ClassName)
        {
            continue;
        }

        if (std::strcmp(ClassName, "AnimStack") != 0 && std::strcmp(ClassName, "FbxAnimStack") != 0)
        {
            continue;
        }

        FbxAnimStack* AnimStack = static_cast<FbxAnimStack*>(Obj);
        if (!AnimStack)
        {
            continue;
        }

        FbxAnimLayer* BaseLayer = AnimStack->GetMember<FbxAnimLayer>(0);
        if (!BaseLayer)
        {
            UE_LOG("[FBXImporter] Skip animation stack. Stack=%s Reason=NoAnimLayer",
                   AnimStack->GetName() ? AnimStack->GetName() : "Unnamed");
            continue;
        }

        const int32 LayerCount = AnimStack->GetMemberCount<FbxAnimLayer>();
        if (LayerCount > 1)
        {
            UE_LOG("[FBXImporter] Animation stack has %d layers. Only first layer will be imported. Stack=%s",
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
        for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
        {
            Tracks[BoneIndex].Name = (BoneIndex < static_cast<int32>(SkeletonBones.size()))
                                         ? FName(SkeletonBones[BoneIndex].Name.c_str())
                                         : FName();
            Tracks[BoneIndex].InternalTrack.LocalMatrixKeys.resize(FrameCount, FMatrix::Identity);
        }

        TArray<FMatrix> LocalMatrices;
        LocalMatrices.resize(BoneCount, FMatrix::Identity);

        TArray<FMatrix> GlobalMatrices;
        GlobalMatrices.resize(BoneCount, FMatrix::Identity);

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
                    LocalMatrices[BoneIndex] = FMatrix::Identity;
                    GlobalMatrices[BoneIndex] = FMatrix::Identity;
                    continue;
                }

                LocalMatrices[BoneIndex] =
                    FBXUtil::ConvertFbxMatrix(SampleLocalMatrixFromBaseLayer(Node, BaseLayer, SampleTime));

                const int32 ParentIndex = (BoneIndex < static_cast<int32>(SkeletonBones.size()))
                                              ? SkeletonBones[BoneIndex].ParentIndex
                                              : -1;

                if (ParentIndex >= 0 && ParentIndex < BoneCount)
                {
                    // 기존 importer의 local 계산식이 Local = Global * ParentGlobal^-1 이므로,
                    // 같은 행렬 규약을 유지하기 위해 Global = Local * ParentGlobal 로 누적한다.
                    GlobalMatrices[BoneIndex] = LocalMatrices[BoneIndex] * GlobalMatrices[ParentIndex];
                }
                else
                {
                    GlobalMatrices[BoneIndex] = LocalMatrices[BoneIndex];
                }

                Tracks[BoneIndex].InternalTrack.LocalMatrixKeys[FrameIndex] = LocalMatrices[BoneIndex];
            }
        }

        DataModel->SetBoneAnimationTracks(std::move(Tracks));

        UE_LOG("[FBXImporter] Baked animation sequence. Skeleton=%d Name=%s Duration=%.3fs Frames=%d "
               "Bones=%d Layer=0/%d",
               SkeletonMeta.SkeletonId,
               SequenceName.c_str(),
               static_cast<float>(DurationSeconds),
               FrameCount,
               BoneCount,
               LayerCount);

        OutAnimSequences.push_back(Sequence);
    }
}
