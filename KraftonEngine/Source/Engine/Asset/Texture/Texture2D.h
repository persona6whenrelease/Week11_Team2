#pragma once

#include "Object/Object.h"
#include "Core/CoreTypes.h"

#include <map>
#include <string>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

/**
 * 이미지 파일을 D3D11 SRV로 로드하고 경로 기반 캐시로 공유하는 텍스처 에셋 타입이다.
 */
class UTexture2D : public UObject
{
public:
	DECLARE_CLASS(UTexture2D, UObject)

	UTexture2D() = default;
	~UTexture2D() override;

	
	/**
	 * 경로 기반 캐시를 확인한 뒤 필요하면 이미지 파일을 GPU 텍스처로 로드한다.
	 */
	static UTexture2D* LoadFromFile(const FString& FilePath, ID3D11Device* Device);
	/**
	 * 이미 로드된 텍스처가 캐시에 있는지 조회한다.
	 */
	static UTexture2D* LoadFromCached(const FString& FilePath);

	
	/**
	 * 캐시에 남아 있는 모든 텍스처의 GPU 리소스를 해제한다.
	 */
	static void ReleaseAllGPU();

	ID3D11ShaderResourceView* GetSRV() const { return SRV; }
	const FString& GetSourcePath() const { return SourceFilePath; }
	uint32 GetWidth() const { return Width; }
	uint32 GetHeight() const { return Height; }
	bool IsLoaded() const { return SRV != nullptr; }

private:
	/**
	 * 패키지 경로를 실제 파일 경로로 해석하고 WIC 로더로 SRV를 생성한다.
	 */
	bool LoadInternal(const FString& FilePath, ID3D11Device* Device);

	FString SourceFilePath;
	ID3D11ShaderResourceView* SRV = nullptr;
	uint32 Width = 0;
	uint32 Height = 0;
	uint64 TrackedTextureMemory = 0;

	
	static std::map<FString, UTexture2D*> TextureCache;
};
