#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Component/PrimitiveComponent.h"
#include "GameFramework/AActor.h"
#include "Render/Shader/ShaderManager.h"
#include "Asset/Material/Material.h"
#include "Object/ObjectFactory.h"

// ============================================================
// FPrimitiveSceneProxy ??湲곕낯 援ы쁽
// ============================================================
FPrimitiveSceneProxy::FPrimitiveSceneProxy(UPrimitiveComponent* InComponent)
	: Owner(InComponent)
{
	if (!Owner->SupportsOutline())
		ProxyFlags &= ~EPrimitiveProxyFlags::SupportsOutline;
}

FPrimitiveSceneProxy::~FPrimitiveSceneProxy()
{
	if (DefaultMaterial)
	{
		UObjectManager::Get().DestroyObject(DefaultMaterial);
		DefaultMaterial = nullptr;
	}
}

ERenderPass FPrimitiveSceneProxy::GetRenderPass() const
{
	if (!SectionDraws.empty() && SectionDraws[0].Material)
		return SectionDraws[0].Material->GetRenderPass();
	return ERenderPass::Opaque;
}

FShader* FPrimitiveSceneProxy::GetShader() const
{
	if (!SectionDraws.empty() && SectionDraws[0].Material)
		return SectionDraws[0].Material->GetShader();
	return nullptr;
}

void FPrimitiveSceneProxy::UpdateTransform()
{
	PerObjectConstants = FPerObjectConstants::FromWorldMatrix(Owner->GetWorldMatrix());
	CachedWorldPos = PerObjectConstants.Model.GetLocation();
	CachedBounds = Owner->GetWorldBoundingBox();
	LastLODUpdateFrame = UINT32_MAX;
	MarkPerObjectCBDirty();
}

void FPrimitiveSceneProxy::UpdateMaterial()
{
	// 湲곕낯 PrimitiveComponent???뱀뀡蹂?癒명떚由ъ뼹???놁쓬 ???쒕툕?대옒?ㅼ뿉???ㅻ쾭?쇱씠??
}

void FPrimitiveSceneProxy::UpdateVisibility()
{
	bVisible = Owner->IsVisible();
	if (bVisible)
	{
		AActor* OwnerActor = Owner->GetOwner();
		if (OwnerActor && !OwnerActor->IsVisible())
			bVisible = false;
	}
	bCastShadow = Owner->GetCastShadow();
	bCastShadowAsTwoSided = Owner->GetCastShadowAsTwoSided();
}

void FPrimitiveSceneProxy::UpdateMesh()
{
	MeshBuffer = Owner->GetMeshBuffer();

	if (!DefaultMaterial)
	{
		DefaultMaterial = UMaterial::CreateTransient(
			ERenderPass::Opaque, EBlendState::Opaque,
			EDepthStencilState::Default, ERasterizerState::SolidBackCull,
			FShaderManager::Get().GetOrCreate(EShaderPath::Primitive));
	}

	SectionDraws.clear();
	if (MeshBuffer && DefaultMaterial)
	{
		uint32 IdxCount = MeshBuffer->GetIndexBuffer().GetIndexCount();
		SectionDraws.push_back({ DefaultMaterial, 0, IdxCount });
	}
}

