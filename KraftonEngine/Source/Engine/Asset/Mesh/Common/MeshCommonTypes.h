/**
 * 정적/스켈레탈 메시가 공유하는 섹션과 머티리얼 슬롯 구조를 정의한다.
 *
 * 렌더러는 메시 종류와 관계없이 섹션 단위로 인덱스 범위와 머티리얼을 선택한다. 이 공통 타입들은 에셋
 * 목록 표시, 섹션 렌더링, 머티리얼 바인딩에서 StaticMesh와 SkeletalMesh가 같은 규칙을 쓰도록 만든다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Engine/Object/FName.h"
#include "Asset/Material/Material.h"
#include "Asset/Material/MaterialManager.h"
#include "Serialization/Archive.h"

/**
 * 에디터 메시 목록에 표시할 에셋 경로와 이름을 담는 구조이다.
 */
struct FMeshAssetListItem
{
    FString DisplayName;
    FString FullPath;
};

/**
 * 메시의 특정 인덱스 범위를 하나의 머티리얼로 렌더링하기 위한 섹션 정보이다.
 */
struct FMeshSection
{
    int32   MaterialIndex = -1;
    FString MaterialSlotName;
    uint32  FirstIndex = 0;
    uint32  NumTriangles = 0;

    friend FArchive &operator<<(FArchive &Ar, FMeshSection &Section)
    {
        Ar << Section.MaterialSlotName << Section.FirstIndex << Section.NumTriangles;
        return Ar;
    }
};

/**
 * 메시 섹션이 참조할 머티리얼 슬롯 이름과 에셋 경로를 저장한다.
 */
struct FMeshMaterial
{
    UMaterial *MaterialInterface = nullptr;
    FString    MaterialSlotName = "None";

    friend FArchive &operator<<(FArchive &Ar, FMeshMaterial &Mat)
    {
        Ar << Mat.MaterialSlotName;

        FString JsonPath;
        if (Ar.IsSaving() && Mat.MaterialInterface)
        {
            JsonPath = Mat.MaterialInterface->GetAssetPathFileName();
        }
        Ar << JsonPath;

        if (Ar.IsLoading())
        {
            Mat.MaterialInterface =
                JsonPath.empty() ? nullptr : FMaterialManager::Get().GetOrCreateMaterial(JsonPath);
        }

        return Ar;
    }
};

// legacy
using FStaticMeshSection = FMeshSection;
using FStaticMaterial = FMeshMaterial;
