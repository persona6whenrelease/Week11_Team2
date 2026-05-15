/**
 * FBX ??硫뷀? ?곗씠???섏쭛 ?뚯꽌瑜??좎뼵?쒕떎.
 *
 * ???뚯꽌???ㅼ젣 ?뺤젏?대굹 ?좊땲硫붿씠???섑뵆??留뚮뱾湲??꾩뿉 ?먮낯 ?ъ쓽 援ъ“? 李몄“ 愿怨꾨? ID 湲곕컲?쇰줈
 * ?뺢퇋?뷀븳?? ?щ윭 ?섏쐞 ?뚯꽌媛 媛숈? ?몃뱶/硫붿떆/?ㅼ펷?덊넠 ?뺣낫瑜??덉젙?곸쑝濡?怨듭쑀?섍린 ?꾪븳 ?좏뻾
 * ?④퀎?대떎.
 */

#pragma once
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Import/FBX/Core/FBXUtil.h"

/**
 * FBX ?ъ쓣 ?쒗쉶?섏뿬 ?몃뱶/硫붿떆/?ㅼ펷?덊넠/癒명떚由ъ뼹 硫뷀? ?뺣낫瑜??섏쭛?섎뒗 ?뚯꽌?대떎.
 *
 * ?ㅼ젣 ?먯뀑 ?곗씠???앹꽦 ?꾩뿉 ?먮낯 援ъ“瑜?ID 湲곕컲?쇰줈 ?뺢퇋?뷀븳?? ???좏뻾 ?묒뾽 ?뺣텇???댄썑 ?뚯꽌?ㅼ?
 * FBX SDK 怨꾩링??諛섎났 ?먯깋?섏? ?딄퀬 ImportMeta留?李몄“???묒뾽?????덈떎.
 */
class FFbxMetaParser final
{
  public:
    FFbxMetaParser(FFbxImportMeta &InImportMeta) : ImportMeta(InImportMeta) {}
    ~FFbxMetaParser() = default;

    bool BuildFbxMeta(FbxScene *Scene);

  private:
    int32 RegisterNodeRecursive(FbxNode *Node, int32 ParentNodeId, const FString &ParentPath);
    void  RegisterSkinsForMesh(int32 MeshId);
    void  EnsureBoneParentChain(int32 BoneId);
    void  BuildRegisteredBoneHierarchyLinks();
    void  BuildSkeletonTables();
    void  AttachRigidMeshesToSkeletons();
    void  ClassifyMeshes();
    bool  ValidateFbxMeta() const;

  private:
    void  RegisterMeshFromNode(FbxNode *Node, int32 NodeId);
    int32 RegisterMaterial(FbxSurfaceMaterial *SurfaceMaterial);
    int32 RegisterCluster(int32 SkinId, FbxCluster *Cluster);
    int32 RegisterBoneNode(FbxNode *Node, bool bReferencedByCluster, bool bInsertedAsParentChain);

  private:
    bool  IsSceneRootNode(FbxNode *Node) const;
    bool  CanPromoteNodeToBoneParent(FbxNode *Node) const;
    void  LinkBoneParentChild(int32 ParentBoneId, int32 ChildBoneId);
    void  LinkBoneToNearestValidParent(int32 BoneId);
    int32 FindTopRootBone(int32 BoneId) const;

  private:
    int32 FindOrCreateSkeletonForRoot(int32 RootBoneId, bool bBuiltFromSkinClusters,
                                      bool                bHasSingleRoot,
                                      TMap<int32, int32> &RootBoneIdToSkeletonId);
    void  AddBoneDfs(int32 CurrentBoneId, FFbxSkeletonMeta &SkeletonMeta, uint32 SkeletonId);
    int32 FindSkeletonRootBoneForSkin(const TArray<int32> &BoneIds) const;
    bool  ShouldBuildRigidSkeletonForRoot(int32 RootBoneId) const;
    int32 FindSkeletonIdForBone(int32 BoneId) const;
    int32 FindNearestParentBoneIdForNode(FbxNode *Node) const;

  private:
    FFbxImportMeta &ImportMeta;
};
