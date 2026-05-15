/**
 * FBX 씬 메타 데이터 수집 파서를 선언한다.
 *
 * 이 파서는 실제 정점이나 애니메이션 샘플을 만들기 전에 원본 씬의 구조와 참조 관계를 ID 기반으로
 * 정규화한다. 여러 하위 파서가 같은 노드/메시/스켈레톤 정보를 안정적으로 공유하기 위한 선행
 * 단계이다.
 */

#pragma once
#include "FBXImportMeta.h"
#include "FBXUtil.h"

/**
 * FBX 씬을 순회하여 노드/메시/스켈레톤/머티리얼 메타 정보를 수집하는 파서이다.
 *
 * 실제 에셋 데이터 생성 전에 원본 구조를 ID 기반으로 정규화한다. 이 선행 작업 덕분에 이후 파서들은
 * FBX SDK 계층을 반복 탐색하지 않고 ImportMeta만 참조해 작업할 수 있다.
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
