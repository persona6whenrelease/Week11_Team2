/**
 * FBX 스켈레탈 메시 조립 과정에서 사용하는 중간 메시 파트 구조를 정의한다.
 *
 * FBX에서는 하나의 스켈레톤에 여러 mesh node가 skin으로 연결되거나, rigid mesh가 특정 본에 붙어
 * 있는 식의 구성이 가능하다. 이 파일의 타입들은 그런 부분 메시들을 섹션과 정점 데이터 단위로 모아
 * 최종 FSkeletalMesh로 병합하기 전에 필요한 정보를 담는다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Mesh/SkeletalMeshAsset.h"

/** 스키닝 메시 파트 내부에서 머티리얼별 인덱스 범위를 나타내는 섹션 데이터이다. */
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
 * 최종 스켈레탈 메시로 병합되기 전의 부분 메시 데이터이다.
 *
 * FBX mesh node 하나에서 얻은 정점, 인덱스, 섹션, 연결 스켈레톤 정보를 담는다. 조립 단계에서 같은
 * 스켈레톤을 공유하는 파트들이 하나의 FSkeletalMesh로 합쳐진다.
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
