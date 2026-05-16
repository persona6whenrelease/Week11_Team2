/**
 * FBX 씬 분석 단계에서 사용하는 메타 데이터 구조를 정의한다.
 *
 * 이 파일의 구조체들은 원본 FBX 객체의 고유 ID와 엔진 임포트 분류 결과를 연결한다. 노드, 메시, 스킨,
 * 클러스터, 본, 스켈레톤, 머티리얼의 관계를 먼저 정리해 두면 이후 지오메트리/애니메이션 파서가 복잡한
 * FBX 씬 그래프를 다시 순회하지 않아도 된다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Engine/Math/Matrix.h"
#include "Asset/Import/FBX/Core/FBXUtil.h"

// ====================================================
// Per-Object Metadata
// ====================================================

/**
 * FBX 노드의 계층, 타입, 연결 정보를 저장하는 메타 데이터이다.
 */
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

/**
 * FBX mesh node가 어떤 스킨/스켈레톤/머티리얼과 연결되는지 저장한다.
 */
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

/**
 * FBX skin deformer와 그 cluster 목록을 저장한다.
 */
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

/**
 * 하나의 본이 mesh control point에 미치는 가중치 정보를 가리키는 메타 데이터이다.
 */
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

/**
 * FBX 노드가 본으로 해석될 때 필요한 부모/자식 관계와 스켈레톤 연결을 저장한다.
 */
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
 * 하나의 스켈레톤 루트와 그 본 목록을 저장하는 메타 데이터이다.
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

/**
 * FBX 머티리얼의 이름, 색상, 텍스처 참조를 엔진 임포트 단계에서 보관하는 구조이다.
 */
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
 * FBX 씬 분석 결과 전체를 담는 공유 메타 데이터 컨테이너이다.
 */
// ====================================================
// Import Metadata Container
// ====================================================

struct FFbxImportMeta
{
    // Source file
    FString                           SourceFilePath;

    // Collected metadata tables
    TArray<FFbxNodeMeta>              Nodes;
    TArray<FFbxMeshMeta>              Meshes;
    TArray<FFbxSkinMeta>              Skins;
    TArray<FFbxClusterMeta>           Clusters;
    TArray<FFbxBoneMeta>              Bones;
    TArray<FFbxSkeletonMeta>          Skeletons;
    TArray<FFbxMaterialInfo>          Materials;

    // Classified result lists
    TArray<int32>                     StaticMeshIds;
    TArray<int32>                     SkeletalMeshIds;
    TArray<int32>                     RigidAttachedMeshIds;
    TArray<int32>                     IndependentStaticMeshIds;
    TArray<int32>                     LightNodeIds;
    TArray<int32>                     CameraNodeIds;

    // Reverse lookup maps
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
