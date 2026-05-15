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
		if (Path.stem() == L"None") continue; // Fallback 癒명떚由ъ뼹? 紐⑸줉?먯꽌 ?쒖쇅

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
	// 1. 罹먯떆 諛섑솚
	auto It = MaterialCache.find(GenericPath);
	if (It != MaterialCache.end())
	{
		return It->second;
	}

	// 2. 罹먯떆???녿떎硫?JSON?먯꽌 ?쎄린 
	json::JSON JsonData = ReadJsonFile(GenericPath);
	if (JsonData.IsNull())
	{
		// 湲곕낯 癒명떚由ъ뼹 ?앹꽦
		UMaterial* DefaultMaterial = UObjectManager::Get().CreateObject<UMaterial>();
		FMaterialTemplate* Template = GetOrCreateTemplate(DefaultShaderPath);
		TMap<FString, std::unique_ptr<FMaterialConstantBuffer>> Buffers = CreateConstantBuffers(Template);
		DefaultMaterial->Create(GenericPath, Template, ERenderPass::Opaque, EBlendState::Opaque, EDepthStencilState::Default, ERasterizerState::SolidBackCull, std::move(Buffers));
		// ?대갚: ?묓겕?됱쑝濡?誘몄???癒명떚由ъ뼹?꾩쓣 ?쒖떆
		DefaultMaterial->SetVector4Parameter("SectionColor", FVector4(1.0f, 0.0f, 1.0f, 1.0f));
		MaterialCache.emplace(GenericPath, DefaultMaterial);
		return DefaultMaterial;
	}

	// 3. JSON?먯꽌 湲곕낯 ?뺣낫 異붿텧
	FString PathFileName = JsonData[MatKeys::PathFileName].ToString().c_str();
	FString ShaderPath = JsonData[MatKeys::ShaderPath].ToString().c_str();
	FString RenderPassStr = JsonData[MatKeys::RenderPass].ToString().c_str();
	ERenderPass RenderPass = StringToRenderPass(RenderPassStr);

	// ?덈줈???뚮뜑 ?곹깭 異붿텧 (JSON???놁쑝硫??⑥뒪 湲곕컲 湲곕낯媛?
	FString BlendStr = JsonData.hasKey(MatKeys::BlendState) ? JsonData[MatKeys::BlendState].ToString().c_str() : "";
	FString DepthStr = JsonData.hasKey(MatKeys::DepthStencilState) ? JsonData[MatKeys::DepthStencilState].ToString().c_str() : "";
	FString RasterStr = JsonData.hasKey(MatKeys::RasterizerState) ? JsonData[MatKeys::RasterizerState].ToString().c_str() : "";

	EBlendState BlendState = StringToBlendState(BlendStr, RenderPass);
	EDepthStencilState DepthState = StringToDepthStencilState(DepthStr, RenderPass);
	ERasterizerState RasterState = StringToRasterizerState(RasterStr, RenderPass);

	// 4. ?쒗뵆由??뺣낫 (?놁쑝硫?由ы뵆?됱뀡???듯빐 ?앹꽦??
	FMaterialTemplate* Template = GetOrCreateTemplate(ShaderPath);
	if (!Template) return nullptr;

	// 5. D3D ?곸닔 踰꾪띁 ?앹꽦
	auto InjectedBuffers = CreateConstantBuffers(Template);

	// 6. UMaterial ?몄뒪?댁뒪 ?앹꽦 諛?珥덇린??(RenderPass???몄뒪?댁뒪蹂?
	UMaterial* Material = UObjectManager::Get().CreateObject<UMaterial>();
	Material->Create(PathFileName, Template, RenderPass, BlendState, DepthState, RasterState, std::move(InjectedBuffers));
	MaterialCache.emplace(GenericPath, Material);

	//?쒗뵆由우쓣 ?듯빐 material???ｊ린
	bool bInjected = InjectDefaultParameters(JsonData, Template, Material);

	// ?댁쟾 ?곗씠?붿쓽 李뚭볼湲??뚮씪誘명꽣 ?뺣━
	bool bPurged = PurgeStaleParameters(JsonData, Template);

	// 5. ?뚮씪誘명꽣 諛??띿뒪泥??곸슜
	ApplyParameters(Material, JsonData);
	ApplyTextures(Material, JsonData);
	Material->RebuildCachedSRVs();

	// JSON ?곗씠?곗뿉???꾩옱 ?곹깭瑜?湲곕줉 (?섏쨷????????좎??섎룄濡?
	JsonData[MatKeys::BlendState] = BlendStr.empty() ? "" : BlendStr.c_str();
	JsonData[MatKeys::DepthStencilState] = DepthStr.empty() ? "" : DepthStr.c_str();
	JsonData[MatKeys::RasterizerState] = RasterStr.empty() ? "" : RasterStr.c_str();

	//理쒖쥌?곸쑝濡?material ???
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
	if (!File.is_open()) return json::JSON(); // Null JSON 諛섑솚

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

	// 臾몄옄?댁씠 鍮꾩뼱?덉쑝硫?Pass 湲곕컲 湲곕낯媛?
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

	// 臾몄옄?댁씠 鍮꾩뼱?덉쑝硫?Pass 湲곕컲 湲곕낯媛?
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

	// 臾몄옄?댁씠 鍮꾩뼱?덉쑝硫?Pass 湲곕컲 湲곕낯媛?
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

		// ?대? JSON???덉쑝硫??ㅽ궢
		if (!JsonData[MatKeys::Parameters][ParamName].IsNull())
			continue;

		bInjected = true;

		switch (Info->Size)
		{
			case sizeof(float) : // 4諛붿씠??- Scalar
			{
				float Value = 0.f;
				Material->GetScalarParameter(ParamName, Value);
				JsonData[MatKeys::Parameters][ParamName] = Value;
				break;
			}
			case sizeof(float) * 3: // 12諛붿씠??- Vector3
			{
				FVector Value;
				Material->GetVector3Parameter(ParamName, Value);
				JsonData[MatKeys::Parameters][ParamName] = json::Array(Value.X, Value.Y, Value.Z);
				break;
			}
			case sizeof(float) * 4: // 16諛붿씠??- Vector4
			{
				FVector4 Value;
				Material->GetVector4Parameter(ParamName, Value);
				JsonData[MatKeys::Parameters][ParamName] = json::Array(Value.X, Value.Y, Value.Z, Value.W);
				break;
			}
			case sizeof(float) * 16: // 64諛붿씠??- Matrix
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
				break; // uint, bool ???뱀닔 耳?댁뒪??蹂꾨룄 泥섎━ ?꾩슂
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
	// 1. ?쒗뵆由우씠 罹먯떆???덈뒗吏 ?뺤씤 (?곗씠??寃쎈줈瑜??ㅺ컪?쇰줈 ?ъ슜)
	auto It = TemplateCache.find(ShaderPath);
	if (It != TemplateCache.end())
	{
		return It->second;
	}

	// 2. ?쒗뵆由우씠 湲곗〈???녿떎硫??덈줈 ?쒖옉
	//    罹먯떆???덉쑝硫?諛섑솚, ?놁쑝硫?而댄뙆????罹먯떛
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
	// 1. TemplateCache 硫붾え由??댁젣
	// GetOrCreateTemplate()?먯꽌 new FMaterialTemplate()濡?吏곸젒 ?좊떦?덉쑝誘濡??ш린??delete ?댁쨳?덈떎.
	for (auto& Pair : TemplateCache)
	{
		if (Pair.second != nullptr)
		{
			delete Pair.second;
			Pair.second = nullptr;
		}
	}
	TemplateCache.clear();

	// 2. MaterialCache ??UMaterial? UObjectManager媛 ?섎챸??愿由ы븯誘濡?罹먯떆 留듬쭔 鍮꾩?
	MaterialCache.clear();

	// 3. Device 李몄“ ?댁젣
	// ?몃??먯꽌 二쇱엯諛쏆? 由ъ냼?ㅼ씠誘濡??ъ씤?곕쭔 珥덇린?뷀빀?덈떎.
	Device = nullptr;
}
