/**
 * 애니메이션 데이터 모델과 시퀀스 에셋의 직렬화 구현을 제공한다.
 *
 * UAnimSequence 저장 시에는 공통 에셋 헤더를 먼저 기록하고, 로드 시 Magic/AssetType/Version이 현재
 * 버전과 일치하지 않으면 본문을 읽지 않는다. 실제 키 데이터는 UAnimDataModel이 직렬화한다.
 */

#include "Asset/Animation/Core/AnimSequence.h"

#include "Core/Log.h"
#include "Object/ObjectFactory.h"

IMPLEMENT_CLASS(UAnimationAsset, UObject)
IMPLEMENT_CLASS(UAnimDataModel, UObject)
IMPLEMENT_CLASS(UAnimSequenceBase, UAnimationAsset)
IMPLEMENT_CLASS(UAnimSequence, UAnimSequenceBase)

void UAnimDataModel::Serialize(FArchive &Ar)
{
    Ar << BoneAnimationTracks;
    Ar << PlayLength;
    Ar << FrameRate;
    Ar << NumberOfFrames;
    Ar << NumberOfKeys;
    Ar << CurveData;
}

void UAnimSequence::Serialize(FArchive &Ar)
{
    FAssetFileHeader Header;
    if (Ar.IsSaving())
    {
        Header.AssetType = EAssetType::AnimSequence;
        Header.Version = AssetVersion;
    }

    Ar << Header;
    if (!Header.IsValid(EAssetType::AnimSequence, AssetVersion))
    {
        UE_LOG("[UAnimSequence] Invalid asset header. Type=%s Version=%u",
               LexToString(Header.AssetType), Header.Version);
        return;
    }

    Ar << SequenceName;
    Ar << SkeletonAssetPath;

    uint32 bHasDataModel = DataModel ? 1u : 0u;
    Ar << bHasDataModel;
    if (Ar.IsLoading() && bHasDataModel != 0u && !DataModel)
    {
        DataModel = UObjectManager::Get().CreateObject<UAnimDataModel>(this);
    }
    if (bHasDataModel != 0u && DataModel)
    {
        DataModel->Serialize(Ar);
    }

    Ar << Notifies;
}
