/**
 * FBX ?먮낯 ?ъ쓣 遺꾩꽍???살? 以묎컙 硫뷀? ?곗씠?곕? ?뺤쓽?쒕떎.
 *
 * ?몃뱶, 硫붿떆, ?ㅽ궓, ?대윭?ㅽ꽣, 蹂? ?ㅼ펷?덊넠, 癒명떚由ъ뼹???먮낯 SDK ?ъ씤?곗? ?붿쭊 ?대? ID瑜??④퍡
 * 蹂닿??쒕떎. ?ㅼ젣 硫붿떆 ?곗씠?곕? 留뚮뱾湲??꾩뿉 ?먮낯 怨꾩링怨?李몄“ 愿怨꾨? ?덉젙?곸쑝濡??뺣━?섎뒗 ?④퀎?대ŉ,
 * ?щ윭 ?뚯꽌媛 媛숈? FBX ???뺣낫瑜?怨듭쑀?????덇쾶 ?섎뒗 怨듯넻 而⑦뀓?ㅽ듃 ??븷???쒕떎.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Engine/Math/Matrix.h"
#include "Asset/Import/FBX/Core/FBXUtil.h"

/** FBX ?몃뱶??ID, 怨꾩링, ?먮낯 ?ъ씤?곕? ?④퍡 蹂닿??섎뒗 硫뷀? ?곗씠?곗씠?? */
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

/** FBX mesh node???먮낯 ?ъ씤?곗? ???대? ?앸퀎?먮? 臾띕뒗 硫뷀? ?곗씠?곗씠?? */
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

/** FBX skin deformer? ?곌껐??硫붿떆 ?뺣낫瑜?異붿쟻?섎뒗 硫뷀? ?곗씠?곗씠?? */
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

/** FBX cluster媛 ?대뼡 蹂??몃뱶? control point weight瑜?媛吏?붿? 湲곕줉?섎뒗 硫뷀? ?곗씠?곗씠?? */
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

/** FBX ?몃뱶 以??ㅼ펷?덊넠 蹂몄쑝濡??댁꽍????ぉ??怨꾩링 ?뺣낫瑜??대뒗 硫뷀? ?곗씠?곗씠?? */
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
 * ?섎굹???ㅼ펷?덊넠?쇰줈 臾띠씤 蹂?怨꾩링怨?ID 留ㅽ븨??蹂닿??쒕떎.
 *
 * FBX?먮뒗 ?щ윭 skeleton root媛 ?덉쓣 ???덉쑝誘濡? 媛??ㅼ펷?덊넠 ?⑥쐞濡?蹂?諛곗뿴怨??먮낯 bone id?먯꽌 ?붿쭊
 * bone index濡?媛??留ㅽ븨??遺꾨━?쒕떎. ?ㅽ궎???뚰듃 議곕┰怨??좊땲硫붿씠???뚯떛??湲곗? ?뚯씠釉붿씠??
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

/** FBX 癒명떚由ъ뼹???대쫫, ?먮낯 ?ъ씤?? 蹂?섎맂 ?먯뀑 寃쎈줈瑜??④퍡 蹂닿??섎뒗 硫뷀? ?곗씠?곗씠?? */
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
 * FBX ?꾪룷???꾩껜媛 怨듭쑀?섎뒗 ?먮낯 ??硫뷀? 而⑦뀓?ㅽ듃?대떎.
 *
 * ?몃뱶, 硫붿떆, ?ㅽ궓, 蹂? ?ㅼ펷?덊넠, 癒명떚由ъ뼹??ID 湲곕컲?쇰줈 ?뺣━???섏쐞 ?뚯꽌?ㅼ씠 媛숈? 湲곗??? * ?ъ슜?섎룄濡??쒕떎. FBX SDK ?ъ씤?곗쓽 吏곸젒 ?쒗쉶 鍮꾩슜怨?以묐났 ?댁꽍??以꾩씠??以묒떖 ?곗씠?곗씠??
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
