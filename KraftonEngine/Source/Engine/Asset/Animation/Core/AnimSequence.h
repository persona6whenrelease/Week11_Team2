/**
 * 애니메이션 시퀀스 계층과 데이터 모델을 선언한다.
 *
 * 기존 legacy clip 단일 구조를 제거하고 Unreal 방식에 가까운 UAnimDataModel, UAnimSequenceBase,
 * UAnimSequence 계층으로 분리한다. 실제 키 데이터는 UAnimDataModel이 소유하고, UAnimSequence는
 * 스켈레톤 참조와 Notify를 포함한 재생 가능한 애니메이션 에셋 역할을 한다.
 */

#pragma once

#include "Asset/Animation/Core/AnimationTypes.h"
#include "Asset/Animation/Notify/AnimNotify.h"
#include "Asset/AssetFileHeader.h"
#include "Object/Object.h"

/**
 * 애니메이션 에셋 계층의 공통 기반 클래스이다.
 */
class UAnimationAsset : public UObject
{
  public:
    DECLARE_CLASS(UAnimationAsset, UObject)
    virtual float GetPlayLength() const { return 0.0f; }
};

/**
 * 애니메이션 키, 프레임 정보, 커브 데이터를 소유하는 데이터 모델이다.
 */
class UAnimDataModel : public UObject
{
  public:
    DECLARE_CLASS(UAnimDataModel, UObject)

    void Serialize(FArchive &Ar);

    void SetBoneAnimationTracks(TArray<FBoneAnimationTrack> &&InTracks) { BoneAnimationTracks = std::move(InTracks); }
    const TArray<FBoneAnimationTrack> &GetBoneAnimationTracks() const { return BoneAnimationTracks; }
    TArray<FBoneAnimationTrack>       &GetMutableBoneAnimationTracks() { return BoneAnimationTracks; }

    void SetPlayLength(float InPlayLength) { PlayLength = InPlayLength; }
    float GetPlayLength() const { return PlayLength; }

    void SetFrameRate(const FFrameRate &InFrameRate) { FrameRate = InFrameRate; }
    const FFrameRate &GetFrameRate() const { return FrameRate; }

    void SetNumberOfFrames(int32 InNumberOfFrames) { NumberOfFrames = InNumberOfFrames; }
    int32 GetNumberOfFrames() const { return NumberOfFrames; }

    void SetNumberOfKeys(int32 InNumberOfKeys) { NumberOfKeys = InNumberOfKeys; }
    int32 GetNumberOfKeys() const { return NumberOfKeys; }

    FAnimationCurveData       &GetMutableCurveData() { return CurveData; }
    const FAnimationCurveData &GetCurveData() const { return CurveData; }

  private:
    TArray<FBoneAnimationTrack> BoneAnimationTracks;
    float                      PlayLength = 0.0f;
    FFrameRate                 FrameRate;
    int32                      NumberOfFrames = 0;
    int32                      NumberOfKeys = 0;
    FAnimationCurveData        CurveData; // 시간에 따라 변하는 부가 float 값들
};

/**
 * 데이터 모델을 참조하는 재생 가능 애니메이션 에셋 기반 클래스이다.
 */
class UAnimSequenceBase : public UAnimationAsset
{
  public:
    DECLARE_CLASS(UAnimSequenceBase, UAnimationAsset)

    UAnimDataModel *GetDataModel() const { return DataModel; }
    UAnimDataModel *GetDataMode() const { return DataModel; }
    void SetDataModel(UAnimDataModel *InDataModel) { DataModel = InDataModel; }

    float GetPlayLength() const override
    {
        return DataModel ? DataModel->GetPlayLength() : 0.0f;
    }

  protected:
    UAnimDataModel *DataModel = nullptr;
};

/**
 * 스켈레톤 참조, 데이터 모델, Notify를 함께 저장하는 애니메이션 시퀀스 에셋이다.
 */
class UAnimSequence : public UAnimSequenceBase
{
  public:
    DECLARE_CLASS(UAnimSequence, UAnimSequenceBase)

    static constexpr uint32 AssetVersion = 3u;

    void Serialize(FArchive &Ar);

    void           SetSequenceName(const FString &InName) { SequenceName = InName; }
    const FString &GetSequenceName() const { return SequenceName; }

    void           SetSkeletonAssetPath(const FString &InPath) { SkeletonAssetPath = InPath; }
    const FString &GetSkeletonAssetPath() const { return SkeletonAssetPath; }

    void AddNotify(const FAnimNotifyEvent &Notify) { Notifies.push_back(Notify); }
    const TArray<FAnimNotifyEvent> &GetNotifies() const { return Notifies; }
    TArray<FAnimNotifyEvent>       &GetMutableNotifies() { return Notifies; }

    bool IsValidSequence() const
    {
        return DataModel && DataModel->GetPlayLength() > 0.0f &&
               !DataModel->GetBoneAnimationTracks().empty();
    }

  private:
    FString                  SequenceName;
    FString                  SkeletonAssetPath;
    TArray<FAnimNotifyEvent> Notifies;
};
