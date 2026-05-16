/**
 * FBX 머티리얼 정보를 엔진 머티리얼 슬롯과 파일로 변환하는 유틸리티를 선언한다.
 *
 * FBX 파서가 수집한 머티리얼 이름, 색상, 텍스처 경로는 곧바로 렌더러가 사용할 수 있는 형태가 아니다.
 * 이 유틸리티는 슬롯 이름을 정규화하고, StaticMesh/SkeletalMesh가 참조할 FStaticMaterial 배열을 만든다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Mesh/Common/MeshCommonTypes.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "Asset/Mesh/StaticMesh/StaticMeshAsset.h"

namespace FbxMaterialImportUtils
{
    /**
     * FBX 머티리얼 이름을 파일명과 슬롯 비교에 안전한 형태로 정규화한다.
     */
    FString NormalizeMaterialSlotName(const FString &SlotName);
    void    BuildStaticMaterials(FFbxImportMeta &ImportMeta, const FStaticMesh &Mesh,
                                 TArray<FMeshMaterial> &OutMaterials);
    void    BuildSkeletalMaterials(FFbxImportMeta &ImportMeta, const FSkeletalMesh &Mesh,
                                   TArray<FMeshMaterial> &OutMaterials);
} 
