/**
 * 硫붿떆 濡쒕뵫??吏꾩엯?먯쓣 援ы쁽?쒕떎.
 *
 * OBJ ?먮낯, FBX ??李몄“, FBX?먯꽌 遺꾨━??StaticMesh/SkeletalMesh 李몄“瑜??몄텧遺媛 吏곸젒 援щ텇?섏?
 * ?딆븘???섎룄濡?寃쎈줈 洹쒖튃???먮퀎?섍퀬 ?뚮쭪? ?섏쐞 留ㅻ땲?濡??꾩엫?쒕떎. ?먰븳 FBX ??罹먯떆 ?뚯씪?? * ????꾩튂瑜??꾨줈?앺듃 湲곗? 寃쎈줈濡??뺢퇋?뷀븯???먮낯 ?뚯씪 寃쎈줈? 罹먯떆 諛붿씠?덈━ 寃쎈줈 ?ъ씠?? * 蹂??洹쒖튃????怨녹뿉 紐⑥???
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
