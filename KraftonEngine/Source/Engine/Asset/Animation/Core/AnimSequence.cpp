/**
 * 애니메이션 데이터 모델과 시퀀스 에셋의 직렬화 구현을 제공한다.
 *
 * UAnimSequence 저장 시에는 공통 에셋 헤더를 먼저 기록하고, 로드 시 Magic/AssetType/Version이 현재
 * 버전과 일치하지 않으면 본문을 읽지 않는다. 실제 키 데이터는 UAnimDataModel이 직렬화한다.
 */

#include "Asset/Animation/Core/AnimSequence.h"

#include "Core/Log.h"
#include "Object/ObjectFactory.h"

REGISTER_FACTORY(UAnimationAsset)
REGISTER_FACTORY(UAnimDataModel)
REGISTER_FACTORY(UAnimSequenceBase)
REGISTER_FACTORY(UAnimSequence)

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

    // R6: FAssetFileHeader::IsValid 시그니처를 변경하지 않기 위해 호출 대신
    // AnimSequence 내부에서 두 조건(Type/Version)만 인라인 검사한다.
    // Version은 <= 로 비교하여 v3 -> v4 backward-compat 로드를 허용한다.
    const bool bTypeOk = (Header.AssetType == EAssetType::AnimSequence);
    const bool bVersionOk = (Header.Version <= AssetVersion);
    if (!bTypeOk || !bVersionOk)
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

    if (Ar.IsLoading() && Header.Version == 3u)
    {
        // v3 backfill: 새 v4 필드(Type/SoundId/ShakeParams)는 default 값으로 채운다.
        uint32 Count = 0;
        Ar << Count;
        Notifies.resize(Count);
        for (uint32 i = 0; i < Count; ++i)
        {
            Ar << Notifies[i].TriggerTime;
            Ar << Notifies[i].Duration;
            Ar << Notifies[i].NotifyName;
            Notifies[i].Type = EAnimNotifyType::None;
        }
    }
    else
    {
        Ar << Notifies;
    }
}
