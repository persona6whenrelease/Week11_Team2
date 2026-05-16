#pragma once

#include "Core/Singleton.h"
#include "Core/CoreTypes.h"
#include "Render/Types/RenderTypes.h"
#include "SimpleJSON/json.hpp"
#include <memory>

#include "Render/Types/RenderStateTypes.h"

namespace MatKeys
{
	static constexpr const char* PathFileName = "PathFileName";
	static constexpr const char* ShaderPath = "ShaderPath";
	static constexpr const char* RenderPass = "RenderPass";
	static constexpr const char* BlendState = "BlendState";
	static constexpr const char* DepthStencilState = "DepthStencilState";
	static constexpr const char* RasterizerState = "RasterizerState";
	static constexpr const char* Parameters = "Parameters";
	static constexpr const char* Textures = "Textures";
}

class FMaterialTemplate;
class UMaterial;
struct FMaterialConstantBuffer;

/**
 * 에디터 목록에 표시할 머티리얼 파일의 이름과 경로를 담는 구조이다.
 */
struct FMaterialAssetListItem
{
	FString DisplayName;
	FString FullPath;
};

/**
 * 머티리얼 JSON 파일을 읽어 템플릿과 UMaterial 인스턴스로 복원하는 매니저이다.
 */
class FMaterialManager : public TSingleton<FMaterialManager>
{
	friend class TSingleton<FMaterialManager>;

    TMap<FString, FMaterialTemplate*> TemplateCache;    
	TMap<FString, UMaterial*> MaterialCache;	
	TArray<FMaterialAssetListItem> AvailableMaterialFiles;

	ID3D11Device* Device = nullptr;

public:
	~FMaterialManager(); 

	void Initialize(ID3D11Device* InDevice) { Device = InDevice; }

	
	/**
	 * 프로젝트 머티리얼 파일을 스캔하고 필요한 템플릿/인스턴스를 준비한다.
	 */
	void LoadAllMaterials(ID3D11Device* Device);

    
	UMaterial* GetOrCreateMaterial(const FString& MatFilePath);

	/**
	 * 프로젝트 경로를 순회해 에디터에 표시할 에셋 후보를 수집한다.
	 */
	void ScanMaterialAssets();
	const TArray<FMaterialAssetListItem>& GetAvailableMaterialFiles() const { return AvailableMaterialFiles; }

	void Release();
private:
	
	FMaterialTemplate* GetOrCreateTemplate(const FString& ShaderPath);

	/**
	 * 원본 포맷에서 필요한 속성 값을 읽어 엔진 타입으로 변환한다.
	 */
	json::JSON ReadJsonFile(const FString& FilePath) const;

	TMap<FString, std::unique_ptr<FMaterialConstantBuffer>> CreateConstantBuffers(FMaterialTemplate* Template);

	/**
	 * JSON에서 읽은 스칼라/벡터/행렬 값을 머티리얼 인스턴스에 적용한다.
	 */
	void ApplyParameters(UMaterial* Material, json::JSON& JsonData);
	/**
	 * JSON에서 읽은 텍스처 경로를 UTexture2D로 로드해 머티리얼에 연결한다.
	 */
	void ApplyTextures(UMaterial* Material, json::JSON& JsonData);

	ERenderPass StringToRenderPass(const FString& Str) const;
	EBlendState StringToBlendState(const FString& Str, ERenderPass Pass) const;
	EDepthStencilState StringToDepthStencilState(const FString& Str, ERenderPass Pass) const;
	ERasterizerState StringToRasterizerState(const FString& Str, ERenderPass Pass) const;

	/**
	 * 현재 머티리얼 값을 에디터에서 다시 열 수 있는 JSON 파일로 저장한다.
	 */
	void SaveToJSON(json::JSON& JsonData, const FString& MatFilePath);
	
	/**
	 * 템플릿에 필요한 파라미터가 파일에 없을 때 기본값을 보충한다.
	 */
	bool InjectDefaultParameters(json::JSON& JsonData, FMaterialTemplate* Template, UMaterial* Material);
	/**
	 * 현재 템플릿에서 더 이상 사용하지 않는 저장 파라미터를 제거한다.
	 */
	bool PurgeStaleParameters(json::JSON& JsonData, FMaterialTemplate* Template);
	
	const FString DefaultShaderPath = "Shaders/Geometry/UberLit.hlsl";


};