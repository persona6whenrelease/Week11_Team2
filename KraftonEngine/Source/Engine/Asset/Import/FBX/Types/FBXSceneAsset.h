/**
 * 하나의 FBX 파일에서 생성된 하위 에셋들을 씬 단위로 저장하는 UObject를 선언한다.
 *
 * FBX 하나는 여러 StaticMesh, SkeletalMesh, Skeleton, AnimSequence를 포함할 수 있다. UFBXSceneAsset은
 * 이 결과물을 한 캐시 파일에 묶고, FBX 노드 ID와 하위 에셋 인덱스의 대응 관계를 함께 저장해 나중에 특정
 * 하위 에셋만 다시 꺼낼 수 있게 한다.
 */

#pragma once

#include "Asset/Import/FBX/Core/FBXImporter.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Core/Skeleton.h"
#include "Asset/Mesh/StaticMesh/StaticMesh.h"
#include "Object/Object.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"
#include "FBXSceneAsset.generated.h"

/**
 * FBX 하나에서 생성된 여러 하위 에셋과 씬 컴포넌트 설명을 묶는 직렬화 구조이다.
 */
struct FFBXScene
{
    FString                         SourcePath;
    int64                           SourceTimestamp = 0;
    TArray<FStaticMesh>             StaticMeshes;
    TArray<FSkeletalMesh>           SkeletalMeshes;
    TArray<FSkeleton>               Skeletons;
    TArray<TArray<UAnimSequence *>> AnimSequences;
    TArray<TArray<FStaticMaterial>> StaticMeshMaterials;
    TArray<TArray<FMeshMaterial>>   SkeletalMeshMaterials;
    TArray<FFBXSceneComponentDesc>  SceneComponents;
    TMap<int32, int32>              MeshIdToStaticMeshAssetIndex;
    TMap<int32, int32>              SkeletonIdToSkeletalMeshAssetIndex;

    /**
     * 에셋 헤더 검증과 본문 데이터 저장/로드를 함께 처리한다.
     */
    void Serialize(FArchive &Ar)
    {
        Ar << SourcePath;
        Ar << SourceTimestamp;
        SerializeMeshArray(Ar, StaticMeshes);
        SerializeMeshArray(Ar, SkeletalMeshes);
        Ar << Skeletons;
        SerializeAnimSequenceArray(Ar, AnimSequences);
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



    static void SerializeAnimSequenceArray(FArchive &Ar, TArray<TArray<UAnimSequence*>> &Sequences)
    {
        uint32 OuterCount = static_cast<uint32>(Sequences.size());
        Ar << OuterCount;
        if (Ar.IsLoading())
        {
            Sequences.clear();
            Sequences.resize(OuterCount);
        }

        for (uint32 OuterIndex = 0; OuterIndex < OuterCount; ++OuterIndex)
        {
            uint32 InnerCount = static_cast<uint32>(Sequences[OuterIndex].size());
            Ar << InnerCount;
            if (Ar.IsLoading())
            {
                Sequences[OuterIndex].clear();
                Sequences[OuterIndex].resize(InnerCount, nullptr);
            }

            for (uint32 InnerIndex = 0; InnerIndex < InnerCount; ++InnerIndex)
            {
                if (Ar.IsLoading() && !Sequences[OuterIndex][InnerIndex])
                {
                    Sequences[OuterIndex][InnerIndex] = UObjectManager::Get().CreateObject<UAnimSequence>();
                }
                if (Sequences[OuterIndex][InnerIndex])
                {
                    Sequences[OuterIndex][InnerIndex]->Serialize(Ar);
                }
            }
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
 * FBX 씬 캐시를 UObject로 저장해 하위 에셋을 나중에 다시 꺼낼 수 있게 하는 타입이다.
 */
UCLASS()
class UFBXSceneAsset : public UObject
{
  public:
    GENERATED_BODY()

    void           SetSourcePath(const FString &InSourcePath) { SourcePath = InSourcePath; }
    const FString &GetSourcePath() const { return SourcePath; }

    void AddStaticMesh(UStaticMesh *Mesh) { StaticMeshes.push_back(Mesh); }
    void AddSkeletalMesh(USkeletalMesh *Mesh) { SkeletalMeshes.push_back(Mesh); }
    void AddSkeleton(USkeleton *Skeleton) { Skeletons.push_back(Skeleton); }
    void AddAnimSequence(UAnimSequence *Sequence) { AnimSequences.push_back(Sequence); }
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
    const TArray<USkeleton *>            &GetSkeletons() const { return Skeletons; }
    const TArray<UAnimSequence *>        &GetAnimSequences() const { return AnimSequences; }
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
    TArray<USkeleton *>            Skeletons;
    TArray<UAnimSequence *>        AnimSequences;
    TArray<FFBXSceneComponentDesc> SceneComponents;
    TMap<int32, int32>             MeshIdToStaticMeshAssetIndex;
    TMap<int32, int32>             SkeletonIdToSkeletalMeshAssetIndex;
};
