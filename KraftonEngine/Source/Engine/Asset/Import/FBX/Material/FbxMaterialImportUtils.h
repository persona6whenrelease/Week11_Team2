/**
 * FBX 머티리얼 임포트 보조 함수들을 선언한다.
 *
 * 원본 FBX 머티리얼의 이름, 색상, 텍스처 파일 경로를 엔진의 Material/Texture 에셋 규칙에 맞게
 * 변환하는 기능을 제공한다. 메시 파서는 지오메트리 생성에 집중하고, 머티리얼 경로 복구와 uasset
 * 생성 정책은 이 유틸리티 계층으로 분리된다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Mesh/Common/MeshCommonTypes.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "Asset/Mesh/StaticMesh/StaticMeshAsset.h"

namespace FbxMaterialImportUtils
{
    FString NormalizeMaterialSlotName(const FString &SlotName);
    void    BuildStaticMaterials(FFbxImportMeta &ImportMeta, const FStaticMesh &Mesh,
                                 TArray<FMeshMaterial> &OutMaterials);
    void    BuildSkeletalMaterials(FFbxImportMeta &ImportMeta, const FSkeletalMesh &Mesh,
                                   TArray<FMeshMaterial> &OutMaterials);
} // namespace FbxMaterialImportUtils
