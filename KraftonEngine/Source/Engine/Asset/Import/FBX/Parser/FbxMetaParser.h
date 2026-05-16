/**
 * FBX 씬의 노드, 메시, 스킨, 본, 머티리얼 관계를 분석하는 메타 파서를 선언한다.
 *
 * 실제 메시 지오메트리를 만들기 전에 어떤 노드가 스켈레톤이고, 어떤 메시가 skinned 또는 rigid attached
 * 인지 분류해야 한다. 이 파서는 FBX 씬 전체를 순회하며 이후 파서들이 공유할 식별자와 연결 정보를 만든다.
 */

#pragma once
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Import/FBX/Core/FBXUtil.h"

/**
 * FBX 씬을 파싱하기 전에 노드, 본, 스킨, 머티리얼 관계를 분석하는 메타 파서이다.
 *
 * 이 단계에서 메시가 static인지 skinned인지, rigid attachment인지 결정되며 이후 파서들은 이 분류 결과를
 * 기준으로 필요한 데이터만 읽는다.
 */
class FFbxMetaParser final
{
  public:
    FFbxMetaParser(FFbxImportMeta &InImportMeta) : ImportMeta(InImportMeta) {}
    ~FFbxMetaParser() = default;

    /**
     * FBX 씬 전체를 순회해 이후 임포트 단계가 공유할 메타 정보를 구성한다.
     */
    bool BuildFbxMeta(FbxScene *Scene);

  private:
    /**
     * FBX 노드 계층을 재귀적으로 등록하고 부모/자식 관계를 메타 데이터에 반영한다.
     */
    int32 RegisterNodeRecursive(FbxNode *Node, int32 ParentNodeId, const FString &ParentPath);
    /**
     * FBX 씬에서 발견한 객체를 메타 데이터 테이블에 등록한다.
     */
    void  RegisterSkinsForMesh(int32 MeshId);
    void  EnsureBoneParentChain(int32 BoneId);
    /**
     * 임포트 중간 데이터에서 다음 단계가 사용할 구조를 구성한다.
     */
    void  BuildRegisteredBoneHierarchyLinks();
    /**
     * 임포트 중간 데이터에서 다음 단계가 사용할 구조를 구성한다.
     */
    void  BuildSkeletonTables();
    void  AttachRigidMeshesToSkeletons();
    /**
     * 등록된 메시가 static, skinned, rigid attachment 중 어느 경로로 처리될지 결정한다.
     */
    void  ClassifyMeshes();
    /**
     * 파싱 전 메타 데이터의 연결 관계가 유효한지 검사한다.
     */
    bool  ValidateFbxMeta() const;

  private:
    /**
     * FBX 씬에서 발견한 객체를 메타 데이터 테이블에 등록한다.
     */
    void  RegisterMeshFromNode(FbxNode *Node, int32 NodeId);
    /**
     * FBX 씬에서 발견한 객체를 메타 데이터 테이블에 등록한다.
     */
    int32 RegisterMaterial(FbxSurfaceMaterial *SurfaceMaterial);
    /**
     * FBX 씬에서 발견한 객체를 메타 데이터 테이블에 등록한다.
     */
    int32 RegisterCluster(int32 SkinId, FbxCluster *Cluster);
    /**
     * FBX 씬에서 발견한 객체를 메타 데이터 테이블에 등록한다.
     */
    int32 RegisterBoneNode(FbxNode *Node, bool bReferencedByCluster, bool bInsertedAsParentChain);

  private:
    bool  IsSceneRootNode(FbxNode *Node) const;
    bool  CanPromoteNodeToBoneParent(FbxNode *Node) const;
    void  LinkBoneParentChild(int32 ParentBoneId, int32 ChildBoneId);
    void  LinkBoneToNearestValidParent(int32 BoneId);
    /**
     * 이미 수집된 메타 데이터에서 조건에 맞는 항목을 찾아 반환한다.
     */
    int32 FindTopRootBone(int32 BoneId) const;

  private:
    int32 FindOrCreateSkeletonForRoot(int32 RootBoneId, bool bBuiltFromSkinClusters,
                                      bool                bHasSingleRoot,
                                      TMap<int32, int32> &RootBoneIdToSkeletonId);
    void  AddBoneDfs(int32 CurrentBoneId, FFbxSkeletonMeta &SkeletonMeta, uint32 SkeletonId);
    /**
     * 이미 수집된 메타 데이터에서 조건에 맞는 항목을 찾아 반환한다.
     */
    int32 FindSkeletonRootBoneForSkin(const TArray<int32> &BoneIds) const;
    bool  ShouldBuildRigidSkeletonForRoot(int32 RootBoneId) const;
    /**
     * 이미 수집된 메타 데이터에서 조건에 맞는 항목을 찾아 반환한다.
     */
    int32 FindSkeletonIdForBone(int32 BoneId) const;
    /**
     * 이미 수집된 메타 데이터에서 조건에 맞는 항목을 찾아 반환한다.
     */
    int32 FindNearestParentBoneIdForNode(FbxNode *Node) const;

  private:
    FFbxImportMeta &ImportMeta;
};
