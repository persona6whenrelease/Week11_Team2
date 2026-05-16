/**
 * FBX 머티리얼 정보를 .mat 파일과 메시 머티리얼 슬롯으로 변환한다.
 *
 * 임포트된 diffuse/specular/normal 텍스처 참조와 기본 색상 값을 엔진 머티리얼 JSON 구조에 맞춰 저장한다.
 * 슬롯 이름은 파일명으로 사용할 수 있도록 정리하고, 메시 섹션이 같은 이름으로 머티리얼을 찾을 수 있게
 * 동일한 정규화 규칙을 적용한다.
 */

#include "Asset/Import/FBX/Material/FbxMaterialImportUtils.h"

#include "Core/Log.h"
#include "Engine/Platform/Paths.h"
#include "Asset/Material/Material.h"
#include "Asset/Material/MaterialManager.h"
#include "SimpleJSON/json.hpp"

#include <filesystem>
#include <fstream>

namespace
{
    template <typename T> bool IsValidIndex(const TArray<T> &Items, int32 Index)
    {
        return Index >= 0 && static_cast<size_t>(Index) < Items.size();
    }

    FString SanitizeMaterialFileName(const FString &SlotName)
    {
        FString Result = FbxMaterialImportUtils::NormalizeMaterialSlotName(SlotName);
        for (char &Ch : Result)
        {
            const unsigned char UCh = static_cast<unsigned char>(Ch);
            if (UCh < 32 || Ch == '<' || Ch == '>' || Ch == ':' || Ch == '"' || Ch == '/' ||
                Ch == '\\' || Ch == '|' || Ch == '?' || Ch == '*')
            {
                Ch = '_';
            }
        }
        return Result.empty() ? "None" : Result;
    }

    /**
     * 이미 수집된 메타 데이터에서 조건에 맞는 항목을 찾아 반환한다.
     */
    int32 FindMaterialInfoBySlotName(const FFbxImportMeta &ImportMeta, const FString &SlotName)
    {
        auto It = ImportMeta.MaterialNameToMaterialId.find(
            FbxMaterialImportUtils::NormalizeMaterialSlotName(SlotName));
        return It != ImportMeta.MaterialNameToMaterialId.end() ? It->second : -1;
    }

    /**
     * 외부 포맷 또는 중간 데이터를 엔진 내부 표현으로 변환한다.
     */
    FString ConvertFbxMaterialInfoToMat(FFbxMaterialInfo &MaterialInfo)
    {
        const FString SlotName =
            FbxMaterialImportUtils::NormalizeMaterialSlotName(MaterialInfo.MaterialSlotName);
        if (SlotName == "None")
        {
            MaterialInfo.MaterialAssetPath = "None";
            return MaterialInfo.MaterialAssetPath;
        }

        const FString MatPath =
            "Asset/Materials/Auto/" + SanitizeMaterialFileName(SlotName) + ".mat";
        MaterialInfo.MaterialAssetPath = MatPath;

        std::wstring MatDiskPath;
        FString      Error;
        if (!FPaths::TryResolvePackagePath(MatPath, MatDiskPath, &Error))
        {
            return "";
        }

        const std::filesystem::path DiskPath(MatDiskPath);
        if (std::filesystem::exists(DiskPath))
        {
            UE_LOG("[FBXImporter] Auto material exists; skip overwrite. Path=%s", MatPath.c_str());
            return MatPath;
        }

        std::filesystem::create_directories(DiskPath.parent_path());

        json::JSON JsonData;
        JsonData["PathFileName"] = MatPath;
        JsonData["Origin"] = "FbxImport";
        JsonData["ShaderPath"] = "Shaders/Geometry/UberLit.hlsl";
        JsonData["RenderPass"] = "Opaque";

        const bool bHasAnyTexture =
            !MaterialInfo.DiffuseTexturePath.empty() || !MaterialInfo.NormalTexturePath.empty() ||
            !MaterialInfo.SpecularTexturePath.empty() || !MaterialInfo.EmissiveTexturePath.empty();

        if (bHasAnyTexture)
        {
            if (!MaterialInfo.DiffuseTexturePath.empty())
            {
                JsonData["Textures"]["DiffuseTexture"] = MaterialInfo.DiffuseTexturePath;
            }
            if (!MaterialInfo.NormalTexturePath.empty())
            {
                JsonData["Textures"]["NormalTexture"] = MaterialInfo.NormalTexturePath;
            }
            if (!MaterialInfo.SpecularTexturePath.empty())
            {
                JsonData["Textures"]["SpecularTexture"] = MaterialInfo.SpecularTexturePath;
            }
            if (!MaterialInfo.EmissiveTexturePath.empty())
            {
                JsonData["Textures"]["EmissiveTexture"] = MaterialInfo.EmissiveTexturePath;
            }

            JsonData["Parameters"]["SectionColor"][0] = 1.0f;
            JsonData["Parameters"]["SectionColor"][1] = 1.0f;
            JsonData["Parameters"]["SectionColor"][2] = 1.0f;
            JsonData["Parameters"]["SectionColor"][3] = 1.0f;
        }
        else
        {
            JsonData["Parameters"]["SectionColor"][0] = MaterialInfo.DiffuseColor.X;
            JsonData["Parameters"]["SectionColor"][1] = MaterialInfo.DiffuseColor.Y;
            JsonData["Parameters"]["SectionColor"][2] = MaterialInfo.DiffuseColor.Z;
            JsonData["Parameters"]["SectionColor"][3] = 1.0f;
        }

#if !IS_GAME_CLIENT
        std::ofstream File(DiskPath, std::ios::binary);
        File << JsonData.dump();
#endif

        return MatPath;
    }
} 

namespace FbxMaterialImportUtils
{
    /**
     * FBX 머티리얼 이름을 파일명과 슬롯 비교에 안전한 형태로 정규화한다.
     */
    FString NormalizeMaterialSlotName(const FString &SlotName)
    {
        return SlotName.empty() ? "None" : SlotName;
    }

    void BuildStaticMaterials(FFbxImportMeta &ImportMeta, const FStaticMesh &Mesh,
                              TArray<FMeshMaterial> &OutMaterials)
    {
        OutMaterials.clear();
        TSet<FString> AddedSlotNames;
        UMaterial    *FallbackMaterial = FMaterialManager::Get().GetOrCreateMaterial("None");

        for (const FStaticMeshSection &Section : Mesh.Sections)
        {
            const FString SlotName = NormalizeMaterialSlotName(Section.MaterialSlotName);
            if (AddedSlotNames.find(SlotName) != AddedSlotNames.end())
            {
                continue;
            }

            FMeshMaterial Material;
            Material.MaterialSlotName = SlotName;
            Material.MaterialInterface = FallbackMaterial;
            FString MaterialPath = "None";

            const int32 MaterialInfoId = FindMaterialInfoBySlotName(ImportMeta, SlotName);
            if (IsValidIndex(ImportMeta.Materials, MaterialInfoId))
            {
                MaterialPath = ConvertFbxMaterialInfoToMat(ImportMeta.Materials[MaterialInfoId]);
                if (!MaterialPath.empty())
                {
                    Material.MaterialInterface =
                        FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);
                }
            }

            OutMaterials.push_back(std::move(Material));
            AddedSlotNames.insert(SlotName);
        }

        if (OutMaterials.empty())
        {
            FMeshMaterial Material;
            Material.MaterialSlotName = "None";
            Material.MaterialInterface = FallbackMaterial;
            OutMaterials.push_back(std::move(Material));
        }
    }

    void BuildSkeletalMaterials(FFbxImportMeta &ImportMeta, const FSkeletalMesh &Mesh,
                                TArray<FMeshMaterial> &OutMaterials)
    {
        OutMaterials.clear();
        TSet<FString> AddedSlotNames;
        UMaterial    *FallbackMaterial = FMaterialManager::Get().GetOrCreateMaterial("None");

        for (const FMeshSection &Section : Mesh.Sections)
        {
            const FString SlotName = NormalizeMaterialSlotName(Section.MaterialSlotName);
            if (AddedSlotNames.find(SlotName) != AddedSlotNames.end())
            {
                continue;
            }

            FMeshMaterial Material;
            Material.MaterialSlotName = SlotName;
            Material.MaterialInterface = FallbackMaterial;
            FString MaterialPath = "None";

            const int32 MaterialInfoId = FindMaterialInfoBySlotName(ImportMeta, SlotName);
            if (IsValidIndex(ImportMeta.Materials, MaterialInfoId))
            {
                MaterialPath = ConvertFbxMaterialInfoToMat(ImportMeta.Materials[MaterialInfoId]);
                if (!MaterialPath.empty())
                {
                    Material.MaterialInterface =
                        FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);
                }
            }

            OutMaterials.push_back(std::move(Material));
            AddedSlotNames.insert(SlotName);

            const FMeshMaterial &AddedMaterial = OutMaterials.back();
            const FString        ResolvedMaterialPath =
                AddedMaterial.MaterialInterface
                           ? AddedMaterial.MaterialInterface->GetAssetPathFileName()
                           : FString("<null>");
            UE_LOG("[FBXImporter] Skeletal material. Index=%d SlotName=%s MaterialPath=%s "
                   "ResolvedMaterial=%s",
                   static_cast<int32>(OutMaterials.size() - 1), SlotName.c_str(),
                   MaterialPath.c_str(), ResolvedMaterialPath.c_str());
        }

        if (OutMaterials.empty())
        {
            FMeshMaterial Material;
            Material.MaterialSlotName = "None";
            Material.MaterialInterface = FallbackMaterial;
            OutMaterials.push_back(std::move(Material));

            UE_LOG("[FBXImporter] Skeletal material. Index=0 SlotName=None MaterialPath=None "
                   "ResolvedMaterial=%s",
                   FallbackMaterial ? FallbackMaterial->GetAssetPathFileName().c_str() : "<null>");
        }
    }
} 
