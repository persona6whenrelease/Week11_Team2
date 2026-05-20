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
#include <set>
#include <vector>

namespace
{
    // .asset 단일 파일에서 로드한 UAnimSequence를 보관하는 메모리 캐시.
    // FBX scene cache와 동일한 패턴 — 프로세스 종료까지 비우지 않는다 (의도적 누수 수용).
    TMap<FString, UAnimSequence*> AnimSequenceAssetCache;
    TMap<const USkeletalMesh*, FString> SkeletalMeshAssetPathCache;

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

    std::filesystem::path ResolveDiskPath(const FString &Path)
    {
        std::wstring DiskPath;
        FString      ResolveError;
        if (FPaths::TryResolvePackagePath(Path, DiskPath, &ResolveError))
        {
            return std::filesystem::path(DiskPath).lexically_normal();
        }
        return std::filesystem::path(FPaths::ToWide(Path)).lexically_normal();
    }

    FString ToPackageLikePath(const std::filesystem::path &DiskPath)
    {
        const std::filesystem::path Root = std::filesystem::path(FPaths::RootDir()).lexically_normal();
        const std::filesystem::path Normalized = DiskPath.lexically_normal();
        const std::filesystem::path Relative = Normalized.lexically_relative(Root);

        if (!Relative.empty())
        {
            bool bEscapesRoot = false;
            for (const std::filesystem::path &Part : Relative)
            {
                if (Part == L"..")
                {
                    bEscapesRoot = true;
                    break;
                }
            }
            if (!bEscapesRoot)
            {
                return FPaths::ToUtf8(Relative.generic_wstring());
            }
        }

        return FPaths::ToUtf8(Normalized.generic_wstring());
    }

    bool HasAssetExtension(const std::filesystem::path &Path)
    {
        std::wstring Ext = Path.extension().wstring();
        std::transform(Ext.begin(), Ext.end(), Ext.begin(),
                       [](wchar_t Ch) { return static_cast<wchar_t>(std::towlower(Ch)); });
        return Ext == L".asset";
    }

    FString GetPathStemLower(const FString &Path)
    {
        std::filesystem::path FsPath(FPaths::ToWide(Path));
        std::wstring          Stem = FsPath.stem().wstring();
        std::transform(Stem.begin(), Stem.end(), Stem.begin(),
                       [](wchar_t Ch) { return static_cast<wchar_t>(std::towlower(Ch)); });
        return FPaths::ToUtf8(Stem);
    }

    FString GetSkeletonAssetMarker(const FString &SkeletonAssetPath)
    {
        const FString Marker = "#SkeletonAsset_";
        const size_t  MarkerPos = SkeletonAssetPath.rfind(Marker);
        return MarkerPos == FString::npos ? FString() : SkeletonAssetPath.substr(MarkerPos);
    }

    int32 GetSkeletonReferenceScore(const FString &MeshSkeletonPath,
                                    const FString &SequenceSkeletonPath)
    {
        if (MeshSkeletonPath.empty() || SequenceSkeletonPath.empty())
        {
            return 0;
        }
        if (MeshSkeletonPath == SequenceSkeletonPath)
        {
            return 3;
        }

        const FString MeshMarker = GetSkeletonAssetMarker(MeshSkeletonPath);
        const FString SequenceMarker = GetSkeletonAssetMarker(SequenceSkeletonPath);
        if (MeshMarker.empty() || MeshMarker != SequenceMarker)
        {
            return 0;
        }

        const FString MeshSourceStem =
            GetPathStemLower(FMeshManager::GetFbxSourcePathFromSubAssetPath(MeshSkeletonPath));
        const FString SequenceSourceStem =
            GetPathStemLower(FMeshManager::GetFbxSourcePathFromSubAssetPath(SequenceSkeletonPath));
        return (!MeshSourceStem.empty() && MeshSourceStem == SequenceSourceStem) ? 2 : 1;
    }

    void AddSearchRoot(const std::filesystem::path &Root,
                       std::vector<std::filesystem::path> &Roots,
                       std::set<std::wstring> &AddedRoots)
    {
        if (Root.empty())
        {
            return;
        }

        std::error_code Ec;
        if (!std::filesystem::exists(Root, Ec))
        {
            return;
        }

        const std::filesystem::path Normalized = Root.lexically_normal();
        std::wstring Key = Normalized.wstring();
        std::transform(Key.begin(), Key.end(), Key.begin(),
                       [](wchar_t Ch) { return static_cast<wchar_t>(std::towlower(Ch)); });
        if (AddedRoots.insert(Key).second)
        {
            Roots.push_back(Normalized);
        }
    }

    void CollectAssetFiles(const std::vector<std::filesystem::path> &Roots,
                           TArray<FString> &OutAssetPaths)
    {
        std::set<std::wstring> AddedFiles;
        for (const std::filesystem::path &Root : Roots)
        {
            std::error_code Ec;
            if (std::filesystem::is_regular_file(Root, Ec))
            {
                if (HasAssetExtension(Root))
                {
                    OutAssetPaths.push_back(ToPackageLikePath(Root));
                }
                continue;
            }

            if (!std::filesystem::is_directory(Root, Ec))
            {
                continue;
            }

            TArray<std::filesystem::path> Files;
            for (const std::filesystem::directory_entry &Entry :
                 std::filesystem::recursive_directory_iterator(
                     Root, std::filesystem::directory_options::skip_permission_denied, Ec))
            {
                if (Ec)
                {
                    break;
                }
                if (!Entry.is_regular_file(Ec) || !HasAssetExtension(Entry.path()))
                {
                    continue;
                }
                Files.push_back(Entry.path().lexically_normal());
            }

            std::sort(Files.begin(), Files.end());
            for (const std::filesystem::path &File : Files)
            {
                std::wstring Key = File.wstring();
                std::transform(Key.begin(), Key.end(), Key.begin(),
                               [](wchar_t Ch) { return static_cast<wchar_t>(std::towlower(Ch)); });
                if (AddedFiles.insert(Key).second)
                {
                    OutAssetPaths.push_back(ToPackageLikePath(File));
                }
            }
        }
    }

    FString TryBuildStandaloneSkeletalAssetFallbackPath(const FString& Path)
    {
        const FString Marker = "#Skeleton_";
        const size_t MarkerPos = Path.rfind(Marker);
        if (MarkerPos == FString::npos)
        {
            return FString();
        }

        const FString SourcePath = Path.substr(0, MarkerPos);
        std::filesystem::path CandidatePath(FPaths::ToWide(SourcePath));
        CandidatePath.replace_extension(L".asset");

        const auto IsStandaloneSkeletalAsset = [](const std::filesystem::path& AssetDiskPath) -> bool
        {
            const FString AssetPath = FPaths::ToUtf8(AssetDiskPath.generic_wstring());
            EAssetType AssetType = EAssetType::Unknown;
            return TryReadAssetType(AssetPath, AssetType) && AssetType == EAssetType::SkeletalMesh;
        };

        if (IsStandaloneSkeletalAsset(CandidatePath))
        {
            return FPaths::ToUtf8(CandidatePath.generic_wstring());
        }

        // 저장된 FBX reference 절대경로가 오래된/잘못된 경로일 수 있으므로 Asset/FBX 아래에서 같은 파일명을 다시 찾는다.
        const std::wstring TargetFileName = CandidatePath.filename().wstring();
        const std::filesystem::path SearchRoot = std::filesystem::path(FPaths::RootDir()) / L"Asset" / L"FBX";
        std::error_code ErrorCode;
        for (std::filesystem::recursive_directory_iterator It(SearchRoot, std::filesystem::directory_options::skip_permission_denied, ErrorCode), End;
             It != End;
             It.increment(ErrorCode))
        {
            if (ErrorCode)
            {
                ErrorCode.clear();
                continue;
            }

            if (!It->is_regular_file())
            {
                continue;
            }

            if (It->path().filename().wstring() != TargetFileName)
            {
                continue;
            }

            if (IsStandaloneSkeletalAsset(It->path()))
            {
                return FPaths::ToUtf8(It->path().generic_wstring());
            }
        }

        return FString();
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
    if (HasAssetExtension(PathFileName))
    {
        return LoadSkeletalMeshFromFile(PathFileName);
    }

    const FString FallbackAssetPath = TryBuildStandaloneSkeletalAssetFallbackPath(PathFileName);
    if (!FallbackAssetPath.empty())
    {
        UE_LOG("[MeshManager] Resolved skeletal mesh FBX reference to standalone asset. Ref=%s Asset=%s",
               PathFileName.c_str(), FallbackAssetPath.c_str());
        return LoadSkeletalMeshFromFile(FallbackAssetPath);
    }

    if (USkeletalMesh* Mesh = FFBXManager::LoadSkeletalMesh(PathFileName))
    {
        return Mesh;
    }

    return nullptr;
}

USkeletalMesh *FMeshManager::LoadSkeletalMeshFromFile(const FString &PathFileName)
{
    if (PathFileName.empty() || PathFileName == "None")
    {
        return nullptr;
    }

    FWindowsBinReader Reader(PathFileName);
    if (!Reader.IsValid())
    {
        UE_LOG("[MeshManager] LoadSkeletalMeshFromFile: failed to open reader. Path=%s",
               PathFileName.c_str());
        return nullptr;
    }

    USkeletalMesh *Mesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
    if (!Mesh)
    {
        UE_LOG("[MeshManager] LoadSkeletalMeshFromFile: failed to create USkeletalMesh");
        return nullptr;
    }

    Mesh->Serialize(Reader);
    if (!Mesh->GetSkeletalMeshAsset())
    {
        UE_LOG("[MeshManager] LoadSkeletalMeshFromFile: deserialized mesh is invalid. Path=%s",
               PathFileName.c_str());
        UObjectManager::Get().DestroyObject(Mesh);
        return nullptr;
    }

    // Standalone .asset 로드에서는 내부 원본 FBX 참조 대신 실제 .asset 경로를 자기 경로로 사용한다.
    // 그래야 이후 컴포넌트/에디터가 이 메시를 다시 경로 기반으로 다룰 때 FBX reference loader를 타지 않는다.
    Mesh->GetSkeletalMeshAsset()->PathFileName = PathFileName;
    SkeletalMeshAssetPathCache[Mesh] = PathFileName;

    return Mesh;
}

FString FMeshManager::GetLoadedSkeletalMeshAssetPath(const USkeletalMesh *Mesh)
{
    auto It = SkeletalMeshAssetPathCache.find(Mesh);
    return It != SkeletalMeshAssetPathCache.end() ? It->second : FString();
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
        EAssetType AssetType = EAssetType::Unknown;
        if (TryReadAssetType(PathFileName, AssetType) && AssetType != EAssetType::AnimSequence)
        {
            return nullptr;
        }
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

bool FMeshManager::IsAnimSequenceCompatibleWithMesh(const UAnimSequence *Sequence,
                                                    const USkeletalMesh *Mesh)
{
    if (!Sequence || !Mesh)
    {
        return false;
    }

    const UAnimDataModel *DataModel = Sequence->GetDataModel();
    if (!DataModel || DataModel->GetBoneAnimationTracks().empty())
    {
        return false;
    }

    const USkeleton *Skeleton = Mesh->GetSkeleton();
    if (!Skeleton || Skeleton->GetBones().empty())
    {
        return false;
    }

    const FName RootTrackName = DataModel->GetBoneAnimationTracks()[0].Name;
    const FString RootTrackStr = RootTrackName.ToString();
    for (const FBoneInfo &Bone : Skeleton->GetBones())
    {
        if (Bone.Name == RootTrackStr)
        {
            return true;
        }
    }

    return false;
}

bool FMeshManager::SaveSkeletalMeshToFile(const USkeletalMesh *Mesh, const FString &PathFileName)
{
    if (!Mesh)
    {
        UE_LOG("[MeshManager] SaveSkeletalMeshToFile: null mesh");
        return false;
    }
    if (PathFileName.empty())
    {
        UE_LOG("[MeshManager] SaveSkeletalMeshToFile: empty path");
        return false;
    }

    FWindowsBinWriter Writer(PathFileName);
    if (!Writer.IsValid())
    {
        UE_LOG("[MeshManager] SaveSkeletalMeshToFile: failed to open writer. Path=%s",
               PathFileName.c_str());
        return false;
    }

    const_cast<USkeletalMesh *>(Mesh)->Serialize(Writer);
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

FString FMeshManager::GetFbxSourcePathFromSubAssetPath(const FString &AssetPath)
{
    const size_t MarkerPos = AssetPath.find('#');
    return MarkerPos == FString::npos ? AssetPath : AssetPath.substr(0, MarkerPos);
}

UFBXSceneAsset *FMeshManager::LoadFbxSceneForSkeletonAssetPath(const FString &SkeletonAssetPath)
{
    if (SkeletonAssetPath.empty() || SkeletonAssetPath.find('#') == FString::npos)
    {
        return nullptr;
    }

    const FString FbxSourcePath = GetFbxSourcePathFromSubAssetPath(SkeletonAssetPath);
    return FbxSourcePath.empty() ? nullptr : LoadFbxScene(FbxSourcePath);
}

UFBXSceneAsset *FMeshManager::LoadFbxSceneForSkeletalMesh(const USkeletalMesh *Mesh)
{
    const FSkeletalMesh *MeshAsset = GetMeshAsset(Mesh);
    return MeshAsset ? LoadFbxSceneForSkeletonAssetPath(MeshAsset->SkeletonAssetPath) : nullptr;
}

USkeletalMesh *FMeshManager::FindPreviewMeshForAnimSequence(const UAnimSequence *Sequence)
{
    return FindPreviewMeshForAnimSequence(Sequence, FString());
}

USkeletalMesh *FMeshManager::FindPreviewMeshForAnimSequence(const UAnimSequence *Sequence,
                                                            const FString       &SequenceAssetPath)
{
    if (!Sequence)
    {
        return nullptr;
    }

    UFBXSceneAsset *SceneAsset = Sequence->GetTypedOuter<UFBXSceneAsset>();
    if (!SceneAsset)
    {
        SceneAsset = LoadFbxSceneForSkeletonAssetPath(Sequence->GetSkeletonAssetPath());
    }

    if (USkeletalMesh *PreviewMesh = FindSkeletalMeshForAnimSequence(SceneAsset, Sequence))
    {
        return PreviewMesh;
    }

    std::vector<std::filesystem::path> Roots;
    std::set<std::wstring>             AddedRoots;
    if (!SequenceAssetPath.empty())
    {
        const std::filesystem::path SequenceDiskPath = ResolveDiskPath(SequenceAssetPath);
        const std::filesystem::path SequenceDir = SequenceDiskPath.parent_path();
        AddSearchRoot(SequenceDir.parent_path(), Roots, AddedRoots);
        AddSearchRoot(SequenceDir, Roots, AddedRoots);
    }
    AddSearchRoot(FPaths::AssetDir(), Roots, AddedRoots);

    TArray<FString> CandidatePaths;
    CollectAssetFiles(Roots, CandidatePaths);

    USkeletalMesh *BestMesh = nullptr;
    int32          BestScore = -1;
    for (const FString &CandidatePath : CandidatePaths)
    {
        EAssetType AssetType = EAssetType::Unknown;
        if (!TryReadAssetType(CandidatePath, AssetType) || AssetType != EAssetType::SkeletalMesh)
        {
            continue;
        }

        USkeletalMesh *Mesh = LoadSkeletalMeshFromFile(CandidatePath);
        if (!Mesh || !IsAnimSequenceCompatibleWithMesh(Sequence, Mesh))
        {
            continue;
        }

        const FSkeletalMesh *MeshAsset = GetMeshAsset(Mesh);
        const int32 Score = MeshAsset
            ? GetSkeletonReferenceScore(MeshAsset->SkeletonAssetPath, Sequence->GetSkeletonAssetPath())
            : 0;
        if (Score > BestScore)
        {
            BestMesh = Mesh;
            BestScore = Score;
            if (BestScore >= 3)
            {
                break;
            }
        }
    }

    return BestMesh;
}

void FMeshManager::FindCompatibleAnimSequenceAssetsForSkeletalMesh(
    const USkeletalMesh *Mesh,
    const FString       &MeshAssetPath,
    TArray<FString>     &OutPaths,
    TArray<UAnimSequence*> &OutSequences)
{
    OutPaths.clear();
    OutSequences.clear();
    if (!Mesh)
    {
        return;
    }

    std::vector<std::filesystem::path> Roots;
    std::set<std::wstring>             AddedRoots;
    if (!MeshAssetPath.empty())
    {
        const std::filesystem::path MeshDiskPath = ResolveDiskPath(MeshAssetPath);
        const std::filesystem::path MeshDir = MeshDiskPath.parent_path();
        AddSearchRoot(MeshDir / L"animation", Roots, AddedRoots);
        AddSearchRoot(MeshDir, Roots, AddedRoots);
    }
    AddSearchRoot(FPaths::AssetDir(), Roots, AddedRoots);

    TArray<FString> CandidatePaths;
    CollectAssetFiles(Roots, CandidatePaths);

    const FSkeletalMesh *MeshAsset = GetMeshAsset(Mesh);
    struct FMatch
    {
        FString        Path;
        UAnimSequence *Sequence = nullptr;
        int32          Score = 0;
    };
    TArray<FMatch> Matches;

    for (const FString &CandidatePath : CandidatePaths)
    {
        EAssetType AssetType = EAssetType::Unknown;
        if (!TryReadAssetType(CandidatePath, AssetType) || AssetType != EAssetType::AnimSequence)
        {
            continue;
        }

        UAnimSequence *Sequence = LoadAnimSequenceFromFile(CandidatePath);
        if (!Sequence || !IsAnimSequenceCompatibleWithMesh(Sequence, Mesh))
        {
            continue;
        }

        const int32 Score = MeshAsset
            ? GetSkeletonReferenceScore(MeshAsset->SkeletonAssetPath, Sequence->GetSkeletonAssetPath())
            : 0;
        Matches.push_back({CandidatePath, Sequence, Score});
    }

    std::sort(Matches.begin(), Matches.end(),
              [](const FMatch &A, const FMatch &B)
              {
                  if (A.Score != B.Score)
                  {
                      return A.Score > B.Score;
                  }
                  return A.Path < B.Path;
              });

    for (const FMatch &Match : Matches)
    {
        OutPaths.push_back(Match.Path);
        OutSequences.push_back(Match.Sequence);
    }
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
