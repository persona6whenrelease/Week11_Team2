/**
 * FBX 씬 전체를 보존하기 위한 에셋 데이터와 UObject 래퍼를 선언한다.
 *
 * FBX 원본의 계층형 컴포넌트, 정적 메시 배열, 스켈레탈 메시 배열, 라이트 정보를 하나의 단위로
 * 묶는다. FBX 내부 메시 참조를 나중에 다시 해석할 수 있도록 씬 에셋이 중심 인덱스 역할을 한다.
 */

#pragma once

#include "FBXImporter.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/StaticMesh.h"
#include "Object/Object.h"
#include "Serialization/Archive.h"

/**
 * FBX 씬 전체를 직렬화하기 위한 순수 데이터 묶음이다.
 *
 * 원본 파일 경로, 컴포넌트 계층, 메시 배열, 라이트 정보를 함께 저장한다. 개별 메시만으로는 표현할
 * 수 없는 원본 FBX의 씬 구성을 보존하는 단위이다.
 */
struct FFBXScene
{
    FString                         SourcePath;
    int64                           SourceTimestamp = 0;
    TArray<FStaticMesh>             StaticMeshes;
    TArray<FSkeletalMesh>           SkeletalMeshes;
    TArray<TArray<FStaticMaterial>> StaticMeshMaterials;
    TArray<TArray<FMeshMaterial>>   SkeletalMeshMaterials;
    TArray<FFBXSceneComponentDesc>  SceneComponents;
    TMap<int32, int32>              MeshIdToStaticMeshAssetIndex;
    TMap<int32, int32>              SkeletonIdToSkeletalMeshAssetIndex;

    void Serialize(FArchive &Ar)
    {
        Ar << SourcePath;
        Ar << SourceTimestamp;
        SerializeMeshArray(Ar, StaticMeshes);
        SerializeMeshArray(Ar, SkeletalMeshes);
        Ar << StaticMeshMaterials;
        Ar << SkeletalMeshMaterials;
        Ar << SceneComponents;
        SerializeIntMap(Ar, MeshIdToStaticMeshAssetIndex);
        SerializeIntMap(Ar, SkeletonIdToSkeletalMeshAssetIndex);
    }

  private:
    template <typename MeshType>
    static void SerializeMeshArray(FArchive &Ar, TArray<MeshType> &Meshes)
    {
        uint32 Count = static_cast<uint32>(Meshes.size());
        Ar << Count;
        if (Ar.IsLoading())
        {
            Meshes.resize(Count);
        }
        for (MeshType &Mesh : Meshes)
        {
            Mesh.Serialize(Ar);
        }
    }

    static void SerializeIntMap(FArchive &Ar, TMap<int32, int32> &Map)
    {
        uint32 Count = static_cast<uint32>(Map.size());
        Ar << Count;
        if (Ar.IsLoading())
        {
            Map.clear();
            for (uint32 Index = 0; Index < Count; ++Index)
            {
                int32 Key = -1;
                int32 Value = -1;
                Ar << Key;
                Ar << Value;
                Map[Key] = Value;
            }
            return;
        }

        for (auto &Pair : Map)
        {
            int32 Key = Pair.first;
            int32 Value = Pair.second;
            Ar << Key;
            Ar << Value;
        }
    }
};

/**
 * FFBXScene 데이터를 엔진 에셋으로 다루기 위한 UObject 래퍼이다.
 *
 * Content Browser와 Asset Editor가 FBX 씬 전체를 하나의 에셋으로 열고, 내부 메시 참조를 다시 해석할
 * 수 있도록 순수 씬 데이터와 엔진 객체 시스템을 연결한다.
 */
class UFBXSceneAsset : public UObject
{
  public:
    DECLARE_CLASS(UFBXSceneAsset, UObject)

    void           SetSourcePath(const FString &InSourcePath) { SourcePath = InSourcePath; }
    const FString &GetSourcePath() const { return SourcePath; }

    void AddStaticMesh(UStaticMesh *Mesh) { StaticMeshes.push_back(Mesh); }
    void AddSkeletalMesh(USkeletalMesh *Mesh) { SkeletalMeshes.push_back(Mesh); }
    void SetMeshIdToStaticMeshAssetIndex(TMap<int32, int32> &&InMeshIdToStaticMeshAssetIndex)
    {
        MeshIdToStaticMeshAssetIndex = std::move(InMeshIdToStaticMeshAssetIndex);
    }
    void
    SetSkeletonIdToSkeletalMeshAssetIndex(TMap<int32, int32> &&InSkeletonIdToSkeletalMeshAssetIndex)
    {
        SkeletonIdToSkeletalMeshAssetIndex = std::move(InSkeletonIdToSkeletalMeshAssetIndex);
    }
    void SetSceneComponents(TArray<FFBXSceneComponentDesc> &&InSceneComponents)
    {
        SceneComponents = std::move(InSceneComponents);
    }

    const TArray<UStaticMesh *>          &GetStaticMeshes() const { return StaticMeshes; }
    const TArray<USkeletalMesh *>        &GetSkeletalMeshes() const { return SkeletalMeshes; }
    const TArray<FFBXSceneComponentDesc> &GetSceneComponents() const { return SceneComponents; }
    UStaticMesh                          *FindStaticMeshBySourceMeshId(int32 SourceMeshId) const
    {
        const auto It = MeshIdToStaticMeshAssetIndex.find(SourceMeshId);
        if (It == MeshIdToStaticMeshAssetIndex.end())
        {
            return nullptr;
        }

        const int32 AssetIndex = It->second;
        if (AssetIndex < 0 || AssetIndex >= static_cast<int32>(StaticMeshes.size()))
        {
            return nullptr;
        }
        return StaticMeshes[AssetIndex];
    }
    USkeletalMesh *FindSkeletalMeshBySourceSkeletonId(int32 SourceSkeletonId) const
    {
        const auto It = SkeletonIdToSkeletalMeshAssetIndex.find(SourceSkeletonId);
        if (It == SkeletonIdToSkeletalMeshAssetIndex.end())
        {
            return nullptr;
        }

        const int32 AssetIndex = It->second;
        if (AssetIndex < 0 || AssetIndex >= static_cast<int32>(SkeletalMeshes.size()))
        {
            return nullptr;
        }
        return SkeletalMeshes[AssetIndex];
    }

  private:
    FString                        SourcePath;
    TArray<UStaticMesh *>          StaticMeshes;
    TArray<USkeletalMesh *>        SkeletalMeshes;
    TArray<FFBXSceneComponentDesc> SceneComponents;
    TMap<int32, int32>             MeshIdToStaticMeshAssetIndex;
    TMap<int32, int32>             SkeletonIdToSkeletalMeshAssetIndex;
};
