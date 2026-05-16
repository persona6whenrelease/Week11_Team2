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
 * 에디터 목록에 표시할 메시 에셋 항목이다.
 *
 * 사용자에게 보여줄 이름과 실제 로드에 사용할 전체 경로를 분리해서 저장한다. Content Browser나
 * 드롭다운 UI는 DisplayName을 사용하고, 선택 이후의 로드 경로는 FullPath를 사용한다.
 */
struct FMeshAssetListItem
{
    FString DisplayName;
    FString FullPath;
};

/**
 * 하나의 메시 안에서 같은 머티리얼을 사용하는 인덱스 버퍼 구간이다.
 *
 * 렌더러는 섹션 단위로 머티리얼을 바꾸며 draw call을 나눌 수 있다. FirstIndex와 NumTriangles는
 * 인덱스 버퍼 안에서 이 섹션이 차지하는 범위를 나타내고, MaterialIndex/MaterialSlotName은 해당
 * 구간이 어떤 머티리얼 슬롯에 연결되는지 표현한다.
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

using FStaticMeshSection = FMeshSection;
using FStaticMaterial = FMeshMaterial;
