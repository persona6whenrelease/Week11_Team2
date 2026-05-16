/**
 * FBX 파일의 노드, 메시, 스킨, 본, 머티리얼 관계를 분석하는 메타 파서다.
 *
 * 실제 메시 지오메트리를 만들기 전에 어떤 노드가 스켈레톤이고, 어떤 메시가 skinned 또는
 * rigid attached 경로로 가야 하는지 분류할 수 있도록 공용 메타 데이터를 구축한다.
 */

#pragma once

#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Import/FBX/Core/FBXUtil.h"

/**
 * FBX 씬을 순회하며 임포트 전 단계에서 사용할 관계 메타를 구축한다.
 */
class FFbxMetaParser final
{
  public:
    FFbxMetaParser(FFbxImportMeta &InImportMeta) : ImportMeta(InImportMeta) {}
    ~FFbxMetaParser() = default;

    /**
     * FBX 씬을 순회하며 노드, 메시, 스킨, 본, 스켈레톤 관계 메타를 구축한다.
     */
    bool BuildFbxMeta(FbxScene *Scene);

  private:
    /**
     * FBX 노드 트리를 재귀적으로 등록하고 경로와 부모-자식 관계를 기록한다.
     */
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
    /**
     * skin에 속한 cluster를 등록하고 링크된 본 및 bind 정보를 기록한다.
     */
    int32 RegisterCluster(int32 SkinId, FbxCluster *Cluster);
    /**
     * 본으로 취급할 FBX 노드를 등록하고 참조 여부와 삽입 사유를 기록한다.
     */
    int32 RegisterBoneNode(FbxNode *Node, bool bReferencedByCluster, bool bInsertedAsParentChain);

  private:
    bool  IsSceneRootNode(FbxNode *Node) const;
    bool  CanPromoteNodeToBoneParent(FbxNode *Node) const;
    void  LinkBoneParentChild(int32 ParentBoneId, int32 ChildBoneId);
    void  LinkBoneToNearestValidParent(int32 BoneId);
    int32 FindTopRootBone(int32 BoneId) const;

  private:
    /**
     * 루트 본 기준으로 기존 스켈레톤을 재사용하거나 새 스켈레톤 메타를 만든다.
     */
    int32 FindOrCreateSkeletonForRoot(int32 RootBoneId, bool bBuiltFromSkinClusters,
                                      bool                bHasSingleRoot,
                                      TMap<int32, int32> &RootBoneIdToSkeletonId);
    void  AddBoneDfs(int32 CurrentBoneId, FFbxSkeletonMeta &SkeletonMeta, uint32 SkeletonId);
    int32 FindSkeletonRootBoneForSkin(const TArray<int32> &BoneIds) const;
    bool  ShouldBuildRigidSkeletonForRoot(int32 RootBoneId) const;
    int32 FindSkeletonIdForBone(int32 BoneId) const;
    /**
     * 노드의 부모 체인을 따라가며 가장 가까운 상위 본을 찾는다.
     */
    int32 FindNearestParentBoneIdForNode(FbxNode *Node) const;

  private:
    FFbxImportMeta &ImportMeta;
};
