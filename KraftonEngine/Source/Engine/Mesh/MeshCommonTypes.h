/**
 * 메시 에셋 목록 표시, 섹션 범위, 머티리얼 슬롯처럼 정적 메시와 스켈레탈 메시가 함께 사용하는
 * 공통 데이터 구조를 정의한다.
 *
 * 이 파일의 타입들은 렌더링 리소스 자체가 아니라 에셋 로딩, 직렬화, 에디터 목록 표시 과정에서
 * 메시 데이터의 기본 단위를 맞추기 위한 경량 구조체이다. 특히 섹션은 인덱스 버퍼의 구간과
 * 머티리얼 슬롯을 연결하고, 머티리얼 정보는 저장 시 경로 문자열로 바뀌었다가 로드 시
 * MaterialManager를 통해 실제 머티리얼 객체로 복구된다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Engine/Object/FName.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
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
 * 메시의 머티리얼 슬롯과 실제 머티리얼 객체를 연결하는 데이터이다.
 *
 * 저장 시에는 UObject 포인터를 그대로 기록할 수 없으므로 머티리얼 에셋 경로를 직렬화하고,
 * 로드 시에는 MaterialManager를 통해 다시 UMaterial 포인터로 복원한다.
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
