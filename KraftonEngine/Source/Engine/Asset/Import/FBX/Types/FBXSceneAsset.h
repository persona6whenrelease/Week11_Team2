/**
 * FBX ???꾩껜瑜?蹂댁〈?섍린 ?꾪븳 ?먯뀑 ?곗씠?곗? UObject ?섑띁瑜??좎뼵?쒕떎.
 *
 * FBX ?먮낯??怨꾩링??而댄룷?뚰듃, ?뺤쟻 硫붿떆 諛곗뿴, ?ㅼ펷?덊깉 硫붿떆 諛곗뿴, ?쇱씠???뺣낫瑜??섎굹???⑥쐞濡? * 臾띕뒗?? FBX ?대? 硫붿떆 李몄“瑜??섏쨷???ㅼ떆 ?댁꽍?????덈룄濡????먯뀑??以묒떖 ?몃뜳????븷???쒕떎.
 */

#pragma once

#include "Asset/Import/FBX/Core/FBXImporter.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Asset/Mesh/StaticMesh/StaticMesh.h"
#include "Object/Object.h"
#include "Serialization/Archive.h"

/**
 * FBX ???꾩껜瑜?吏곷젹?뷀븯湲??꾪븳 ?쒖닔 ?곗씠??臾띠쓬?대떎.
 *
 * ?먮낯 ?뚯씪 寃쎈줈, 而댄룷?뚰듃 怨꾩링, 硫붿떆 諛곗뿴, ?쇱씠???뺣낫瑜??④퍡 ??ν븳?? 媛쒕퀎 硫붿떆留뚯쑝濡쒕뒗 ?쒗쁽?? * ???녿뒗 ?먮낯 FBX????援ъ꽦??蹂댁〈?섎뒗 ?⑥쐞?대떎.
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
 * FFBXScene ?곗씠?곕? ?붿쭊 ?먯뀑?쇰줈 ?ㅻ（湲??꾪븳 UObject ?섑띁?대떎.
 *
 * Content Browser? Asset Editor媛 FBX ???꾩껜瑜??섎굹???먯뀑?쇰줈 ?닿퀬, ?대? 硫붿떆 李몄“瑜??ㅼ떆 ?댁꽍?? * ???덈룄濡??쒖닔 ???곗씠?곗? ?붿쭊 媛앹껜 ?쒖뒪?쒖쓣 ?곌껐?쒕떎.
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
