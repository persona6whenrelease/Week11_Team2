/**
 * 텍스처 로드 캐시와 D3D11 리소스 생명주기를 구현한다.
 *
 * 패키지 경로를 실제 디스크 경로로 해석한 뒤 WICTextureLoader로 SRV를 생성한다. 로드된 텍스처는 경로를
 * 키로 캐시되고, 메모리 통계에는 생성된 D3D11 texture 크기를 반영한다.
 */

#include "Asset/Texture/Texture2D.h"
#include "Object/ObjectFactory.h"
#include "Core/Log.h"
#include "Platform/Paths.h"
#include "WICTextureLoader.h"

#include <d3d11.h>
#include <filesystem>

IMPLEMENT_CLASS(UTexture2D, UObject)

std::map<FString, UTexture2D*> UTexture2D::TextureCache;

UTexture2D::~UTexture2D()
{
	if (SRV)
	{
		if (TrackedTextureMemory > 0)
		{
			MemoryStats::SubTextureMemory(TrackedTextureMemory);
			TrackedTextureMemory = 0;
		}

		SRV->Release();
		SRV = nullptr;
	}

	
	auto It = TextureCache.find(SourceFilePath);
	if (It != TextureCache.end() && It->second == this)
	{
		TextureCache.erase(It);
	}
}

void UTexture2D::ReleaseAllGPU()
{
	for (auto& [Path, Texture] : TextureCache)
	{
		if (Texture && Texture->SRV)
		{
			if (Texture->TrackedTextureMemory > 0)
			{
				MemoryStats::SubTextureMemory(Texture->TrackedTextureMemory);
				Texture->TrackedTextureMemory = 0;
			}
			Texture->SRV->Release();
			Texture->SRV = nullptr;
		}
	}
	TextureCache.clear();
}

UTexture2D* UTexture2D::LoadFromFile(const FString& FilePath, ID3D11Device* Device)
{
	if (FilePath.empty() || !Device) return nullptr;

	
	auto It = TextureCache.find(FilePath);
	if (It != TextureCache.end())
	{
		return It->second;
	}

	
	UTexture2D* Texture = UObjectManager::Get().CreateObject<UTexture2D>();
	if (!Texture->LoadInternal(FilePath, Device))
	{
		UObjectManager::Get().DestroyObject(Texture);
		return nullptr;
	}

	TextureCache[FilePath] = Texture;
	return Texture;
}

UTexture2D* UTexture2D::LoadFromCached(const FString& FilePath)
{
	if (FilePath.empty()) return nullptr;

	auto It = TextureCache.find(FilePath);
	if (It != TextureCache.end())
	{
		return It->second;
	}

	return nullptr;
}

bool UTexture2D::LoadInternal(const FString& FilePath, ID3D11Device* Device)
{
	std::wstring WidePath;
	FString Error;
	if (!FPaths::TryResolvePackagePath(FilePath, WidePath, &Error))
	{
		UE_LOG("Invalid texture path: %s", Error.c_str());
		return false;
	}

	ID3D11Resource* Resource = nullptr;
	HRESULT hr = DirectX::CreateWICTextureFromFileEx(
		Device, WidePath.c_str(),
		0,                                    
		D3D11_USAGE_DEFAULT,                  
		D3D11_BIND_SHADER_RESOURCE,           
		0,                                    
		0,                                    
		DirectX::WIC_LOADER_IGNORE_SRGB,     
		&Resource, &SRV);

	if (FAILED(hr))
	{
		UE_LOG("Failed to load texture: %s", FilePath.c_str());
		return false;
	}

	
	if (Resource)
	{
		TrackedTextureMemory = MemoryStats::CalculateTextureMemory(Resource);

		ID3D11Texture2D* Tex2D = nullptr;
		if (SUCCEEDED(Resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&Tex2D)))
		{
			D3D11_TEXTURE2D_DESC Desc;
			Tex2D->GetDesc(&Desc);
			Width = Desc.Width;
			Height = Desc.Height;
			Tex2D->Release();
		}

		if (TrackedTextureMemory > 0)
		{
			MemoryStats::AddTextureMemory(TrackedTextureMemory);
		}
		Resource->Release();
	}

	SourceFilePath = FilePath;
	return true;
}
