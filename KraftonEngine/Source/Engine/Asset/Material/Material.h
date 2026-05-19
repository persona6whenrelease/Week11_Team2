/**
 * 엔진 머티리얼 템플릿, 파라미터, 상수 버퍼, 인스턴스 객체를 선언한다.
 *
 * 머티리얼은 셰이더, 렌더 상태, 스칼라/벡터/행렬/텍스처 파라미터를 하나로 묶어 메시 섹션 렌더링에
 * 필요한 상태를 제공한다. JSON으로 저장된 머티리얼 파일은 MaterialManager를 거쳐 UMaterial 인스턴스로
 * 복원된다.
 */

#pragma once

#include "Object/ObjectFactory.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Render/Types/RenderTypes.h"
#include "Render/Types/RenderStateTypes.h"
#include "Render/Resource/Buffer.h"
#include "Render/Types/MaterialTextureSlot.h"
#include "Render/Types/RenderConstants.h"
#include <memory>
#include "Material.generated.h"

class UTexture2D;
class FArchive;
class FShader;

/**
 * 머티리얼 파라미터 하나의 이름, 타입, 기본값, 바인딩 위치를 설명한다.
 */
struct FMaterialParameterInfo
{
	FString BufferName;  
	uint32 SlotIndex;    

	uint32 Offset;      
	uint32 Size;        

	uint32 BufferSize;   
};

/**
 * 머티리얼 인스턴스들이 공유하는 셰이더와 렌더 상태, 파라미터 레이아웃을 저장한다.
 */
class FMaterialTemplate
{
private:
	uint32 MaterialTemplateID; 
	FShader* Shader; 
	TMap<FString, FMaterialParameterInfo*> ParameterLayout; 

public:
	const TMap<FString, FMaterialParameterInfo*>& GetParameterInfo() const { return ParameterLayout; }
	void Create(FShader* InShader);

	FShader* GetShader() const { return Shader; }
	bool GetParameterInfo(const FString& Name, FMaterialParameterInfo& OutInfo) const;
};

/**
 * 머티리얼 파라미터를 GPU 상수 버퍼로 업로드하기 위한 래퍼이다.
 */
struct FMaterialConstantBuffer
{
	uint8* CPUData;   
	FConstantBuffer GPUBuffer;
	uint32 Size = 0;
	UINT SlotIndex = 0;	
	bool bDirty = false;

	FMaterialConstantBuffer() = default;
	~FMaterialConstantBuffer();

	FMaterialConstantBuffer(const FMaterialConstantBuffer&) = delete;
	FMaterialConstantBuffer& operator=(const FMaterialConstantBuffer&) = delete;

	void Init(ID3D11Device* InDevice, uint32 InSize, uint32 InSlot);
	void SetData(const void* Data, uint32 InSize, uint32 Offset = 0);
	void Upload(ID3D11DeviceContext* DeviceContext);
	void Release();

	FConstantBuffer* GetConstantBuffer() { return &GPUBuffer; }
};

/**
 * 렌더링에 사용할 머티리얼 인스턴스이다.
 *
 * 템플릿이 제공하는 셰이더/렌더 상태에 개별 파라미터와 텍스처 값을 더해 메시 섹션 단위로 바인딩된다.
 */
UCLASS()
class UMaterial : public UObject
{
private:
	FString PathFileName;
	uint32 MaterialInstanceID; 
	FMaterialTemplate* Template; 

	
	ERenderPass RenderPass = ERenderPass::Opaque;
	EBlendState BlendState = EBlendState::Opaque;
	EDepthStencilState DepthStencilState = EDepthStencilState::Default;
	ERasterizerState RasterizerState = ERasterizerState::SolidBackCull;

	TMap<FString, std::unique_ptr<FMaterialConstantBuffer>> ConstantBufferMap; 
	TMap<FString, UTexture2D*> TextureParameters;  

	FShader* TransientShader = nullptr; 

	
	FConstantBufferBinding PerShaderOverride;

	
	ID3D11ShaderResourceView* CachedSRVs[(int)EMaterialTextureSlot::Max] = {};

	bool SetParameter(const FString& Name, const void* Data, uint32 Size);

public:
	GENERATED_BODY()
	~UMaterial() override;

	void Create(const FString& InPathFileName, FMaterialTemplate* InTemplate,
		ERenderPass InRenderPass,
		EBlendState InBlend,
		EDepthStencilState InDepth,
		ERasterizerState InRaster,
		TMap<FString, std::unique_ptr<FMaterialConstantBuffer>>&& InBuffers);

	const uint8* GetRawPtr(const FString& BufferName, uint32 Offset) const;

	const TMap<FString, FMaterialParameterInfo*> GetParameterInfo() const { return Template->GetParameterInfo(); }

	bool SetScalarParameter(const FString& ParamName, float Value);
	bool SetVector3Parameter(const FString& ParamName, const FVector& Value);
	bool SetVector4Parameter(const FString& ParamName, const FVector4& Value);
	bool SetTextureParameter(const FString& ParamName, UTexture2D* Texture);
	bool SetMatrixParameter(const FString& ParamName, const FMatrix& Value);

	bool GetScalarParameter(const FString& ParamName, float& OutValue) const;
	bool GetVector3Parameter(const FString& ParamName, FVector& OutValue) const;
	bool GetVector4Parameter(const FString& ParamName, FVector4& OutValue) const;
	bool GetTextureParameter(const FString& ParamName, UTexture2D*& OutTexture) const;
	bool GetMatrixParameter(const FString& ParamName, FMatrix& Value) const;

	TMap<FString, UTexture2D*>* GetTexture() { return &TextureParameters; }

	void Bind(ID3D11DeviceContext* Context);

	FShader* GetShader() const { return Template ? Template->GetShader() : TransientShader; }
	ERenderPass GetRenderPass() const { return RenderPass; }
	EBlendState GetBlendState() const { return BlendState; }
	EDepthStencilState GetDepthStencilState() const { return DepthStencilState; }
	ERasterizerState GetRasterizerState() const { return RasterizerState; }

	
	template<typename T>
	T& BindPerShaderCB(FConstantBuffer* Buffer, uint32 Slot)
	{
		return PerShaderOverride.Bind<T>(Buffer, Slot);
	}

	template<typename T>
	T& GetPerShaderAs() { return PerShaderOverride.As<T>(); }

	template<typename T>
	const T& GetPerShaderAs() const { return PerShaderOverride.As<T>(); }

	const FString& GetTexturePathFileName(const FString& TextureName)const;

	const FString& GetAssetPathFileName() const { return PathFileName; }
	void SetAssetPathFileName(const FString& InPath) { PathFileName = InPath; }
	/**
	 * 에셋 헤더 검증과 본문 데이터 저장/로드를 함께 처리한다.
	 */
	void Serialize(FArchive& Ar);

	FConstantBuffer* GetGPUBufferBySlot(uint32 InSlot) const
	{
		
		if (PerShaderOverride.Buffer && PerShaderOverride.Slot == InSlot)
			return PerShaderOverride.Buffer;

		for (const auto& Pair : ConstantBufferMap)
		{
			if (Pair.second->SlotIndex == InSlot)
				return Pair.second->GetConstantBuffer();
		}
		return nullptr;
	}

	
	void FlushDirtyBuffers(ID3D11Device* Device, ID3D11DeviceContext* Ctx)
	{
		for (auto& Pair : ConstantBufferMap)
		{
			if (Pair.second->bDirty)
				Pair.second->Upload(Ctx);
		}
		
		if (PerShaderOverride.Buffer)
		{
			if (!PerShaderOverride.Buffer->GetBuffer())
				PerShaderOverride.Buffer->Create(Device, PerShaderOverride.Size);
			PerShaderOverride.Buffer->Update(Ctx, PerShaderOverride.Data, PerShaderOverride.Size);
		}
	}

	
	const ID3D11ShaderResourceView* const* GetCachedSRVs() const { return CachedSRVs; }

	
	void RebuildCachedSRVs();

	
	void SetCachedSRV(EMaterialTextureSlot Slot, ID3D11ShaderResourceView* SRV) { CachedSRVs[(int)Slot] = SRV; }

	
	
	static UMaterial* CreateTransient(ERenderPass InPass, EBlendState InBlend,
		EDepthStencilState InDepth = EDepthStencilState::Default,
		ERasterizerState InRaster = ERasterizerState::SolidBackCull,
		FShader* InShader = nullptr);
};
