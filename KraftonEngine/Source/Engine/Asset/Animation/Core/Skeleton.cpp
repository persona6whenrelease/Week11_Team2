/**
 * 스켈레톤 에셋의 저장/로드와 본 검색 로직을 구현한다.
 *
 * 공통 에셋 헤더를 통해 Skeleton 파일인지 검증한 뒤 본 배열을 직렬화한다. 본 이름 검색은 FBX 임포트,
 * 에디터 선택, 애니메이션 트랙 연결처럼 이름 기반으로 본 인덱스를 찾아야 하는 지점에서 사용된다.
 */

#include "Asset/Animation/Core/Skeleton.h"

#include "Core/Log.h"
#include "Object/ObjectFactory.h"

IMPLEMENT_CLASS(USkeleton, UObject)

void USkeleton::Serialize(FArchive& Ar)
{
    FAssetFileHeader Header;
    if (Ar.IsSaving())
    {
        Header.AssetType = EAssetType::Skeleton;
        Header.Version = AssetVersion;
    }

    Ar << Header;
    if (!Header.IsValid(EAssetType::Skeleton, AssetVersion))
    {
        UE_LOG("[USkeleton] Invalid asset header. Type=%s Version=%u", LexToString(Header.AssetType), Header.Version);
        return;
    }

    Ar << Bones;
}

int32 USkeleton::FindBoneIndexByName(const FString& BoneName) const
{
    for (int32 Index = 0; Index < static_cast<int32>(Bones.size()); ++Index)
    {
        if (Bones[Index].Name == BoneName)
        {
            return Index;
        }
    }
    return -1;
}
