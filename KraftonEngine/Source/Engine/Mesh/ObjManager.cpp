/**
 * OBJ 원본 파일과 StaticMesh 바이너리 캐시 사이의 로드 흐름을 구현한다.
 *
 * 원본 OBJ를 매번 파싱하지 않도록 프로젝트 내부의 캐시 경로를 계산하고, 필요한 경우 파싱 결과를
 * 바이너리로 저장한 뒤 UStaticMesh 생성까지 연결한다. Content Browser에 표시할 OBJ 원본 목록과
 * 생성된 메시 에셋 목록을 스캔하는 책임도 함께 가진다.
 */

#include "Mesh/ObjManager.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/ObjImporter.h"
#include "Materials/Material.h"
#include "Core/Log.h"
#include "Serialization/WindowsArchive.h"
#include "Engine/Platform/Paths.h"
#include "Materials/MaterialManager.h"
#include <filesystem>
#include <algorithm>

TMap<FString, UStaticMesh *> FObjManager::StaticMeshCache;
TArray<FMeshAssetListItem>   FObjManager::AvailableMeshFiles;
TArray<FMeshAssetListItem>   FObjManager::AvailableObjFiles;

static void EnsureMeshCacheDirExists()
{
    static bool bCreated = false;
    if (!bCreated)
    {
        std::wstring CacheDir = FPaths::RootDir() + L"Asset\\MeshCache\\";
        FPaths::CreateDir(CacheDir);
        bCreated = true;
    }
}

FString FObjManager::GetBinaryFilePath(const FString &OriginalPath)
{
    std::wstring OriginalDiskPath;
    FString      ResolveError;
    const bool   bResolvedOriginal =
        FPaths::TryResolvePackagePath(OriginalPath, OriginalDiskPath, &ResolveError);
    std::filesystem::path SrcPath(bResolvedOriginal ? OriginalDiskPath
                                                    : FPaths::ToWide(OriginalPath));
    std::wstring          Ext = SrcPath.extension().wstring();

    if (Ext == L".bin")
    {
        return OriginalPath;
    }

    EnsureMeshCacheDirExists();

    std::filesystem::path RelPath = std::filesystem::path(L"Asset\\MeshCache") / SrcPath.stem();
    RelPath += L".bin";

    return FPaths::ToUtf8(RelPath.generic_wstring());
}

void FObjManager::ScanMeshAssets()
{
    AvailableMeshFiles.clear();

    const std::filesystem::path MeshCacheRoot = FPaths::RootDir() + L"Asset\\MeshCache\\";

    if (!std::filesystem::exists(MeshCacheRoot))
    {
        return;
    }

    const std::filesystem::path ProjectRoot(FPaths::RootDir());

    for (const auto &Entry : std::filesystem::recursive_directory_iterator(MeshCacheRoot))
    {
        if (!Entry.is_regular_file())
            continue;

        const std::filesystem::path &Path = Entry.path();
        if (Path.extension() != L".bin")
            continue;

        FMeshAssetListItem Item;
        Item.DisplayName = FPaths::ToUtf8(Path.stem().wstring());
        Item.FullPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());
        AvailableMeshFiles.push_back(std::move(Item));
    }
}

void FObjManager::ScanObjSourceFiles()
{
    AvailableObjFiles.clear();

    const std::filesystem::path DataRoot = FPaths::RootDir() + L"Data\\";

    if (!std::filesystem::exists(DataRoot))
    {
        return;
    }

    const std::filesystem::path ProjectRoot(FPaths::RootDir());

    for (const auto &Entry : std::filesystem::recursive_directory_iterator(DataRoot))
    {
        if (!Entry.is_regular_file())
            continue;

        const std::filesystem::path &Path = Entry.path();
        std::wstring                 Ext = Path.extension().wstring();

        std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
        if (Ext != L".obj")
            continue;

        FMeshAssetListItem Item;
        Item.DisplayName = FPaths::ToUtf8(Path.filename().wstring());
        Item.FullPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());
        AvailableObjFiles.push_back(std::move(Item));
    }
}

const TArray<FMeshAssetListItem> &FObjManager::GetAvailableMeshFiles()
{
    return AvailableMeshFiles;
}

const TArray<FMeshAssetListItem> &FObjManager::GetAvailableObjFiles() { return AvailableObjFiles; }

UStaticMesh *FObjManager::LoadObjStaticMesh(const FString        &PathFileName,
                                            const FImportOptions &Options, ID3D11Device *InDevice)
{
    FString CacheKey = GetBinaryFilePath(PathFileName);

    StaticMeshCache.erase(CacheKey);

    UStaticMesh *StaticMesh = UObjectManager::Get().CreateObject<UStaticMesh>();

    FString BinPath = CacheKey;

    FStaticMesh            *NewMeshAsset = new FStaticMesh();
    TArray<FStaticMaterial> ParsedMaterials;

    if (FObjImporter::Import(PathFileName, Options, *NewMeshAsset, ParsedMaterials))
    {
        NewMeshAsset->PathFileName = PathFileName;

        StaticMesh->SetStaticMaterials(std::move(ParsedMaterials));
        StaticMesh->SetStaticMeshAsset(NewMeshAsset);

        FWindowsBinWriter Writer(BinPath);
        if (Writer.IsValid())
        {
            StaticMesh->Serialize(Writer);
        }
    }

    StaticMesh->InitResources(InDevice);
    StaticMeshCache[CacheKey] = StaticMesh;

    ScanMeshAssets();
    FMaterialManager::Get().ScanMaterialAssets();

    return StaticMesh;
}

UStaticMesh *FObjManager::LoadObjStaticMesh(const FString &PathFileName, ID3D11Device *InDevice)
{
    FString CacheKey = GetBinaryFilePath(PathFileName);

    auto It = StaticMeshCache.find(CacheKey);
    if (It != StaticMeshCache.end())
    {
        return It->second;
    }

    UStaticMesh *StaticMesh = UObjectManager::Get().CreateObject<UStaticMesh>();

    FString BinPath = CacheKey;
    bool    bNeedRebuild = true;

    std::wstring BinDiskPath;
    std::wstring SourceDiskPath;
    FString      ResolveError;
    if (!FPaths::TryResolvePackagePath(BinPath, BinDiskPath, &ResolveError))
    {
        BinDiskPath = FPaths::ToWide(BinPath);
    }
    if (!FPaths::TryResolvePackagePath(PathFileName, SourceDiskPath, &ResolveError))
    {
        SourceDiskPath = FPaths::ToWide(PathFileName);
    }
    std::filesystem::path BinPathW(BinDiskPath);
    std::filesystem::path PathFileNameW(SourceDiskPath);
    if (std::filesystem::exists(BinPathW))
    {
        if (!std::filesystem::exists(PathFileNameW) || PathFileName == BinPath ||
            std::filesystem::last_write_time(BinPathW) >=
                std::filesystem::last_write_time(PathFileNameW))
        {
            bNeedRebuild = false;
        }
    }

    if (!bNeedRebuild)
    {

        FWindowsBinReader Reader(BinPath);
        if (Reader.IsValid())
        {
            StaticMesh->Serialize(Reader);
        }
        else
        {
            bNeedRebuild = true;
        }
    }

    if (bNeedRebuild)
    {

        FString ObjPath = PathFileName;
        if (StaticMesh->GetStaticMeshAsset() &&
            !StaticMesh->GetStaticMeshAsset()->PathFileName.empty())
            ObjPath = StaticMesh->GetStaticMeshAsset()->PathFileName;

        FStaticMesh            *NewMeshAsset = new FStaticMesh();
        TArray<FStaticMaterial> ParsedMaterials;

        if (FObjImporter::Import(ObjPath, *NewMeshAsset, ParsedMaterials))
        {

            StaticMesh->SetStaticMaterials(std::move(ParsedMaterials));
            StaticMesh->SetStaticMeshAsset(NewMeshAsset);

            FWindowsBinWriter Writer(BinPath);
            if (Writer.IsValid())
            {
                StaticMesh->Serialize(Writer);
            }
        }
    }

    StaticMesh->InitResources(InDevice);

    StaticMeshCache[CacheKey] = StaticMesh;

    ScanMeshAssets();
    FMaterialManager::Get().ScanMaterialAssets();

    return StaticMesh;
}

void FObjManager::ReleaseAllGPU()
{
    for (auto &[Key, Mesh] : StaticMeshCache)
    {
        if (Mesh)
        {
            FStaticMesh *Asset = Mesh->GetStaticMeshAsset();
            if (Asset && Asset->RenderBuffer)
            {
                Asset->RenderBuffer->Release();
                Asset->RenderBuffer.reset();
            }

            for (uint32 LOD = 1; LOD < UStaticMesh::MAX_LOD_COUNT; ++LOD)
            {
                FMeshBuffer *LODBuffer = Mesh->GetLODMeshBuffer(LOD);
                if (LODBuffer)
                {
                    LODBuffer->Release();
                }
            }
        }
    }
    StaticMeshCache.clear();
}
