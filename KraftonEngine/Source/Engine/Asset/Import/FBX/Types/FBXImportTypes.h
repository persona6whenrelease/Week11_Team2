/**
 * FBX 임포트 중간 단계에서 사용하는 메시 파트 구조를 정의한다.
 *
 * 스켈레탈 메시 조립 전에는 mesh node마다 정점, 인덱스, 섹션, 머티리얼 슬롯을 따로 보관해야 한다.
 * 이 구조들은 파서와 assembler 사이를 연결하며, 최종 FSkeletalMesh로 합쳐지기 전의 임시 결과물이다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"

/**
 * 스켈레탈 메시 파트 내부의 섹션 인덱스 범위와 머티리얼 슬롯을 저장한다.
 */
struct FFbxMeshPartSection
{
    int32   SourceMeshId = -1;
    int32   MaterialSlotIndex = 0;
    int32   SourceMaterialId = -1;
    FString MaterialSlotName = "None";
    int32   FirstIndex = 0;
    int32   IndexCount = 0;
};

/**
 * 최종 스켈레탈 메시로 병합되기 전의 파트 단위 정점/인덱스/섹션 데이터이다.
 */
struct FFbxSkinnedMeshPart
{
    int32                       MeshId = -1;
    int32                       SkinId = -1;
    int32                       SkeletonId = -1;
    int32                       AttachedBoneId = -1;
    int32                       AttachedSkeletonBoneIndex = -1;
    bool                        bRigidAttached = false;
    bool                        bSkinned = false;
    FString                     SourceNodePath;
    TArray<FSkeletalVertex>     Vertices;
    TArray<uint32>              Indices;
    TArray<FFbxMeshPartSection> Sections;
};
