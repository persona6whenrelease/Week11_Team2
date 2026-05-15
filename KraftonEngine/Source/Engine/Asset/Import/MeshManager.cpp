/**
 * 메시 로딩의 진입점을 구현한다.
 *
 * OBJ 원본, FBX 씬 참조, FBX에서 분리된 StaticMesh/SkeletalMesh 참조를 호출부가 직접 구분하지
 * 않아도 되도록 경로 규칙을 판별하고 알맞은 하위 매니저로 위임한다. 또한 FBX 씬 캐시 파일의
 * 저장 위치를 프로젝트 기준 경로로 정규화하여 원본 파일 경로와 캐시 바이너리 경로 사이의
 * 변환 규칙을 한 곳에 모은다.
 */

#include "Asset/Import/MeshManager.h"

#include "Engine/Platform/Paths.h"
#include "Asset/Import/FBX/Core/FBXManager.h"
#include "Asset/Import/OBJ/ObjManager.h"

#include <filesystem>

namespace
{
    void EnsureFbxSceneCacheDirExists()
    {
        static bool bCreated = false;
        if (!bCreated)
        {
            FPaths::CreateDir(FPaths::RootDir() + L"Asset\\FBXSceneCache\\");
            bCreated = true;
        }
    }
} // namespace

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

UFBXSceneAsset *FMeshManager::LoadFbxScene(const FString &PathFileName)
{
    return FFBXManager::LoadFbxScene(PathFileName);
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
