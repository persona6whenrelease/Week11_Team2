/**
 * FBX 원본 씬을 분석해 얻은 중간 메타 데이터를 정의한다.
 *
 * 노드, 메시, 스킨, 클러스터, 본, 스켈레톤, 머티리얼의 원본 SDK 포인터와 엔진 내부 ID를 함께
 * 보관한다. 실제 메시 데이터를 만들기 전에 원본 계층과 참조 관계를 안정적으로 정리하는 단계이며,
 * 여러 파서가 같은 FBX 씬 정보를 공유할 수 있게 하는 공통 컨텍스트 역할을 한다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Engine/Math/Matrix.h"
#include "FBXUtil.h"

/** FBX 노드의 ID, 계층, 원본 포인터를 함께 보관하는 메타 데이터이다. */
struct FFbxNodeMeta
{
    int32         NodeId = -1;
    int32         ParentNodeId = -1;
    TArray<int32> ChildNodeIds;
    FbxNode      *Node = nullptr;
    FString       Name;
    FString       FullPath;
    FString       AttributeTypeName;
    bool          bHasAttribute = false;
    bool          bHasMesh = false;
    bool          bHasSkeleton = false;
    bool          bHasLight = false;
    bool          bHasCamera = false;
    FMatrix       LocalTransform = FMatrix::Identity;
    FMatrix       GlobalTransform = FMatrix::Identity;
};

/** FBX mesh node의 원본 포인터와 씬 내부 식별자를 묶는 메타 데이터이다. */
struct FFbxMeshMeta
{
    int32           MeshId = -1;
    int32           NodeId = -1;
    FbxNode        *Node = nullptr;
    FbxMesh        *Mesh = nullptr;
    FString         Name;
    FString         SourceNodePath;
    TArray<int32>   MaterialSlotIds;
    TArray<FString> MaterialSlotNames;
    TArray<FString> MaterialUVSetNames;
    TArray<int32>   MaterialIds;
    TArray<int32>   SkinIds;
    int32           PrimarySkinId = -1;
    int32           SkeletonId = -1;
    int32           AttachedSkeletonId = -1;
    int32           AttachedBoneId = -1;
    bool            bHasSkin = false;
    bool            bHasValidSkin = false;
    bool            bStaticCandidate = false;
    bool            bSkeletalCandidate = false;
    bool            bAttachedToSkeleton = false;
    bool            bRigidAttachedCandidate = false;
    bool            bIndependentStaticCandidate = false;
    int32           ControlPointCount = 0;
    int32           PolygonCount = 0;
};

/** FBX skin deformer와 연결된 메시 정보를 추적하는 메타 데이터이다. */
struct FFbxSkinMeta
{
    int32         SkinId = -1;
    int32         MeshId = -1;
    FbxSkin      *Skin = nullptr;
    TArray<int32> ClusterIds;
    TArray<int32> BoneIds;
    int32         SkeletonId = -1;
    bool          bValid = false;
    int32         ClusterCount = 0;
    int32         TotalInfluenceCount = 0;
};

/** FBX cluster가 어떤 본 노드와 control point weight를 가지는지 기록하는 메타 데이터이다. */
struct FFbxClusterMeta
{
    int32       ClusterId = -1;
    int32       SkinId = -1;
    int32       MeshId = -1;
    FbxCluster *Cluster = nullptr;
    FbxNode    *LinkNode = nullptr;
    int32       LinkNodeId = -1;
    int32       LinkBoneId = -1;
    FString     LinkNodeName;
    FMatrix     MeshBindGlobalMatrix = FMatrix::Identity;
    FMatrix     BoneBindGlobalMatrix = FMatrix::Identity;
    bool        bHasMeshBindMatrix = false;
    bool        bHasBoneBindMatrix = false;
    int32       ControlPointInfluenceCount = 0;
    int32       PositiveWeightCount = 0;
    bool        bValid = false;
};

/** FBX 노드 중 스켈레톤 본으로 해석된 항목의 계층 정보를 담는 메타 데이터이다. */
struct FFbxBoneMeta
{
    int32         BoneId = -1;
    int32         NodeId = -1;
    FbxNode      *Node = nullptr;
    FString       Name;
    FString       FullPath;
    int32         ParentBoneId = -1;
    TArray<int32> ChildBoneIds;
    int32         SkeletonId = -1;
    int32         SkeletonBoneIndex = -1;
    FMatrix       ModelLocalMatrix = FMatrix::Identity;
    FMatrix       ModelGlobalMatrix = FMatrix::Identity;
    FMatrix       BindGlobalMatrix = FMatrix::Identity;
    FMatrix       InvBindGlobalMatrix = FMatrix::Identity;
    bool          bReferencedByCluster = false;
    bool          bInsertedAsParentChain = false;
    bool          bSyntheticRoot = false;
};

/**
 * 하나의 스켈레톤으로 묶인 본 계층과 ID 매핑을 보관한다.
 *
 * FBX에는 여러 skeleton root가 있을 수 있으므로, 각 스켈레톤 단위로 본 배열과 원본 bone id에서 엔진
 * bone index로 가는 매핑을 분리한다. 스키닝 파트 조립과 애니메이션 파싱의 기준 테이블이다.
 */
struct FFbxSkeletonMeta
{
    int32                  SkeletonId = -1;
    int32                  RootBoneId = -1;
    int32                  RootNodeId = -1;
    FString                Name;
    TArray<int32>          BoneIds;
    TArray<int32>          MeshIds;
    TArray<int32>          SkinnedMeshIds;
    TArray<int32>          RigidAttachedMeshIds;
    TMap<int32, int32>     BoneIdToSkeletonBoneIndex;
    TMap<FbxNode *, int32> BoneNodeToSkeletonBoneIndex;
    bool                   bValid = false;
    bool                   bBuiltFromSkinClusters = false;
    bool                   bHasSingleRoot = true;
};

/** FBX 머티리얼의 이름, 원본 포인터, 변환된 에셋 경로를 함께 보관하는 메타 데이터이다. */
struct FFbxMaterialInfo
{
    int32   MaterialId = -1;
    FString MaterialSlotName = "None";
    FString MaterialAssetPath;
    FVector DiffuseColor = FVector(1.0f, 0.0f, 1.0f);
    FString DiffuseTexturePath;
    FString NormalTexturePath;
    FString SpecularTexturePath;
    FString EmissiveTexturePath;
    FString DiffuseUVSetName;
};

/**
 * FBX 임포트 전체가 공유하는 원본 씬 메타 컨텍스트이다.
 *
 * 노드, 메시, 스킨, 본, 스켈레톤, 머티리얼을 ID 기반으로 정리해 하위 파서들이 같은 기준을
 * 사용하도록 한다. FBX SDK 포인터의 직접 순회 비용과 중복 해석을 줄이는 중심 데이터이다.
 */
struct FFbxImportMeta
{
    FString                           SourceFilePath;
    TArray<FFbxNodeMeta>              Nodes;
    TArray<FFbxMeshMeta>              Meshes;
    TArray<FFbxSkinMeta>              Skins;
    TArray<FFbxClusterMeta>           Clusters;
    TArray<FFbxBoneMeta>              Bones;
    TArray<FFbxSkeletonMeta>          Skeletons;
    TArray<FFbxMaterialInfo>          Materials;
    TArray<int32>                     StaticMeshIds;
    TArray<int32>                     SkeletalMeshIds;
    TArray<int32>                     RigidAttachedMeshIds;
    TArray<int32>                     IndependentStaticMeshIds;
    TArray<int32>                     LightNodeIds;
    TArray<int32>                     CameraNodeIds;
    TMap<FbxNode *, int32>            NodeToNodeId;
    TMap<FbxMesh *, int32>            MeshToMeshId;
    TMap<FbxMesh *, TArray<int32>>    FbxMeshToMeshIds;
    TMap<FbxSkin *, int32>            SkinToSkinId;
    TMap<FbxCluster *, int32>         ClusterToClusterId;
    TMap<FbxNode *, int32>            BoneNodeToBoneId;
    TMap<FbxSurfaceMaterial *, int32> MaterialToMaterialId;
    TMap<FString, int32>              MaterialNameToMaterialId;

    void Clear()
    {
        SourceFilePath.clear();
        Nodes.clear();
        Meshes.clear();
        Skins.clear();
        Clusters.clear();
        Bones.clear();
        Skeletons.clear();
        Materials.clear();
        StaticMeshIds.clear();
        SkeletalMeshIds.clear();
        RigidAttachedMeshIds.clear();
        IndependentStaticMeshIds.clear();
        LightNodeIds.clear();
        CameraNodeIds.clear();
        NodeToNodeId.clear();
        MeshToMeshId.clear();
        FbxMeshToMeshIds.clear();
        SkinToSkinId.clear();
        ClusterToClusterId.clear();
        BoneNodeToBoneId.clear();
        MaterialToMaterialId.clear();
        MaterialNameToMaterialId.clear();
    }

    bool IsAncestorOf(int32 AncestorId, int32 BoneId)
    {
        int32 CurrentId = BoneId;
        while (CurrentId >= 0 && static_cast<size_t>(CurrentId) < Bones.size())
        {
            if (CurrentId == AncestorId)
            {
                return true;
            }
            CurrentId = Bones[CurrentId].ParentBoneId;
        }
        return false;
    };
};
