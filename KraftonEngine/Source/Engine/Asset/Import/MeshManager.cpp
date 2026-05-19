/**
 * 상위 메시 매니저의 경로 판별, 스캔, 로드 위임 로직을 구현한다.
 *
 * OBJ는 ObjManager가 담당하고 FBX는 FFBXManager가 담당한다. 이 파일은 두 시스템의 캐시 디렉터리를
 * 준비하고, Asset/Source와 Asset/Content를 함께 스캔해 에디터에서 보여줄 메시 목록을 구성한다.
 */

#include "Asset/Import/MeshManager.h"

#include "Engine/Platform/Paths.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/AssetFileHeader.h"
#include "Asset/Import/FBX/Core/FBXManager.h"
#include "Asset/Import/OBJ/ObjManager.h"
#include "Asset/Import/FBX/Types/FBXSceneAsset.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "Core/Log.h"
#include "Object/Object.h"
#include "Serialization/WindowsArchive.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cwctype>
#include <filesystem>

namespace
{
    // .asset 단일 파일에서 로드한 UAnimSequence를 보관하는 메모리 캐시.
    // FBX scene cache와 동일한 패턴 — 프로세스 종료까지 비우지 않는다 (의도적 누수 수용).
    TMap<FString, UAnimSequence*> AnimSequenceAssetCache;

    FString NormalizeAssetCacheKey(const FString &Path)
    {
        std::filesystem::path Normalized(FPaths::ToWide(Path));
        std::wstring          Generic = Normalized.lexically_normal().generic_wstring();
        std::transform(Generic.begin(), Generic.end(), Generic.begin(),
                       [](wchar_t Ch) { return static_cast<wchar_t>(std::towlower(Ch)); });
        return FPaths::ToUtf8(Generic);
    }

    bool HasAssetExtension(const FString &Path)
    {
        std::filesystem::path FsPath(FPaths::ToWide(Path));
        std::wstring          Ext = FsPath.extension().wstring();
        std::transform(Ext.begin(), Ext.end(), Ext.begin(),
                       [](wchar_t Ch) { return static_cast<wchar_t>(std::towlower(Ch)); });
        return Ext == L".asset";
    }

    void EnsureFbxSceneCacheDirExists()
    {
        static bool bCreated = false;
        if (!bCreated)
        {
            FPaths::CreateDir(FPaths::RootDir() + L"Asset\\FBXSceneCache\\");
            bCreated = true;
        }
    }

    const FSkeletalMesh *GetMeshAsset(const USkeletalMesh *SkeletalMesh)
    {
        return SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
    }

    bool TryBuildAnimSequenceReferencePath(const UFBXSceneAsset *SceneAsset,
                                           const USkeletalMesh  *SkeletalMesh,
                                           int32                 SequenceIndex,
                                           FString              *OutPath)
    {
        if (!OutPath)
        {
            return true;
        }

        *OutPath = FString();

        const FSkeletalMesh *MeshAsset = GetMeshAsset(SkeletalMesh);
        if (!SceneAsset || !MeshAsset)
        {
            return false;
        }

        const FString &SourcePath = SceneAsset->GetSourcePath();
        const FString &SkeletonAssetPath = MeshAsset->SkeletonAssetPath;
        const FString Marker = "#SkeletonAsset_";
        const size_t MarkerPos = SkeletonAssetPath.rfind(Marker);
        if (SourcePath.empty() || MarkerPos == FString::npos)
        {
            return false;
        }

        const FString SkeletonIdText = SkeletonAssetPath.substr(MarkerPos + Marker.size());
        if (SkeletonIdText.empty())
        {
            return false;
        }

        char      *End = nullptr;
        const long ParsedSkeletonId = std::strtol(SkeletonIdText.c_str(), &End, 10);
        if (!End || *End != '\0' || ParsedSkeletonId < 0 || ParsedSkeletonId > INT32_MAX)
        {
            return false;
        }

        *OutPath = SourcePath + "#Anim_" + std::to_string(ParsedSkeletonId) + "_" +
                   std::to_string(SequenceIndex);
        return true;
    }
}

bool FMeshManager::IsFbxStaticMeshReference(const FString &PathFileName)
{
    return PathFileName.find("#Mesh_") != FString::npos;
}

bool FMeshManager::IsFbxSkeletalMeshReference(const FString &PathFileName)
{
    return PathFileName.find("#Skeleton_") != FString::npos;
}

FString FMeshManager::GetObjBinaryFilePath(const FString &OriginalPath)
{
    return FObjManager::GetBinaryFilePath(OriginalPath);
}

FString FMeshManager::GetFbxSceneCacheFilePath(const FString &SourcePath)
{
    EnsureFbxSceneCacheDirExists();

    std::wstring SourceDiskPath;
    FString      ResolveError;
    const bool   bResolvedSource =
        FPaths::TryResolvePackagePath(SourcePath, SourceDiskPath, &ResolveError);
    const std::filesystem::path SrcPath(bResolvedSource ? SourceDiskPath
                                                        : FPaths::ToWide(SourcePath));

    std::filesystem::path RelPath = std::filesystem::path(L"Asset\\FBXSceneCache") / SrcPath.stem();
    RelPath += L".fbxscene.bin";
    return FPaths::ToUtf8(RelPath.generic_wstring());
}

UStaticMesh *FMeshManager::LoadStaticMesh(const FString &PathFileName, ID3D11Device *InDevice)
{
    if (PathFileName.empty() || PathFileName == "None")
    {
        return nullptr;
    }

    if (IsFbxStaticMeshReference(PathFileName))
    {
        return FFBXManager::LoadStaticMeshFromFbxSceneReference(PathFileName);
    }

    return FObjManager::LoadObjStaticMesh(PathFileName, InDevice);
}

UStaticMesh *FMeshManager::LoadObjStaticMesh(const FString &PathFileName, ID3D11Device *InDevice)
{
    return FObjManager::LoadObjStaticMesh(PathFileName, InDevice);
}

UStaticMesh *FMeshManager::LoadObjStaticMesh(const FString        &PathFileName,
                                             const FImportOptions &Options, ID3D11Device *InDevice)
{
    return FObjManager::LoadObjStaticMesh(PathFileName, Options, InDevice);
}

USkeletalMesh *FMeshManager::LoadSkeletalMesh(const FString &PathFileName)
{
    return FFBXManager::LoadSkeletalMesh(PathFileName);
}

UAnimSequence *FMeshManager::ResolveAnimSequenceReference(const FString &PathFileName)
{
    if (PathFileName.empty() || PathFileName == "None")
    {
        return nullptr;
    }

    // FBX 가상 참조("Foo.fbx#Anim_0_0")는 기존 경로로, ".asset" 단일 파일은 새 로더로 보낸다.
    if (PathFileName.find("#Anim_") != FString::npos)
    {
        return FFBXManager::ResolveAnimSequenceReference(PathFileName);
    }
    if (HasAssetExtension(PathFileName))
    {
        return LoadAnimSequenceFromFile(PathFileName);
    }
    return FFBXManager::ResolveAnimSequenceReference(PathFileName);
}

bool FMeshManager::SaveAnimSequenceToFile(const UAnimSequence *Sequence, const FString &PathFileName)
{
    if (!Sequence)
    {
        UE_LOG("[MeshManager] SaveAnimSequenceToFile: null sequence");
        return false;
    }
    if (PathFileName.empty())
    {
        UE_LOG("[MeshManager] SaveAnimSequenceToFile: empty path");
        return false;
    }

    if (Sequence->GetSkeletonAssetPath().empty())
    {
        UE_LOG("[MeshManager] SaveAnimSequenceToFile: sequence has empty SkeletonAssetPath; "
               "saving anyway but reload may fail to resolve a PreviewMesh. Path=%s",
               PathFileName.c_str());
    }

    FWindowsBinWriter Writer(PathFileName);
    if (!Writer.IsValid())
    {
        UE_LOG("[MeshManager] SaveAnimSequenceToFile: failed to open writer. Path=%s",
               PathFileName.c_str());
        return false;
    }

    const_cast<UAnimSequence *>(Sequence)->Serialize(Writer);
    return true;
}

bool TryReadAssetType(const FString &PathFileName, EAssetType &OutType)
{
    if (PathFileName.empty())
    {
        return false;
    }

    FWindowsBinReader Reader(PathFileName);
    if (!Reader.IsValid())
    {
        return false;
    }

    FAssetFileHeader Header;
    Reader << Header;
    if (Header.Magic != FAssetFileHeader::ExpectedMagic)
    {
        return false;
    }

    OutType = Header.AssetType;
    return true;
}

UAnimSequence *FMeshManager::LoadAnimSequenceFromFile(const FString &PathFileName)
{
    if (PathFileName.empty() || PathFileName == "None")
    {
        return nullptr;
    }

    const FString CacheKey = NormalizeAssetCacheKey(PathFileName);
    auto          It = AnimSequenceAssetCache.find(CacheKey);
    if (It != AnimSequenceAssetCache.end())
    {
        return It->second;
    }

    FWindowsBinReader Reader(PathFileName);
    if (!Reader.IsValid())
    {
        UE_LOG("[MeshManager] LoadAnimSequenceFromFile: failed to open reader. Path=%s",
               PathFileName.c_str());
        return nullptr;
    }

    UAnimSequence *Sequence = UObjectManager::Get().CreateObject<UAnimSequence>();
    if (!Sequence)
    {
        UE_LOG("[MeshManager] LoadAnimSequenceFromFile: failed to create UAnimSequence");
        return nullptr;
    }

    Sequence->Serialize(Reader);
    if (!Sequence->IsValidSequence())
    {
        UE_LOG("[MeshManager] LoadAnimSequenceFromFile: deserialized sequence is invalid. Path=%s",
               PathFileName.c_str());
        UObjectManager::Get().DestroyObject(Sequence);
        return nullptr;
    }

    AnimSequenceAssetCache[CacheKey] = Sequence;
    return Sequence;
}

UFBXSceneAsset *FMeshManager::LoadFbxScene(const FString &PathFileName)
{
    return FFBXManager::LoadFbxScene(PathFileName);
}

int32 FMeshManager::GetAnimSequenceCountForSkeletalMesh(const UFBXSceneAsset *SceneAsset,
                                                        const USkeletalMesh  *SkeletalMesh)
{
    const FSkeletalMesh *MeshAsset = GetMeshAsset(SkeletalMesh);
    if (!SceneAsset || !MeshAsset)
    {
        return 0;
    }

    int32 Count = 0;
    for (const UAnimSequence *Sequence : SceneAsset->GetAnimSequences())
    {
        if (Sequence && Sequence->GetSkeletonAssetPath() == MeshAsset->SkeletonAssetPath)
        {
            ++Count;
        }
    }

    return Count;
}

UAnimSequence *FMeshManager::FindAnimSequenceForSkeletalMesh(UFBXSceneAsset      *SceneAsset,
                                                             const USkeletalMesh *SkeletalMesh,
                                                             int32                SequenceIndex,
                                                             FString             *OutPath)
{
    const FSkeletalMesh *MeshAsset = GetMeshAsset(SkeletalMesh);
    if (!SceneAsset || !MeshAsset || SequenceIndex < 0)
    {
        return nullptr;
    }

    int32 MatchIndex = 0;
    for (UAnimSequence *Sequence : SceneAsset->GetAnimSequences())
    {
        if (!Sequence || Sequence->GetSkeletonAssetPath() != MeshAsset->SkeletonAssetPath)
        {
            continue;
        }

        if (MatchIndex == SequenceIndex)
        {
            TryBuildAnimSequenceReferencePath(SceneAsset, SkeletalMesh, SequenceIndex, OutPath);
            return Sequence;
        }

        ++MatchIndex;
    }

    return nullptr;
}

USkeletalMesh *FMeshManager::FindSkeletalMeshForAnimSequence(UFBXSceneAsset      *SceneAsset,
                                                             const UAnimSequence *Sequence)
{
    if (!SceneAsset || !Sequence)
    {
        return nullptr;
    }

    for (USkeletalMesh *SkeletalMesh : SceneAsset->GetSkeletalMeshes())
    {
        const FSkeletalMesh *MeshAsset = GetMeshAsset(SkeletalMesh);
        if (MeshAsset && MeshAsset->SkeletonAssetPath == Sequence->GetSkeletonAssetPath())
        {
            return SkeletalMesh;
        }
    }

    return nullptr;
}

UObject *FMeshManager::ResolveFbxSceneAssetReference(const FString &PathFileName)
{
    return FFBXManager::ResolveFbxSceneAssetReference(PathFileName);
}

void FMeshManager::ScanMeshAssets() { FObjManager::ScanMeshAssets(); }

void FMeshManager::ScanObjSourceFiles() { FObjManager::ScanObjSourceFiles(); }

void FMeshManager::ScanFbxSourceFiles() { FFBXManager::ScanFbxSourceFiles(); }

void FMeshManager::ScanAllAssets()
{
    ScanMeshAssets();
    ScanObjSourceFiles();
    ScanFbxSourceFiles();
}

const TArray<FMeshAssetListItem> &FMeshManager::GetAvailableStaticMeshFiles()
{
    return FObjManager::GetAvailableMeshFiles();
}

const TArray<FMeshAssetListItem> &FMeshManager::GetAvailableObjSourceFiles()
{
    return FObjManager::GetAvailableObjFiles();
}

const TArray<FMeshAssetListItem> &FMeshManager::GetAvailableFbxSourceFiles()
{
    return FFBXManager::GetAvailableFbxSourceFiles();
}

void FMeshManager::ReleaseAllGPU()
{
    FFBXManager::ReleaseAllGPU();
    FObjManager::ReleaseAllGPU();
}
