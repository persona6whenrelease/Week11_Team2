/**
 * FBX 임포트 전체 절차를 조립하고 실행한다.
 *
 * 씬을 로드한 뒤 전처리로 단위와 축을 맞추고, 메타 파서가 만든 분류 결과를 기반으로 StaticMesh,
 * SkeletalMeshPart, Skeleton, Animation을 순서대로 만든다. 여러 FBX 노드가 하나의 스켈레탈 메시로
 * 합쳐질 수 있으므로, 파싱 단계와 조립 단계를 분리해 처리한다.
 */

#include "Asset/Import/FBX/Core/FBXImporter.h"

#include "Core/Log.h"
#include "Asset/Import/FBX/Parser/FbxAnimationParser.h"
#include "Asset/Import/FBX/Material/FbxMaterialImportUtils.h"
#include "Asset/Import/FBX/Parser/FbxMetaParser.h"
#include "Asset/Import/FBX/Builder/FbxSkeletalMeshAssembler.h"
#include "Asset/Import/FBX/Parser/FbxSkeletalMeshPartParser.h"
#include "Asset/Import/FBX/Parser/FbxStaticMeshParser.h"
#include "Engine/Platform/Paths.h"

#include <fbxsdk.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>

namespace
{
    template <typename T> bool IsValidIndex(const TArray<T> &Items, int32 Index)
    {
        return Index >= 0 && static_cast<size_t>(Index) < Items.size();
    }

    /**
     * 이전 임포트 결과와 SDK 객체 상태를 정리해 다음 로드가 독립적으로 실행되게 한다.
     */
    void ClearAsset(FFBXAsset &Asset)
    {
        Asset.PathFileName.clear();
        Asset.StaticMeshes.clear();
        Asset.SkeletalMeshes.clear();
        Asset.Skeletons.clear();
        Asset.AnimSequences.clear();
        Asset.StaticMeshMaterials.clear();
        Asset.SkeletalMeshMaterials.clear();
        Asset.SkeletalMaterials.clear();
        Asset.SceneComponents.clear();
        Asset.MeshIdToStaticMeshAssetIndex.clear();
        Asset.SkeletonIdToSkeletalMeshAssetIndex.clear();
        Asset.LightAssets.clear();
        Asset.CameraAssets.clear();
    }

    /**
     * 임포트 중간 데이터에서 다음 단계가 사용할 구조를 구성한다.
     */
    void BuildSceneComponents(const FFbxImportMeta &ImportMeta, FFBXAsset &Asset)
    {
        TSet<int32> MeshIdsConsumedBySkeletal;
        for (const FFbxSkeletonMeta &SkeletonMeta : ImportMeta.Skeletons)
        {
            for (int32 MeshId : SkeletonMeta.SkinnedMeshIds)
            {
                MeshIdsConsumedBySkeletal.insert(MeshId);
            }
            for (int32 MeshId : SkeletonMeta.RigidAttachedMeshIds)
            {
                MeshIdsConsumedBySkeletal.insert(MeshId);
            }
        }

        for (const FFbxSkeletonMeta &SkeletonMeta : ImportMeta.Skeletons)
        {
            auto AssetIndexIt =
                Asset.SkeletonIdToSkeletalMeshAssetIndex.find(SkeletonMeta.SkeletonId);
            if (AssetIndexIt == Asset.SkeletonIdToSkeletalMeshAssetIndex.end())
            {
                continue;
            }

            FFBXSceneComponentDesc Desc;
            Desc.Type = EFBXSceneComponentType::SkeletalMesh;
            Desc.Name = SkeletonMeta.Name.empty()
                            ? "Skeleton_" + std::to_string(SkeletonMeta.SkeletonId)
                            : SkeletonMeta.Name;
            Desc.SourceNodeId = SkeletonMeta.RootNodeId;
            Desc.SourceSkeletonId = SkeletonMeta.SkeletonId;
            Desc.SkeletalMeshAssetIndex = AssetIndexIt->second;
            Desc.RelativeTransform = IsValidIndex(ImportMeta.Nodes, SkeletonMeta.RootNodeId)
                                         ? ImportMeta.Nodes[SkeletonMeta.RootNodeId].LocalTransform
                                         : FMatrix::Identity;
            Asset.SceneComponents.push_back(std::move(Desc));
        }

        for (const FFbxMeshMeta &MeshMeta : ImportMeta.Meshes)
        {
            if (MeshIdsConsumedBySkeletal.find(MeshMeta.MeshId) != MeshIdsConsumedBySkeletal.end())
            {
                continue;
            }

            auto AssetIndexIt = Asset.MeshIdToStaticMeshAssetIndex.find(MeshMeta.MeshId);
            if (AssetIndexIt == Asset.MeshIdToStaticMeshAssetIndex.end())
            {
                continue;
            }

            FFBXSceneComponentDesc Desc;
            Desc.Type = EFBXSceneComponentType::StaticMesh;
            Desc.Name =
                MeshMeta.Name.empty() ? "Mesh_" + std::to_string(MeshMeta.MeshId) : MeshMeta.Name;
            Desc.SourceNodeId = MeshMeta.NodeId;
            Desc.SourceMeshId = MeshMeta.MeshId;
            Desc.StaticMeshAssetIndex = AssetIndexIt->second;
            Desc.RelativeTransform = IsValidIndex(ImportMeta.Nodes, MeshMeta.NodeId)
                                         ? ImportMeta.Nodes[MeshMeta.NodeId].LocalTransform
                                         : FMatrix::Identity;
            Asset.SceneComponents.push_back(std::move(Desc));
        }
    }

    const char *ToStatusCodeString(FbxStatus::EStatusCode Code)
    {
        switch (Code)
        {
        case FbxStatus::eSuccess:
            return "Success";
        case FbxStatus::eFailure:
            return "Failure";
        case FbxStatus::eInsufficientMemory:
            return "InsufficientMemory";
        case FbxStatus::eInvalidParameter:
            return "InvalidParameter";
        case FbxStatus::eIndexOutOfRange:
            return "IndexOutOfRange";
        case FbxStatus::ePasswordError:
            return "PasswordError";
        case FbxStatus::eInvalidFileVersion:
            return "InvalidFileVersion";
        case FbxStatus::eInvalidFile:
            return "InvalidFile";
        case FbxStatus::eSceneCheckFail:
            return "SceneCheckFail";
        default:
            return "Unknown";
        }
    }

    std::string NormalizeUtf8Path(const std::wstring &Path)
    {
        return FPaths::ToUtf8(std::filesystem::path(Path).lexically_normal().wstring());
    }

    std::string ResolveImportDiskPath(const FString &InFilePath)
    {
        std::wstring DiskPath;
        FString      ResolveError;
        if (FPaths::TryResolvePackagePath(InFilePath, DiskPath, &ResolveError))
        {
            return NormalizeUtf8Path(DiskPath);
        }

        return NormalizeUtf8Path(FPaths::ToWide(InFilePath));
    }

    bool HasFbxExtension(const std::string &Path)
    {
        std::wstring Extension = std::filesystem::path(FPaths::ToWide(Path)).extension().wstring();
        std::transform(Extension.begin(), Extension.end(), Extension.begin(),
                       [](wchar_t Ch) { return static_cast<wchar_t>(std::towlower(Ch)); });
        return Extension == L".fbx";
    }

    bool FileExistsOnDisk(const std::string &Path)
    {
        std::error_code           ErrorCode;
        const std::filesystem::path DiskPath(FPaths::ToWide(Path));
        return std::filesystem::exists(DiskPath, ErrorCode) && !ErrorCode;
    }

    int FindPreferredFbxReaderFormat(FbxManager *Manager, const char *PathUtf8)
    {
        if (!Manager)
        {
            return -1;
        }

        FbxIOPluginRegistry *Registry = Manager->GetIOPluginRegistry();
        if (!Registry)
        {
            return -1;
        }

        int ReaderFormat = -1;
        if (PathUtf8 && Registry->DetectReaderFileFormat(PathUtf8, ReaderFormat))
        {
            return ReaderFormat;
        }

        const int BinaryReader = Registry->FindReaderIDByDescription("FBX binary (*.fbx)");
        if (BinaryReader >= 0)
        {
            return BinaryReader;
        }

        const int NativeReader = Registry->GetNativeReaderFormat();
        if (NativeReader >= 0 && Registry->ReaderIsFBX(NativeReader))
        {
            return NativeReader;
        }

        const int ReaderCount = Registry->GetReaderFormatCount();
        for (int ReaderIndex = 0; ReaderIndex < ReaderCount; ++ReaderIndex)
        {
            if (Registry->ReaderIsFBX(ReaderIndex))
            {
                return ReaderIndex;
            }
        }

        return -1;
    }

    void LogReaderDiagnostics(FbxManager *Manager, const char *PathUtf8, int ReaderFormat)
    {
        if (!Manager)
        {
            return;
        }

        FbxIOPluginRegistry *Registry = Manager->GetIOPluginRegistry();
        if (!Registry)
        {
            UE_LOG("[FBXImporter] FBX IO plugin registry is null. Path=%s",
                   PathUtf8 ? PathUtf8 : "<null>");
            return;
        }

        const int ReaderCount = Registry->GetReaderFormatCount();
        if (ReaderCount <= 0)
        {
            UE_LOG("[FBXImporter] No FBX readers are registered. Path=%s", PathUtf8 ? PathUtf8 : "<null>");
            return;
        }

        if (ReaderFormat >= 0 && ReaderFormat < ReaderCount)
        {
            UE_LOG("[FBXImporter] Reader diagnostics. Path=%s ReaderCount=%d SelectedFormat=%d "
                   "Description=%s Extension=%s IsFBX=%d",
                   PathUtf8 ? PathUtf8 : "<null>", ReaderCount, ReaderFormat,
                   Registry->GetReaderFormatDescription(ReaderFormat),
                   Registry->GetReaderFormatExtension(ReaderFormat),
                   Registry->ReaderIsFBX(ReaderFormat) ? 1 : 0);
            return;
        }

        UE_LOG("[FBXImporter] Reader diagnostics. Path=%s ReaderCount=%d SelectedFormat=%d",
               PathUtf8 ? PathUtf8 : "<null>", ReaderCount, ReaderFormat);
    }

    bool TryInitializeImporter(FbxImporter *Importer, FbxManager *Manager, const char *PathUtf8,
                               int ReaderFormat, const char *AttemptLabel)
    {
        if (!Importer || !Manager)
        {
            return false;
        }

        const bool bInitialized = Importer->Initialize(PathUtf8, ReaderFormat, Manager->GetIOSettings());
        if (!bInitialized)
        {
            const FbxStatus &Status = Importer->GetStatus();
            UE_LOG("[FBXImporter] Initialize attempt failed. Path=%s Attempt=%s Format=%d Status=%s "
                   "Error=%s",
                   PathUtf8 ? PathUtf8 : "<null>",
                   AttemptLabel ? AttemptLabel : "unknown",
                   ReaderFormat,
                   ToStatusCodeString(Status.GetCode()),
                   Status.GetErrorString());
        }
        return bInitialized;
    }
}

void FBXImporter::ReportProgress(int Percent, const wchar_t* Status) const
{
    if (ProgressCallback)
    {
        ProgressCallback(Percent, Status);
    }
}

bool FBXImporter::ImportFbxAsset(const FString &InFilePath, FFBXAsset &OutFBXAsset,
                                  const FFBXImportOptions &Options,
                                  FFBXProgressCallback     ProgressCb)
{
    ProgressCallback = std::move(ProgressCb);

    ClearAsset(OutFBXAsset);

    ReportProgress(2, L"Initializing FBX SDK...");
    if (!InitializeSdk())
    {
        return false;
    }

    ReportProgress(5, L"Loading FBX file...");
    if (!LoadScene(InFilePath))
    {
        ShutdownSdk();
        return false;
    }
    ReportProgress(40, L"Analyzing scene structure...");

    ImportMeta.SourceFilePath = InFilePath;

    FFbxMetaParser MetaParser(ImportMeta);
    if (!MetaParser.BuildFbxMeta(Scene))
    {
        ShutdownSdk();
        return false;
    }
    ReportProgress(50, L"Parsing static meshes...");

    OutFBXAsset.PathFileName = InFilePath;

    FFbxStaticMeshParser StaticMeshParser(ImportMeta);
    if (!StaticMeshParser.Parse(OutFBXAsset.StaticMeshes, OutFBXAsset.MeshIdToStaticMeshAssetIndex))
    {
        ShutdownSdk();
        return false;
    }
    ReportProgress(55, L"Parsing skeletal meshes...");

    TArray<FFbxSkinnedMeshPart> SkinnedMeshParts;
    FFbxSkeletalMeshPartParser  SkeletalMeshPartParser(ImportMeta);
    if (!SkeletalMeshPartParser.Parse(SkinnedMeshParts))
    {
        ShutdownSdk();
        return false;
    }
    ReportProgress(65, L"Building skeletal meshes...");

    FFbxSkeletalMeshAssembler SkeletalMeshAssembler(ImportMeta);
    if (!SkeletalMeshAssembler.Assemble(SkinnedMeshParts, OutFBXAsset.SkeletalMeshes,
                                        OutFBXAsset.SkeletonIdToSkeletalMeshAssetIndex))
    {
        ShutdownSdk();
        return false;
    }
    ReportProgress(75, L"Baking animations...");

    // Determine effective sample rate
    float NativeFPS = 30.0f;
    if (Scene)
    {
        const FbxTime::EMode TimeMode = Scene->GetGlobalSettings().GetTimeMode();
        const double NativeRate = FbxTime::GetFrameRate(TimeMode);
        if (NativeRate > 0.0)
        {
            NativeFPS = static_cast<float>(NativeRate);
        }
    }
    const float SampleRate = Options.GetEffectiveFPS(NativeFPS);
    UE_LOG("[FBXImporter] Animation bake rate. NativeFPS=%.2f SampleRate=%.2f FPSMode=%d CustomFPS=%.2f",
           NativeFPS, SampleRate, static_cast<int>(Options.FPSMode), Options.CustomFPS);

    {
        FFbxAnimationParser AnimationParser(ImportMeta);
        for (const FFbxSkeletonMeta &SkeletonMeta : ImportMeta.Skeletons)
        {
            auto AssetIndexIt =
                OutFBXAsset.SkeletonIdToSkeletalMeshAssetIndex.find(SkeletonMeta.SkeletonId);
            if (AssetIndexIt == OutFBXAsset.SkeletonIdToSkeletalMeshAssetIndex.end())
            {
                continue;
            }

            const int32 SkeletalMeshIndex = AssetIndexIt->second;
            if (!IsValidIndex(OutFBXAsset.SkeletalMeshes, SkeletalMeshIndex))
            {
                continue;
            }

            FSkeletalMesh& SkeletalMesh = OutFBXAsset.SkeletalMeshes[SkeletalMeshIndex];
            if (OutFBXAsset.Skeletons.size() <= static_cast<size_t>(SkeletalMeshIndex))
            {
                OutFBXAsset.Skeletons.resize(SkeletalMeshIndex + 1);
            }
            if (OutFBXAsset.AnimSequences.size() <= static_cast<size_t>(SkeletalMeshIndex))
            {
                OutFBXAsset.AnimSequences.resize(SkeletalMeshIndex + 1);
            }

            SkeletalMesh.SkeletonAssetPath = InFilePath + "#SkeletonAsset_" + std::to_string(SkeletonMeta.SkeletonId);
            FSkeleton &SkeletonAsset = OutFBXAsset.Skeletons[SkeletalMeshIndex];
            SkeletonAsset.PathFileName = SkeletalMesh.SkeletonAssetPath;
            SkeletonAsset.Bones = std::move(SkeletalMesh.Bones);
            SkeletonAsset.RebuildBoneNameToIndex();

            const TArray<int32>* FilterIndices =
                Options.AnimationFilterIndices.empty() ? nullptr : &Options.AnimationFilterIndices;

            AnimationParser.ParseSkeletonAnimations(Scene, SkeletonMeta,
                                                    SkeletonAsset.Bones,
                                                    OutFBXAsset.AnimSequences[SkeletalMeshIndex],
                                                    SampleRate,
                                                    FilterIndices);

            for (UAnimSequence* AnimSequence : OutFBXAsset.AnimSequences[SkeletalMeshIndex])
            {
                if (AnimSequence)
                {
                    AnimSequence->SetSkeletonAssetPath(SkeletalMesh.SkeletonAssetPath);
                }
            }
        }
    }
    ReportProgress(90, L"Setting up materials...");

    OutFBXAsset.StaticMeshMaterials.resize(OutFBXAsset.StaticMeshes.size());
    for (int32 StaticMeshIndex = 0;
         StaticMeshIndex < static_cast<int32>(OutFBXAsset.StaticMeshes.size()); ++StaticMeshIndex)
    {
        FbxMaterialImportUtils::BuildStaticMaterials(
            ImportMeta, OutFBXAsset.StaticMeshes[StaticMeshIndex],
            OutFBXAsset.StaticMeshMaterials[StaticMeshIndex]);
    }

    OutFBXAsset.SkeletalMeshMaterials.resize(OutFBXAsset.SkeletalMeshes.size());
    for (int32 SkeletalMeshIndex = 0;
         SkeletalMeshIndex < static_cast<int32>(OutFBXAsset.SkeletalMeshes.size());
         ++SkeletalMeshIndex)
    {
        FbxMaterialImportUtils::BuildSkeletalMaterials(
            ImportMeta, OutFBXAsset.SkeletalMeshes[SkeletalMeshIndex],
            OutFBXAsset.SkeletalMeshMaterials[SkeletalMeshIndex]);
    }
    if (!OutFBXAsset.SkeletalMeshMaterials.empty())
    {
        OutFBXAsset.SkeletalMaterials = OutFBXAsset.SkeletalMeshMaterials[0];
    }

    BuildSceneComponents(ImportMeta, OutFBXAsset);
    ReportProgress(98, L"Finalizing...");

    FinalizeAsset();

    ShutdownSdk();

    ReportProgress(100, L"Done");
    return true;
}

bool FBXImporter::PeekFbxInfo(const FString &InFilePath, FFBXPeekInfo &OutInfo)
{
    OutInfo = {};

    FbxManager* PeekManager = FbxManager::Create();
    if (!PeekManager)
    {
        return false;
    }

    FbxIOSettings* IOSettings = FbxIOSettings::Create(PeekManager, IOSROOT);
    if (IOSettings)
    {
        // Skip geometry and materials – we only need animation stack names and global settings
        IOSettings->SetBoolProp(IMP_FBX_MATERIAL,         false);
        IOSettings->SetBoolProp(IMP_FBX_TEXTURE,          false);
        IOSettings->SetBoolProp(IMP_FBX_LINK,             false);
        IOSettings->SetBoolProp(IMP_FBX_SHAPE,            false);
        IOSettings->SetBoolProp(IMP_FBX_GOBO,             false);
        PeekManager->SetIOSettings(IOSettings);
    }

    FbxImporter* PeekImporter = FbxImporter::Create(PeekManager, "");
    if (!PeekImporter)
    {
        PeekManager->Destroy();
        return false;
    }

    const std::string ResolvedPath = NormalizeUtf8Path(FPaths::ToWide(InFilePath));
    const bool bInit = PeekImporter->Initialize(ResolvedPath.c_str(), -1, PeekManager->GetIOSettings());
    if (!bInit)
    {
        PeekImporter->Destroy();
        PeekManager->Destroy();
        return false;
    }

    FbxScene* PeekScene = FbxScene::Create(PeekManager, "PeekScene");
    if (!PeekScene || !PeekImporter->Import(PeekScene))
    {
        if (PeekScene) PeekScene->Destroy();
        PeekImporter->Destroy();
        PeekManager->Destroy();
        return false;
    }

    // Native FPS from global settings
    const FbxTime::EMode TimeMode = PeekScene->GetGlobalSettings().GetTimeMode();
    const double NativeRate = FbxTime::GetFrameRate(TimeMode);
    OutInfo.NativeFPS = (NativeRate > 0.0) ? static_cast<float>(NativeRate) : 30.0f;

    // Animation stack names
    const int32 StackCount = PeekScene->GetSrcObjectCount<FbxAnimStack>();
    for (int32 i = 0; i < StackCount; ++i)
    {
        FbxAnimStack* Stack = PeekScene->GetSrcObject<FbxAnimStack>(i);
        if (Stack && Stack->GetName())
        {
            OutInfo.AnimationNames.push_back(FString(Stack->GetName()));
        }
        else
        {
            OutInfo.AnimationNames.push_back(FString("Anim_") + std::to_string(i));
        }
    }

    PeekScene->Destroy();
    PeekImporter->Destroy();
    PeekManager->Destroy();
    return true;
}

bool FBXImporter::InitializeSdk()
{
    ShutdownSdk();

    Manager = FbxManager::Create();
    if (!Manager)
    {
        UE_LOG("[FBXImporter] Failed to create FbxManager.");
        return false;
    }

    FbxIOSettings *IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
    if (!IOSettings)
    {
        UE_LOG("[FBXImporter] Failed to create FbxIOSettings.");
        ShutdownSdk();
        return false;
    }

    Manager->SetIOSettings(IOSettings);
    return true;
}

void FBXImporter::ShutdownSdk()
{
    DestroyScene();
    ClearState();

    if (Manager)
    {
        Manager->Destroy();
        Manager = nullptr;
    }
}

bool FBXImporter::LoadScene(const FString &InFilePath)
{
    if (!Manager)
    {
        return false;
    }

    DestroyScene();

    FbxImporter *Importer = FbxImporter::Create(Manager, "");
    if (!Importer)
    {
        UE_LOG("[FBXImporter] Failed to create FbxImporter.");
        return false;
    }

    const std::string ResolvedPath = ResolveImportDiskPath(InFilePath);
    if (!FileExistsOnDisk(ResolvedPath))
    {
        UE_LOG("[FBXImporter] Source FBX file not found. Requested=%s Resolved=%s",
               InFilePath.c_str(), ResolvedPath.c_str());
        Importer->Destroy();
        return false;
    }

    int PreferredReaderFormat = -1;
    if (HasFbxExtension(ResolvedPath))
    {
        PreferredReaderFormat = FindPreferredFbxReaderFormat(Manager, ResolvedPath.c_str());
    }

    LogReaderDiagnostics(Manager, ResolvedPath.c_str(), PreferredReaderFormat);

    bool bInitialized = TryInitializeImporter(Importer, Manager, ResolvedPath.c_str(),
                                              PreferredReaderFormat, "preferred");
    if (!bInitialized && PreferredReaderFormat >= 0)
    {
        bInitialized = TryInitializeImporter(Importer, Manager, ResolvedPath.c_str(), -1,
                                             "autodetect");
    }

    if (!bInitialized)
    {
        const FbxStatus &Status = Importer->GetStatus();
        UE_LOG("[FBXImporter] Initialize failed. Requested=%s Resolved=%s Status=%s Error=%s",
               InFilePath.c_str(), ResolvedPath.c_str(), ToStatusCodeString(Status.GetCode()),
               Status.GetErrorString());
        Importer->Destroy();
        return false;
    }

    Scene = FbxScene::Create(Manager, "ImportScene");
    if (!Scene)
    {
        UE_LOG("[FBXImporter] Failed to create FbxScene.");
        Importer->Destroy();
        return false;
    }

    const bool bImported = Importer->Import(Scene);
    if (!bImported)
    {
        UE_LOG("[FBXImporter] Import failed: %s. Error: %s", InFilePath.c_str(),
               Importer->GetStatus().GetErrorString());
    }

    Importer->Destroy();
    if (bImported)
    {
        PreprocessScene();
    }
    else
    {
        DestroyScene();
    }
    return bImported;
}

bool FBXImporter::FinalizeAsset() { return true; }

void FBXImporter::ClearState() { ImportMeta.Clear(); }

void FBXImporter::PreprocessScene()
{
    if (!Scene || !Manager)
    {
        return;
    }

    const FbxAxisSystem EngineAxisSystem(FbxAxisSystem::eZAxis,
                                         FbxAxisSystem::eParityEven,
                                         FbxAxisSystem::eLeftHanded);
    EngineAxisSystem.DeepConvertScene(Scene);

    FbxSystemUnit::ConversionOptions UnitOptions = {};
    UnitOptions.mConvertRrsNodes = true;
    UnitOptions.mConvertLimits = true;
    UnitOptions.mConvertClusters = true;
    UnitOptions.mConvertLightIntensity = true;
    UnitOptions.mConvertPhotometricLProperties = true;
    UnitOptions.mConvertCameraClipPlanes = true;

    const FbxSystemUnit CentimeterUnit(100.0);
    CentimeterUnit.ConvertScene(Scene, UnitOptions);

    /**
     * 외부 포맷 또는 중간 데이터를 엔진 내부 표현으로 변환한다.
     */
    FbxGeometryConverter Converter(Manager);
    Converter.Triangulate(Scene, true);
}

void FBXImporter::DestroyScene()
{
    if (Scene)
    {
        Scene->Destroy();
        Scene = nullptr;
    }
}
