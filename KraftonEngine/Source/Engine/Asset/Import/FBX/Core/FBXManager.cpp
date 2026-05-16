/**
 * FBX 씬 캐시 저장, 로드, 하위 에셋 참조 해석을 구현한다.
 *
 * 원본 FBX를 매번 파싱하지 않도록 별도의 바이너리 캐시를 사용하고, 캐시 헤더에는 원본 경로와 수정 시간을
 * 기록해 오래된 캐시를 걸러낸다. 또한 씬 단위로 저장된 여러 StaticMesh/SkeletalMesh 중 요청된 하위
 * 에셋을 찾아 UObject로 복원한다.
 */

#include "Asset/Import/FBX/Core/FBXManager.h"

#include "Core/Log.h"
#include "Engine/Platform/Paths.h"
#include "Engine/Runtime/Engine.h"
#include "Asset/Import/FBX/Core/FBXImporter.h"
#include "Asset/Import/FBX/Types/FBXSceneAsset.h"
#include "Asset/Import/MeshManager.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Asset/Mesh/StaticMesh/StaticMesh.h"
#include "Object/Object.h"
#include "Serialization/WindowsArchive.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <filesystem>

TMap<FString, UFBXSceneAsset *> FFBXManager::FbxSceneCache;
TArray<FMeshAssetListItem>      FFBXManager::AvailableFbxFiles;

namespace
{
    /**
     * FBX 씬 캐시가 어떤 원본 파일과 버전을 기준으로 만들어졌는지 기록하는 헤더이다.
     */
    struct FFBXSceneCacheHeader
    {
        uint32  Magic = 0;
        uint32  Version = 0;
        FString SourcePath;
        int64   SourceTimestamp = 0;
    };

    /**
     * FBX 씬 캐시 로드 결과가 성공, 누락, 오래됨, 손상 중 어디에 해당하는지 나타낸다.
     */
    enum class EFBXSceneCacheStatus
    {
        MemoryHit,
        DiskHit,
        CacheMiss,
        CacheStale,
        CacheInvalid,
        RebuildFailed,
    };

    const char *ToLogString(EFBXSceneCacheStatus Status)
    {
        switch (Status)
        {
        case EFBXSceneCacheStatus::MemoryHit:
            return "memory hit";
        case EFBXSceneCacheStatus::DiskHit:
            return "disk hit";
        case EFBXSceneCacheStatus::CacheMiss:
            return "cache miss";
        case EFBXSceneCacheStatus::CacheStale:
            return "cache rebuild";
        case EFBXSceneCacheStatus::CacheInvalid:
            return "cache rebuild";
        case EFBXSceneCacheStatus::RebuildFailed:
            return "cache rebuild failed";
        default:
            return "unknown";
        }
    }

    /**
     * 저장된 참조 문자열이나 후보 경로를 실제 에셋/파일 경로로 해석한다.
     */
    std::wstring ResolveDiskPath(const FString &Path)
    {
        std::wstring DiskPath;
        FString      ResolveError;
        if (!FPaths::TryResolvePackagePath(Path, DiskPath, &ResolveError))
        {
            DiskPath = FPaths::ToWide(Path);
        }
        return DiskPath;
    }

    FString NormalizePackagePath(const FString &Path)
    {
        std::filesystem::path Normalized(FPaths::ToWide(Path));
        return FPaths::ToUtf8(Normalized.lexically_normal().generic_wstring());
    }

    std::wstring ToLower(std::wstring Text)
    {
        std::transform(Text.begin(), Text.end(), Text.begin(),
                       [](wchar_t Ch) { return static_cast<wchar_t>(std::towlower(Ch)); });
        return Text;
    }

    int64 GetFileTimestamp(const FString &Path)
    {
        const std::filesystem::path DiskPath(ResolveDiskPath(Path));
        if (!std::filesystem::exists(DiskPath))
        {
            return 0;
        }

        return static_cast<int64>(
            std::filesystem::last_write_time(DiskPath).time_since_epoch().count());
    }

    bool IsFbxPath(const FString &Path)
    {
        std::filesystem::path FsPath(FPaths::ToWide(Path));
        return ToLower(FsPath.extension().wstring()) == L".fbx";
    }

    bool IsBinPath(const FString &Path)
    {
        std::filesystem::path FsPath(FPaths::ToWide(Path));
        return ToLower(FsPath.extension().wstring()) == L".bin";
    }

    bool ParseFbxSceneSubAssetReference(const FString &Path, const char *Marker,
                                        FString &OutSourcePath, int32 &OutSourceId)
    {
        const size_t MarkerPos = Path.find(Marker);
        if (MarkerPos == FString::npos)
        {
            return false;
        }

        OutSourcePath = Path.substr(0, MarkerPos);
        const FString IdText = Path.substr(MarkerPos + std::strlen(Marker));
        if (OutSourcePath.empty() || IdText.empty())
        {
            return false;
        }

        char      *End = nullptr;
        const long ParsedId = std::strtol(IdText.c_str(), &End, 10);
        if (!End || *End != '\0' || ParsedId < 0 || ParsedId > INT32_MAX)
        {
            return false;
        }

        OutSourceId = static_cast<int32>(ParsedId);
        return true;
    }

    void SerializeCacheHeader(FArchive &Ar, FFBXSceneCacheHeader &Header)
    {
        Ar << Header.Magic;
        Ar << Header.Version;
        Ar << Header.SourcePath;
        Ar << Header.SourceTimestamp;
    }

    bool IsCacheHeaderUsable(const FString &RequestedPath, const FFBXSceneCacheHeader &Header)
    {
        if (Header.Magic != FMeshManager::FbxSceneCacheMagic ||
            Header.Version != FMeshManager::FbxSceneCacheVersion)
        {
            return false;
        }
        if (NormalizePackagePath(Header.SourcePath) != NormalizePackagePath(RequestedPath))
        {
            return false;
        }

        const int64 CurrentSourceTimestamp = GetFileTimestamp(RequestedPath);
        return CurrentSourceTimestamp != 0 && Header.SourceTimestamp >= CurrentSourceTimestamp;
    }

    bool TryLoadSceneFromCache(const FString &SourcePath, FFBXScene &OutScene,
                               EFBXSceneCacheStatus &OutStatus)
    {
        const FString               CachePath = FMeshManager::GetFbxSceneCacheFilePath(SourcePath);
        const std::filesystem::path CacheDiskPath(ResolveDiskPath(CachePath));
        if (!std::filesystem::exists(CacheDiskPath))
        {
            OutStatus = EFBXSceneCacheStatus::CacheMiss;
            return false;
        }

        /**
         * 원본 포맷에서 필요한 속성 값을 읽어 엔진 타입으로 변환한다.
         */
        FWindowsBinReader Reader(CachePath);
        if (!Reader.IsValid())
        {
            OutStatus = EFBXSceneCacheStatus::CacheInvalid;
            return false;
        }

        FFBXSceneCacheHeader Header;
        SerializeCacheHeader(Reader, Header);
        if (Header.Magic != FMeshManager::FbxSceneCacheMagic ||
            Header.Version != FMeshManager::FbxSceneCacheVersion)
        {
            OutStatus = EFBXSceneCacheStatus::CacheInvalid;
            return false;
        }
        if (!IsCacheHeaderUsable(SourcePath, Header))
        {
            OutStatus = EFBXSceneCacheStatus::CacheStale;
            return false;
        }

        OutScene.Serialize(Reader);
        OutScene.SourcePath = Header.SourcePath;
        OutScene.SourceTimestamp = Header.SourceTimestamp;
        OutStatus = EFBXSceneCacheStatus::DiskHit;
        return true;
    }

    /**
     * 임포트 결과 씬을 다음 로드에서 재사용할 수 있도록 바이너리 캐시에 저장한다.
     */
    void SaveSceneToCache(FFBXScene &Scene)
    {
        const FString     CachePath = FMeshManager::GetFbxSceneCacheFilePath(Scene.SourcePath);
        FWindowsBinWriter Writer(CachePath);
        if (!Writer.IsValid())
        {
            UE_LOG("[FBXManager] Failed to open FBX scene cache for writing: %s",
                   CachePath.c_str());
            return;
        }

        FFBXSceneCacheHeader Header;
        Header.Magic = FMeshManager::FbxSceneCacheMagic;
        Header.Version = FMeshManager::FbxSceneCacheVersion;
        Header.SourcePath = NormalizePackagePath(Scene.SourcePath);
        Header.SourceTimestamp = Scene.SourceTimestamp;
        SerializeCacheHeader(Writer, Header);
        Scene.Serialize(Writer);
    }

    /**
     * FBX 임포트 중간 결과를 직렬화 가능한 UFBXSceneAsset 데이터로 변환한다.
     */
    FFBXScene ConvertImportedAssetToScene(const FString &SourcePath, FFBXAsset &&ImportedAsset)
    {
        FFBXScene Scene;
        Scene.SourcePath = NormalizePackagePath(SourcePath);
        Scene.SourceTimestamp = GetFileTimestamp(SourcePath);
        Scene.StaticMeshes = std::move(ImportedAsset.StaticMeshes);
        Scene.SkeletalMeshes = std::move(ImportedAsset.SkeletalMeshes);
        Scene.Skeletons = std::move(ImportedAsset.Skeletons);
        Scene.AnimSequences = std::move(ImportedAsset.AnimSequences);
        for (FStaticMesh &Mesh : Scene.StaticMeshes)
        {
            Mesh.RenderBuffer.reset();
        }
        Scene.StaticMeshMaterials = std::move(ImportedAsset.StaticMeshMaterials);
        Scene.SkeletalMeshMaterials = std::move(ImportedAsset.SkeletalMeshMaterials);
        Scene.SceneComponents = std::move(ImportedAsset.SceneComponents);
        Scene.MeshIdToStaticMeshAssetIndex = std::move(ImportedAsset.MeshIdToStaticMeshAssetIndex);
        Scene.SkeletonIdToSkeletalMeshAssetIndex =
            std::move(ImportedAsset.SkeletonIdToSkeletalMeshAssetIndex);
        return Scene;
    }

    UFBXSceneAsset *CreateSceneAssetFromScene(FFBXScene &&Scene)
    {
        UFBXSceneAsset *SceneAsset = UObjectManager::Get().CreateObject<UFBXSceneAsset>();
        SceneAsset->SetSourcePath(Scene.SourcePath);

        ID3D11Device *Device =
            GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
        for (int32 StaticMeshIndex = 0;
             StaticMeshIndex < static_cast<int32>(Scene.StaticMeshes.size()); ++StaticMeshIndex)
        {
            UStaticMesh *StaticMesh = UObjectManager::Get().CreateObject<UStaticMesh>(SceneAsset);
            TArray<FStaticMaterial> Materials;
            if (StaticMeshIndex < static_cast<int32>(Scene.StaticMeshMaterials.size()))
            {
                Materials = std::move(Scene.StaticMeshMaterials[StaticMeshIndex]);
            }
            StaticMesh->SetStaticMaterials(std::move(Materials));

            FStaticMesh *MeshAsset =
                new FStaticMesh(std::move(Scene.StaticMeshes[StaticMeshIndex]));
            if (!MeshAsset->bBoundsValid)
            {
                MeshAsset->CacheBounds();
            }
            StaticMesh->SetStaticMeshAsset(MeshAsset);
            if (Device)
            {
                StaticMesh->InitResources(Device);
            }
            SceneAsset->AddStaticMesh(StaticMesh);
        }

        for (int32 SkeletalMeshIndex = 0;
             SkeletalMeshIndex < static_cast<int32>(Scene.SkeletalMeshes.size());
             ++SkeletalMeshIndex)
        {
            USkeletalMesh *SkeletalMesh =
                UObjectManager::Get().CreateObject<USkeletalMesh>(SceneAsset);
            TArray<FMeshMaterial> Materials;
            if (SkeletalMeshIndex < static_cast<int32>(Scene.SkeletalMeshMaterials.size()))
            {
                Materials = std::move(Scene.SkeletalMeshMaterials[SkeletalMeshIndex]);
            }
            SkeletalMesh->SetMaterials(std::move(Materials));

            FSkeletalMesh *MeshAsset =
                new FSkeletalMesh(std::move(Scene.SkeletalMeshes[SkeletalMeshIndex]));
            if (!MeshAsset->bBoundsValid)
            {
                MeshAsset->CacheBounds();
            }
            SkeletalMesh->SetSkeletalMeshAsset(MeshAsset);
            SceneAsset->AddSkeletalMesh(SkeletalMesh);

            if (SkeletalMeshIndex < static_cast<int32>(Scene.Skeletons.size()))
            {
                USkeleton* Skeleton = UObjectManager::Get().CreateObject<USkeleton>(SceneAsset);
                Skeleton->SetBones(std::move(Scene.Skeletons[SkeletalMeshIndex]));
                SceneAsset->AddSkeleton(Skeleton);
            }
            if (SkeletalMeshIndex < static_cast<int32>(Scene.AnimSequences.size()))
            {
                for (FAnimationClip& Clip : Scene.AnimSequences[SkeletalMeshIndex])
                {
                    UAnimSequence* AnimSequence = UObjectManager::Get().CreateObject<UAnimSequence>(SceneAsset);
                    AnimSequence->SetSkeletonAssetPath(MeshAsset->SkeletonAssetPath);
                    AnimSequence->SetAnimationClip(std::move(Clip));
                    SceneAsset->AddAnimSequence(AnimSequence);
                }
            }

            const FVector &BC = MeshAsset->BoundsCenter;
            const FVector &BE = MeshAsset->BoundsExtent;
            UE_LOG("[FBXManager] SkeletalMesh[%d] BoundsCenter=(%.2f, %.2f, %.2f) "
                   "BoundsExtent=(%.2f, %.2f, %.2f) "
                   "ApproxHeight=%.1fcm ApproxWidthX=%.1fcm Diagonal=%.1fcm",
                   SkeletalMeshIndex, BC.X, BC.Y, BC.Z, BE.X, BE.Y, BE.Z, BE.Z * 2.0f, BE.X * 2.0f,
                   BE.Length());

            UE_LOG("[FBXManager] SkeletalMesh[%d] BoneCount=%u", SkeletalMeshIndex,
                   static_cast<uint32>(MeshAsset->Bones.size()));
            for (size_t BoneIdx = 0; BoneIdx < MeshAsset->Bones.size(); ++BoneIdx)
            {
                const FBoneInfo &B = MeshAsset->Bones[BoneIdx];
                UE_LOG("[FBXManager]   Bone[%zu] Parent=%d Name='%s'", BoneIdx, B.ParentIndex,
                       B.Name.c_str());
            }
        }

        SceneAsset->SetMeshIdToStaticMeshAssetIndex(std::move(Scene.MeshIdToStaticMeshAssetIndex));
        SceneAsset->SetSkeletonIdToSkeletalMeshAssetIndex(
            std::move(Scene.SkeletonIdToSkeletalMeshAssetIndex));
        SceneAsset->SetSceneComponents(std::move(Scene.SceneComponents));
        return SceneAsset;
    }

    void LogSceneLoad(const FString &SourcePath, const UFBXSceneAsset *SceneAsset,
                      EFBXSceneCacheStatus Status)
    {
        UE_LOG("[FBXManager] Loaded FBX scene. Path=%s StaticMeshes=%u SkeletalMeshes=%u "
               "Components=%u Cache=%s",
               SourcePath.c_str(),
               SceneAsset ? static_cast<uint32>(SceneAsset->GetStaticMeshes().size()) : 0u,
               SceneAsset ? static_cast<uint32>(SceneAsset->GetSkeletalMeshes().size()) : 0u,
               SceneAsset ? static_cast<uint32>(SceneAsset->GetSceneComponents().size()) : 0u,
               ToLogString(Status));
    }

    void AddFilesWithExtension(const std::filesystem::path &Root, const std::wstring &Extension,
                               TArray<FMeshAssetListItem> &OutFiles)
    {
        OutFiles.clear();
        if (!std::filesystem::exists(Root))
        {
            return;
        }

        const std::filesystem::path ProjectRoot(FPaths::RootDir());
        for (const auto &Entry : std::filesystem::recursive_directory_iterator(Root))
        {
            if (!Entry.is_regular_file())
            {
                continue;
            }

            const std::filesystem::path &Path = Entry.path();
            if (ToLower(Path.extension().wstring()) != Extension)
            {
                continue;
            }

            FMeshAssetListItem Item;
            Item.DisplayName = FPaths::ToUtf8(Path.filename().wstring());
            Item.FullPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());
            OutFiles.push_back(std::move(Item));
        }
    }
} 

USkeletalMesh *FFBXManager::LoadSkeletalMesh(const FString &PathFileName)
{
    if (PathFileName.empty() || PathFileName == "None")
    {
        return nullptr;
    }

    FString SourcePath;
    int32   SourceSkeletonId = -1;
    if (ParseFbxSceneSubAssetReference(PathFileName, "#Skeleton_", SourcePath, SourceSkeletonId))
    {
        return ResolveSkeletalMeshReference(PathFileName);
    }

    if (IsFbxPath(PathFileName))
    {
        UE_LOG("[FBXManager] Plain FBX path cannot be loaded as a skeletal mesh. Use #Skeleton_ID "
               "reference: %s",
               PathFileName.c_str());
        return nullptr;
    }

    if (IsBinPath(PathFileName))
    {
        UE_LOG("[FBXManager] Legacy skeletal mesh bin cache is no longer supported: %s",
               PathFileName.c_str());
        return nullptr;
    }

    UE_LOG("[FBXManager] Unsupported skeletal mesh path: %s", PathFileName.c_str());
    return nullptr;
}

UFBXSceneAsset *FFBXManager::LoadFbxScene(const FString &PathFileName)
{
    if (PathFileName.empty() || PathFileName == "None" || !IsFbxPath(PathFileName))
    {
        return nullptr;
    }

    const FString CacheKey = NormalizePackagePath(PathFileName);
    auto          It = FbxSceneCache.find(CacheKey);
    if (It != FbxSceneCache.end())
    {
        LogSceneLoad(CacheKey, It->second, EFBXSceneCacheStatus::MemoryHit);
        return It->second;
    }

    FFBXScene            Scene;
    EFBXSceneCacheStatus CacheStatus = EFBXSceneCacheStatus::CacheMiss;
    if (!TryLoadSceneFromCache(PathFileName, Scene, CacheStatus))
    {
        FFBXAsset   ImportedAsset;
        FBXImporter Importer;
        if (!Importer.ImportFbxAsset(PathFileName, ImportedAsset))
        {
            UE_LOG("[FBXManager] FBX scene import failed. Path=%s Cache=%s", PathFileName.c_str(),
                   ToLogString(CacheStatus));
            return nullptr;
        }

        Scene = ConvertImportedAssetToScene(PathFileName, std::move(ImportedAsset));
        SaveSceneToCache(Scene);
    }

    UFBXSceneAsset *SceneAsset = CreateSceneAssetFromScene(std::move(Scene));
    FbxSceneCache[CacheKey] = SceneAsset;
    LogSceneLoad(CacheKey, SceneAsset, CacheStatus);
    return SceneAsset;
}

UStaticMesh *FFBXManager::ResolveStaticMeshReference(const FString &PathFileName)
{
    FString SourcePath;
    int32   SourceMeshId = -1;
    if (!ParseFbxSceneSubAssetReference(PathFileName, "#Mesh_", SourcePath, SourceMeshId))
    {
        return nullptr;
    }

    UFBXSceneAsset *SceneAsset = LoadFbxScene(SourcePath);
    if (!SceneAsset)
    {
        UE_LOG("[FBXManager] Failed to load FBX scene for static mesh reference: %s",
               PathFileName.c_str());
        return nullptr;
    }

    UStaticMesh *StaticMesh = SceneAsset->FindStaticMeshBySourceMeshId(SourceMeshId);
    if (!StaticMesh)
    {
        UE_LOG("[FBXManager] Static mesh reference not found in FBX scene. Ref=%s MeshId=%d",
               PathFileName.c_str(), SourceMeshId);
    }
    return StaticMesh;
}

USkeletalMesh *FFBXManager::ResolveSkeletalMeshReference(const FString &PathFileName)
{
    FString SourcePath;
    int32   SourceSkeletonId = -1;
    if (!ParseFbxSceneSubAssetReference(PathFileName, "#Skeleton_", SourcePath, SourceSkeletonId))
    {
        return nullptr;
    }

    UFBXSceneAsset *SceneAsset = LoadFbxScene(SourcePath);
    if (!SceneAsset)
    {
        UE_LOG("[FBXManager] Failed to load FBX scene for skeletal mesh reference: %s",
               PathFileName.c_str());
        return nullptr;
    }

    USkeletalMesh *SkeletalMesh = SceneAsset->FindSkeletalMeshBySourceSkeletonId(SourceSkeletonId);
    if (!SkeletalMesh)
    {
        UE_LOG("[FBXManager] Skeletal mesh reference not found in FBX scene. Ref=%s SkeletonId=%d",
               PathFileName.c_str(), SourceSkeletonId);
    }
    return SkeletalMesh;
}

UObject *FFBXManager::ResolveFbxSceneAssetReference(const FString &PathFileName)
{
    if (IsFbxPath(PathFileName))
    {
        return LoadFbxScene(PathFileName);
    }
    if (PathFileName.find("#Mesh_") != FString::npos)
    {
        return ResolveStaticMeshReference(PathFileName);
    }
    if (PathFileName.find("#Skeleton_") != FString::npos)
    {
        return ResolveSkeletalMeshReference(PathFileName);
    }
    return nullptr;
}

UStaticMesh *FFBXManager::LoadStaticMeshFromFbxSceneReference(const FString &PathFileName)
{
    return ResolveStaticMeshReference(PathFileName);
}

USkeletalMesh *FFBXManager::LoadSkeletalMeshFromFbxSceneReference(const FString &PathFileName)
{
    return ResolveSkeletalMeshReference(PathFileName);
}

void FFBXManager::ScanFbxSourceFiles()
{
    AddFilesWithExtension(std::filesystem::path(FPaths::RootDir()) / L"Data\\", L".fbx",
                          AvailableFbxFiles);
}

const TArray<FMeshAssetListItem> &FFBXManager::GetAvailableFbxSourceFiles()
{
    return AvailableFbxFiles;
}

void FFBXManager::ReleaseAllGPU() { FbxSceneCache.clear(); }
