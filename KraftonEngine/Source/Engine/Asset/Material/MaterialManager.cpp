/**
 * 머티리얼 JSON 파싱, 기본값 보정, 텍스처 연결을 구현한다.
 *
 * 저장된 머티리얼 파일에서 파라미터와 텍스처 경로를 읽어 UMaterial에 적용한다. 오래된 파일이나 누락된
 * 파라미터가 있어도 현재 템플릿 정의에 맞춰 기본값을 주입하고, 더 이상 사용하지 않는 항목은 정리한다.
 */

#include "MaterialManager.h"
#include <filesystem>
#include <fstream>
#include "Asset/Material/Material.h"
#include "Platform/Paths.h"
#include "Render/Shader/ShaderManager.h"
#include "Render/Resource/Buffer.h"
#include "Asset/Texture/Texture2D.h"
#include "Render/Pipeline/Renderer.h"

void FMaterialManager::ScanMaterialAssets()
{
	AvailableMaterialFiles.clear();

	const std::filesystem::path MaterialRoot = FPaths::RootDir() + L"Asset\\Materials\\";

	if (!std::filesystem::exists(MaterialRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(MaterialRoot))
	{
		if (!Entry.is_regular_file()) continue;

		const std::filesystem::path& Path = Entry.path();

		if (Path.extension() != L".mat") continue;
		if (Path.stem() == L"None") continue; 

		FMaterialAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Path.stem().wstring());
		Item.FullPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());
		AvailableMaterialFiles.push_back(std::move(Item));
	}
}

UMaterial* FMaterialManager::GetOrCreateMaterial(const FString& MatFilePath)
{
	std::filesystem::path Path(FPaths::ToWide(MatFilePath));
	FString GenericPath = FPaths::ToUtf8(Path.generic_wstring());
	
	auto It = MaterialCache.find(GenericPath);
	if (It != MaterialCache.end())
	{
		return It->second;
	}

	
	json::JSON JsonData = ReadJsonFile(GenericPath);
	if (JsonData.IsNull())
	{
		
		UMaterial* DefaultMaterial = UObjectManager::Get().CreateObject<UMaterial>();
		FMaterialTemplate* Template = GetOrCreateTemplate(DefaultShaderPath);
		TMap<FString, std::unique_ptr<FMaterialConstantBuffer>> Buffers = CreateConstantBuffers(Template);
		DefaultMaterial->Create(GenericPath, Template, ERenderPass::Opaque, EBlendState::Opaque, EDepthStencilState::Default, ERasterizerState::SolidBackCull, std::move(Buffers));
		
		DefaultMaterial->SetVector4Parameter("SectionColor", FVector4(1.0f, 0.0f, 1.0f, 1.0f));
		MaterialCache.emplace(GenericPath, DefaultMaterial);
		return DefaultMaterial;
	}

	
	FString PathFileName = JsonData[MatKeys::PathFileName].ToString().c_str();
	FString ShaderPath = JsonData[MatKeys::ShaderPath].ToString().c_str();
	FString RenderPassStr = JsonData[MatKeys::RenderPass].ToString().c_str();
	ERenderPass RenderPass = StringToRenderPass(RenderPassStr);

	
	FString BlendStr = JsonData.hasKey(MatKeys::BlendState) ? JsonData[MatKeys::BlendState].ToString().c_str() : "";
	FString DepthStr = JsonData.hasKey(MatKeys::DepthStencilState) ? JsonData[MatKeys::DepthStencilState].ToString().c_str() : "";
	FString RasterStr = JsonData.hasKey(MatKeys::RasterizerState) ? JsonData[MatKeys::RasterizerState].ToString().c_str() : "";

	EBlendState BlendState = StringToBlendState(BlendStr, RenderPass);
	EDepthStencilState DepthState = StringToDepthStencilState(DepthStr, RenderPass);
	ERasterizerState RasterState = StringToRasterizerState(RasterStr, RenderPass);

	
	FMaterialTemplate* Template = GetOrCreateTemplate(ShaderPath);
	if (!Template) return nullptr;

	
	auto InjectedBuffers = CreateConstantBuffers(Template);

	
	UMaterial* Material = UObjectManager::Get().CreateObject<UMaterial>();
	Material->Create(PathFileName, Template, RenderPass, BlendState, DepthState, RasterState, std::move(InjectedBuffers));
	MaterialCache.emplace(GenericPath, Material);

	
	bool bInjected = InjectDefaultParameters(JsonData, Template, Material);

	
	bool bPurged = PurgeStaleParameters(JsonData, Template);

	
	ApplyParameters(Material, JsonData);
	ApplyTextures(Material, JsonData);
	Material->RebuildCachedSRVs();

	
	JsonData[MatKeys::BlendState] = BlendStr.empty() ? "" : BlendStr.c_str();
	JsonData[MatKeys::DepthStencilState] = DepthStr.empty() ? "" : DepthStr.c_str();
	JsonData[MatKeys::RasterizerState] = RasterStr.empty() ? "" : RasterStr.c_str();

	
	if (bInjected || bPurged)
	{
		SaveToJSON(JsonData, GenericPath);
	}

	return Material;
}

json::JSON FMaterialManager::ReadJsonFile(const FString& FilePath) const
{
	std::wstring DiskPath;
	FString Error;
	if (!FPaths::TryResolvePackagePath(FilePath, DiskPath, &Error))
	{
		return json::JSON();
	}

	std::ifstream File(std::filesystem::path(DiskPath), std::ios::binary);
	if (!File.is_open()) return json::JSON(); 

	std::stringstream Buffer;
	Buffer << File.rdbuf();
	return json::JSON::Load(Buffer.str());
}

TMap<FString, std::unique_ptr<FMaterialConstantBuffer>> FMaterialManager::CreateConstantBuffers(FMaterialTemplate* Template)
{

	TMap<FString, std::unique_ptr<FMaterialConstantBuffer>> InjectedBuffers;

	const auto& RequiredBuffers = Template->GetParameterInfo();
	std::vector<FString> CreatedBuffers;

	for (const auto& BufferInfo : RequiredBuffers)
	{
		const FMaterialParameterInfo* ParamInfo = BufferInfo.second;

		if (std::find(CreatedBuffers.begin(), CreatedBuffers.end(), ParamInfo->BufferName) != CreatedBuffers.end())
			continue;

		auto MatCB = std::make_unique<FMaterialConstantBuffer>();
		MatCB->Init(Device, ParamInfo->BufferSize, ParamInfo->SlotIndex);

		InjectedBuffers.emplace(ParamInfo->BufferName, std::move(MatCB));
		CreatedBuffers.push_back(ParamInfo->BufferName);
	}

	return InjectedBuffers;
}

void FMaterialManager::ApplyParameters(UMaterial* Material, json::JSON& JsonData)
{
	if (!JsonData.hasKey(MatKeys::Parameters)) return;

	for (auto& Pair : JsonData[MatKeys::Parameters].ObjectRange())
	{
		FString ParamName = Pair.first.c_str();
		json::JSON& Value = Pair.second;

		if (Value.JSONType() == json::JSON::Class::Array)
		{
			if (Value.length() == 3)
			{
				Material->SetVector3Parameter(ParamName, FVector((float)Value[0].ToFloat(), (float)Value[1].ToFloat(), (float)Value[2].ToFloat()));
			}
			else if (Value.length() == 4)
			{
				Material->SetVector4Parameter(ParamName, FVector4((float)Value[0].ToFloat(), (float)Value[1].ToFloat(), (float)Value[2].ToFloat(), (float)Value[3].ToFloat()));
			}
		}
		else if (Value.JSONType() == json::JSON::Class::Floating || Value.JSONType() == json::JSON::Class::Integral)
		{
			Material->SetScalarParameter(ParamName, (float)Value.ToFloat());
		}
	}
}

void FMaterialManager::ApplyTextures(UMaterial* Material, json::JSON& JsonData)
{
	if (!JsonData.hasKey(MatKeys::Textures)) return;

	for (auto& Pair : JsonData[MatKeys::Textures].ObjectRange())
	{
		FString SlotName = Pair.first.c_str();
		FString TexturePath = Pair.second.ToString().c_str();

		UTexture2D* Texture = UTexture2D::LoadFromFile(TexturePath, Device);
		if (Texture)
		{
			Material->SetTextureParameter(SlotName, Texture);
		}
	}
}

ERenderPass FMaterialManager::StringToRenderPass(const FString& Str) const
{
	using namespace RenderStateStrings;
	return FromString(RenderPassMap, Str, ERenderPass::Opaque);
}

EBlendState FMaterialManager::StringToBlendState(const FString& Str, ERenderPass Pass) const
{
	using namespace RenderStateStrings;
	if (!Str.empty())
		return FromString(BlendStateMap, Str, EBlendState::Opaque);

	
	switch (Pass)
	{
	case ERenderPass::AlphaBlend:
	case ERenderPass::Decal:
	case ERenderPass::EditorLines:
	case ERenderPass::PostProcess:
	case ERenderPass::GizmoInner:
	case ERenderPass::OverlayFont:
		return EBlendState::AlphaBlend;
	case ERenderPass::AdditiveDecal:
		return EBlendState::Additive;
	case ERenderPass::SelectionMask:
		return EBlendState::NoColor;
	default:
		return EBlendState::Opaque;
	}
}

EDepthStencilState FMaterialManager::StringToDepthStencilState(const FString& Str, ERenderPass Pass) const
{
	using namespace RenderStateStrings;
	if (!Str.empty())
		return FromString(DepthStencilStateMap, Str, EDepthStencilState::Default);

	
	switch (Pass)
	{
	case ERenderPass::Decal:
	case ERenderPass::AdditiveDecal:
		return EDepthStencilState::DepthReadOnly;
	case ERenderPass::SelectionMask:
		return EDepthStencilState::StencilWrite;
	case ERenderPass::PostProcess:
	case ERenderPass::OverlayFont:
		return EDepthStencilState::NoDepth;
	case ERenderPass::GizmoOuter:
		return EDepthStencilState::GizmoOutside;
	case ERenderPass::GizmoInner:
		return EDepthStencilState::GizmoInside;
	default:
		return EDepthStencilState::Default;
	}
}

ERasterizerState FMaterialManager::StringToRasterizerState(const FString& Str, ERenderPass Pass) const
{
	using namespace RenderStateStrings;
	if (!Str.empty())
		return FromString(RasterizerStateMap, Str, ERasterizerState::SolidBackCull);

	
	switch (Pass)
	{
	case ERenderPass::Decal:
	case ERenderPass::AdditiveDecal:
	case ERenderPass::SelectionMask:
	case ERenderPass::PostProcess:
		return ERasterizerState::SolidNoCull;
	default:
		return ERasterizerState::SolidBackCull;
	}
}

void FMaterialManager::SaveToJSON(json::JSON& JsonData, const FString& MatFilePath)
{
#if IS_GAME_CLIENT
	(void)JsonData;
	(void)MatFilePath;
#else
	std::wstring DiskPath;
	FString Error;
	if (!FPaths::TryResolvePackagePath(MatFilePath, DiskPath, &Error))
	{
		return;
	}

	std::ofstream File(std::filesystem::path(DiskPath), std::ios::binary);
	File << JsonData.dump();
#endif
}

bool FMaterialManager::InjectDefaultParameters(json::JSON& JsonData, FMaterialTemplate* Template, UMaterial* Material)
{
	const auto& Layout = Template->GetParameterInfo();
	bool bInjected = false;

	for (const auto& Pair : Layout)
	{
		const FString& ParamName = Pair.first;
		const FMaterialParameterInfo* Info = Pair.second;

		
		if (!JsonData[MatKeys::Parameters][ParamName].IsNull())
			continue;

		bInjected = true;

		switch (Info->Size)
		{
			case sizeof(float) : 
			{
				float Value = 0.f;
				Material->GetScalarParameter(ParamName, Value);
				JsonData[MatKeys::Parameters][ParamName] = Value;
				break;
			}
			case sizeof(float) * 3: 
			{
				FVector Value;
				Material->GetVector3Parameter(ParamName, Value);
				JsonData[MatKeys::Parameters][ParamName] = json::Array(Value.X, Value.Y, Value.Z);
				break;
			}
			case sizeof(float) * 4: 
			{
				FVector4 Value;
				Material->GetVector4Parameter(ParamName, Value);
				JsonData[MatKeys::Parameters][ParamName] = json::Array(Value.X, Value.Y, Value.Z, Value.W);
				break;
			}
			case sizeof(float) * 16: 
			{
				FMatrix Value;
				Material->GetMatrixParameter(ParamName, Value);
				auto MatArray = json::Array();
				for (int i = 0; i < 16; ++i)
					MatArray.append(Value.Data[i]);
				JsonData[MatKeys::Parameters][ParamName] = MatArray;
				break;
			}
			default:
				break; 
		}
	}

	return bInjected;
}

bool FMaterialManager::PurgeStaleParameters(json::JSON& JsonData, FMaterialTemplate* Template)
{
	if (!JsonData.hasKey(MatKeys::Parameters)) return false;

	const auto& Layout = Template->GetParameterInfo();
	json::JSON CleanParams = json::JSON::Make(json::JSON::Class::Object);
	bool bPurged = false;

	for (auto& Pair : JsonData[MatKeys::Parameters].ObjectRange())
	{
		FString ParamName = Pair.first.c_str();
		if (Layout.find(ParamName) != Layout.end())
		{
			CleanParams[Pair.first] = Pair.second;
		}
		else
		{
			bPurged = true;
		}
	}

	if (bPurged)
	{
		JsonData[MatKeys::Parameters] = std::move(CleanParams);
	}

	return bPurged;
}

FMaterialTemplate* FMaterialManager::GetOrCreateTemplate(const FString& ShaderPath)
{
	
	auto It = TemplateCache.find(ShaderPath);
	if (It != TemplateCache.end())
	{
		return It->second;
	}

	
	
	FShader* Shader = FShaderManager::Get().FindOrCreate(ShaderPath);
	if (!Shader)
	{
		return nullptr;
	}

	FMaterialTemplate* NewTemplate = new FMaterialTemplate();
	NewTemplate->Create(Shader);
	TemplateCache.emplace(ShaderPath, NewTemplate);
	return NewTemplate;
}

FMaterialManager::~FMaterialManager()
{
	if (!Device)
	{
		Release();
	}

}

void FMaterialManager::Release()
{
	
	
	for (auto& Pair : TemplateCache)
	{
		if (Pair.second != nullptr)
		{
			delete Pair.second;
			Pair.second = nullptr;
		}
	}
	TemplateCache.clear();

	
	MaterialCache.clear();

	
	
	Device = nullptr;
}
