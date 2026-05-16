/**
 * OBJ 메시의 캐시 생성, 캐시 로드, 리소스 초기화를 구현한다.
 *
 * 원본 OBJ/MTL을 FObjImporter로 변환한 뒤 바이너리 파일로 저장하고, 다음 로드에서는 캐시를 우선 사용한다.
 * 캐시가 없거나 오래된 경우 다시 임포트하며, 최종적으로 UStaticMesh에 데이터를 연결하고 GPU 버퍼를 만든다.
 */

#include "Asset/Import/OBJ/ObjManager.h"
#include "Asset/Mesh/StaticMesh/StaticMesh.h"
#include "Asset/Import/OBJ/ObjImporter.h"
#include "Asset/Material/Material.h"
#include "Core/Log.h"
#include "Serialization/WindowsArchive.h"
#include "Engine/Platform/Paths.h"
#include "Asset/Material/MaterialManager.h"
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

        /**
         * 원본 포맷에서 필요한 속성 값을 읽어 엔진 타입으로 변환한다.
         */
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
