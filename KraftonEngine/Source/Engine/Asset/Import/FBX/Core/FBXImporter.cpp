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

#include <fbxsdk.h>

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
}

bool FBXImporter::ImportFbxAsset(const FString &InFilePath, FFBXAsset &OutFBXAsset)
{
    ClearAsset(OutFBXAsset);

    if (!InitializeSdk())
    {
        return false;
    }

    if (!LoadScene(InFilePath))
    {
        ShutdownSdk();
        return false;
    }

    ImportMeta.SourceFilePath = InFilePath;

    FFbxMetaParser MetaParser(ImportMeta);
    if (!MetaParser.BuildFbxMeta(Scene))
    {
        ShutdownSdk();
        return false;
    }

    OutFBXAsset.PathFileName = InFilePath;

    FFbxStaticMeshParser StaticMeshParser(ImportMeta);
    if (!StaticMeshParser.Parse(OutFBXAsset.StaticMeshes, OutFBXAsset.MeshIdToStaticMeshAssetIndex))
    {
        ShutdownSdk();
        return false;
    }

    TArray<FFbxSkinnedMeshPart> SkinnedMeshParts;
    FFbxSkeletalMeshPartParser  SkeletalMeshPartParser(ImportMeta);
    if (!SkeletalMeshPartParser.Parse(SkinnedMeshParts))
    {
        ShutdownSdk();
        return false;
    }

    FFbxSkeletalMeshAssembler SkeletalMeshAssembler(ImportMeta);
    if (!SkeletalMeshAssembler.Assemble(SkinnedMeshParts, OutFBXAsset.SkeletalMeshes,
                                        OutFBXAsset.SkeletonIdToSkeletalMeshAssetIndex))
    {
        ShutdownSdk();
        return false;
    }

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

            AnimationParser.ParseSkeletonAnimations(Scene, SkeletonMeta,
                                                    SkeletonAsset.Bones,
                                                    OutFBXAsset.AnimSequences[SkeletalMeshIndex]);

            for (UAnimSequence* AnimSequence : OutFBXAsset.AnimSequences[SkeletalMeshIndex])
            {
                if (AnimSequence)
                {
                    AnimSequence->SetSkeletonAssetPath(SkeletalMesh.SkeletonAssetPath);
                }
            }
        }
    }

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

    FinalizeAsset();

    ShutdownSdk();

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

    const bool bInitialized =
        Importer->Initialize(InFilePath.c_str(), -1, Manager->GetIOSettings());

    if (!bInitialized)
    {
        UE_LOG("[FBXImporter] Initialize failed: %s. Error: %s", InFilePath.c_str(),
               Importer->GetStatus().GetErrorString());
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

    FbxAxisSystem EngineAxisSystem;
    if (!FbxAxisSystem::ParseAxisSystem("yzx", EngineAxisSystem))
    {
        UE_LOG("[FBXImporter] Failed to parse engine axis system.");
        return;
    }

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
