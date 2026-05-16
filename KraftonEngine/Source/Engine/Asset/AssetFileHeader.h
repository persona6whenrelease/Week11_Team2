/**
 * 엔진 에셋 파일의 공통 헤더 형식을 정의한다.
 *
 * 각 .uasset 계열 파일은 실제 본문 데이터 앞에 에셋 타입과 버전을 기록한다. 로더는 이 헤더를 먼저
 * 검증해 잘못된 타입의 파일을 읽거나, 호환되지 않는 버전의 데이터를 현재 구조체 레이아웃으로
 * 해석하는 문제를 방지한다.
 */

#pragma once

#include "Asset/AssetTypes.h"
#include "Serialization/Archive.h"

/**
 * 에셋 파일 본문 앞에 기록되는 타입/버전 검증용 헤더이다.
 */
struct FAssetFileHeader
{
    static constexpr uint32 ExpectedMagic = 0x54455341u;

    uint32     Magic = ExpectedMagic;
    EAssetType AssetType = EAssetType::Unknown;
    uint32     Version = 1;
    uint32     PayloadSize = 0;

    friend FArchive &operator<<(FArchive &Ar, FAssetFileHeader &Header)
    {
        Ar << Header.Magic;
        Ar << Header.AssetType;
        Ar << Header.Version;
        Ar << Header.PayloadSize;
        return Ar;
    }

    bool IsValid(EAssetType ExpectedType, uint32 ExpectedVersion) const
    {
        return Magic == ExpectedMagic && AssetType == ExpectedType && Version == ExpectedVersion;
    }
};
