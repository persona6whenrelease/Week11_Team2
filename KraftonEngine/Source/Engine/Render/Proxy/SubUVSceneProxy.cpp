#include "Render/Proxy/SubUVSceneProxy.h"
#include "Component/SubUVComponent.h"
#include "Render/Types/FrameContext.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Asset/Material/Material.h"

// ============================================================
// FSubUVSceneProxy
// ============================================================
FSubUVSceneProxy::FSubUVSceneProxy(USubUVComponent* InComponent)
	: FBillboardSceneProxy(static_cast<UBillboardComponent*>(InComponent))
{
	ProxyFlags &= ~EPrimitiveProxyFlags::ShowAABB;
}

FSubUVSceneProxy::~FSubUVSceneProxy()
{
	UVRegionCB.Release();
}

void FSubUVSceneProxy::UpdateMesh()
{
	USubUVComponent* Comp = GetSubUVComponent();

	// TexturedQuad (FVertexPNCT with UVs) for rendering
	MeshBuffer = &FMeshBufferManager::Get().GetMeshBuffer(EMeshShape::TexturedQuad);

	UMaterial* SubUVMat = Comp->GetSubUVMaterial();

	// UV region CB瑜?Material??諛붿씤??(b2 ?щ’)
	if (SubUVMat)
		SubUVMat->BindPerShaderCB<FSubUVRegionConstants>(&UVRegionCB, ECBSlot::PerShader0);

	// Particle/FrameIndex 罹먯떛
	CachedParticle = Comp->GetParticle();
	CachedFrameIndex = Comp->GetFrameIndex();

	// SectionDraws ?⑥씪 ??ぉ ??SubUVMaterial濡?Particle SRV 諛붿씤??
	SectionDraws.clear();
	if (SubUVMat)
	{
		const uint32 IdxCount = MeshBuffer->GetIndexBuffer().GetIndexCount();
		SectionDraws.push_back({ SubUVMat, 0, IdxCount });
	}
}

void FSubUVSceneProxy::UpdateMaterial()
{
	// TickComponent?먯꽌 FrameIndex 蹂寃???Material dirty濡??몄텧??
	USubUVComponent* Comp = GetSubUVComponent();
	CachedFrameIndex = Comp->GetFrameIndex();
	CachedParticle = Comp->GetParticle();

	// SectionDraws 媛깆떊 ??SubUVMaterial??CachedSRV??Component媛 愿由?
	SectionDraws.clear();
	UMaterial* SubUVMat = Comp->GetSubUVMaterial();
	if (SubUVMat)
	{
		const uint32 IdxCount = MeshBuffer ? MeshBuffer->GetIndexBuffer().GetIndexCount() : 0;
		SectionDraws.push_back({ SubUVMat, 0, IdxCount });
	}
}

void FSubUVSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	if (!bVisible) return;

	if (!CachedParticle || !CachedParticle->IsLoaded())
	{
		bVisible = false;
		return;
	}

	// Billboard matrix
	FVector BillboardForward = Frame.CameraForward * -1.0f;
	FMatrix RotMatrix;
	RotMatrix.SetAxes(BillboardForward, Frame.CameraRight, Frame.CameraUp);
	FMatrix BillboardMatrix = FMatrix::MakeScaleMatrix(CachedScale)
		* RotMatrix * FMatrix::MakeTranslationMatrix(CachedLocation);

	PerObjectConstants = FPerObjectConstants::FromWorldMatrix(BillboardMatrix);
	MarkPerObjectCBDirty();

	// Update UV region from cached frame index
	const uint32 Cols = CachedParticle->Columns;
	const uint32 Rows = CachedParticle->Rows;
	if (Cols > 0 && Rows > 0)
	{
		const float FrameW = 1.0f / static_cast<float>(Cols);
		const float FrameH = 1.0f / static_cast<float>(Rows);
		const uint32 Col = CachedFrameIndex % Cols;
		const uint32 Row = CachedFrameIndex / Cols;

		UMaterial* SubUVMat = SectionDraws.empty() ? nullptr : SectionDraws[0].Material;
		if (!SubUVMat) return;
		FSubUVRegionConstants& Region = SubUVMat->GetPerShaderAs<FSubUVRegionConstants>();
		Region.U = Col * FrameW;
		Region.V = Row * FrameH;
		Region.Width = FrameW;
		Region.Height = FrameH;
	}
}

USubUVComponent* FSubUVSceneProxy::GetSubUVComponent() const
{
	return static_cast<USubUVComponent*>(GetOwner());
}
