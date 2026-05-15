#include "Render/Proxy/BillboardSceneProxy.h"
#include "Component/BillboardComponent.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Render/Types/FrameContext.h"
#include "GameFramework/AActor.h"
#include "Asset/Material/Material.h"
#include "Asset/Texture/Texture2D.h"

// ============================================================
// FBillboardSceneProxy
// ============================================================
FBillboardSceneProxy::FBillboardSceneProxy(UBillboardComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate;
	ProxyFlags &= ~EPrimitiveProxyFlags::ShowAABB;

	if (InComponent->IsEditorOnly())
		ProxyFlags |= EPrimitiveProxyFlags::EditorOnly;
}

UBillboardComponent* FBillboardSceneProxy::GetBillboardComponent() const
{
	return static_cast<UBillboardComponent*>(GetOwner());
}

// ============================================================
// UpdateTransform ??Scale/Location 罹먯떛
// ============================================================
void FBillboardSceneProxy::UpdateTransform()
{
	FPrimitiveSceneProxy::UpdateTransform();
	UBillboardComponent* Comp = GetBillboardComponent();
	CachedScale = Comp->GetWorldScale();
	CachedLocation = Comp->GetWorldLocation();
}

// ============================================================
// UpdateMesh ??TexturedQuad + Material shader/states
// ============================================================
void FBillboardSceneProxy::UpdateMesh()
{
	UBillboardComponent* Comp = GetBillboardComponent();
	UMaterial* Mat = Comp ? Comp->GetMaterial() : nullptr;

	if (Mat)
	{
		// TexturedQuad (FVertexPNCT with UVs)
		MeshBuffer = &FMeshBufferManager::Get().GetMeshBuffer(EMeshShape::TexturedQuad);

		// SectionDraws ?⑥씪 ??ぉ ??Material??CachedSRVs濡??띿뒪泥?諛붿씤??
		const uint32 IndexCount = MeshBuffer->GetIndexBuffer().GetIndexCount();
		SectionDraws.clear();
		SectionDraws.push_back({ Mat, 0, IndexCount });
	}
	else
	{
		MeshBuffer = GetOwner()->GetMeshBuffer();
		SectionDraws.clear();
	}
}

// ============================================================
// UpdatePerViewport ??酉고룷??移대찓??湲곕컲 鍮뚮낫???됰젹 媛깆떊
// ============================================================
void FBillboardSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	if (!bVisible) return;

	// Frame 移대찓??踰≫꽣濡?per-view 鍮뚮낫???됰젹 怨꾩궛
	FVector BillboardForward = Frame.CameraForward * -1.0f;
	FMatrix RotMatrix;
	RotMatrix.SetAxes(BillboardForward, Frame.CameraRight, Frame.CameraUp);
	FMatrix BillboardMatrix = FMatrix::MakeScaleMatrix(CachedScale)
		* RotMatrix * FMatrix::MakeTranslationMatrix(CachedLocation);

	PerObjectConstants = FPerObjectConstants::FromWorldMatrix(BillboardMatrix);
	MarkPerObjectCBDirty();
}
