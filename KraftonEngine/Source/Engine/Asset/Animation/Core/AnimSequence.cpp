/**
 * 애니메이션 시퀀스 에셋의 직렬화 구현을 제공한다.
 *
 * 저장 시에는 AnimSequence 타입과 버전을 가진 공통 에셋 헤더를 먼저 기록하고, 로드 시에는 헤더 검증을
 * 통과한 경우에만 스켈레톤 경로, 클립 데이터, Notify 정보를 읽는다. 잘못된 파일을 다른 에셋 타입으로
 * 해석하지 않도록 초기 단계에서 방어하는 역할을 한다.
 */

#include "Asset/Animation/Core/AnimSequence.h"

#include "Core/Log.h"
#include "Object/ObjectFactory.h"

IMPLEMENT_CLASS(UAnimSequence, UObject)

void UAnimSequence::Serialize(FArchive& Ar)
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
        UE_LOG("[UAnimSequence] Invalid asset header. Type=%s Version=%u", LexToString(Header.AssetType), Header.Version);
        return;
    }

    Ar << SkeletonAssetPath;
    Ar << AnimationClip;
    Ar << Notifies;
}
