#pragma once

#include "SceneComponent.h"
#include "Render/Types/FogParams.h"
#include "HeightFogComponent.generated.h"

UCLASS()
class UHeightFogComponent : public USceneComponent
{
public:
	GENERATED_BODY()

	UHeightFogComponent();

	void CreateRenderState() override;
	void DestroyRenderState() override;

	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	void Serialize(FArchive& Ar) override;

	// Transform 변경 시 FogBaseHeight 갱신
	void OnTransformDirty() override;

	class UBillboardComponent* EnsureEditorBillboard();

private:
	void PushToScene();

	FPROPERTY(DisplayName="Fog Density", Type=Float, min=0.0f, max=0.05f, speed=0.001f)
	float FogDensity        = 0.02f;
	FPROPERTY(DisplayName="Height Falloff", Type=Float, min=0.001f, max=5.0f, speed=0.01f)
	float FogHeightFalloff  = 0.2f;
	FPROPERTY(DisplayName="Start Distance", Type=Float, min=0.0f, max=100000.0f, speed=1.0f)
	float StartDistance     = 0.0f;
	FPROPERTY(DisplayName="Cutoff Distance", Type=Float, min=0.0f, max=100000.0f, speed=1.0f)
	float FogCutoffDistance = 0.0f;
	FPROPERTY(DisplayName="Max Opacity", Type=Float, min=0.0f, max=1.0f, speed=0.01f)
	float FogMaxOpacity     = 1.0f;
	FPROPERTY(DisplayName="Inscattering Color", Type=Color4)
	FVector4 FogInscatteringColor = FVector4(0.45f, 0.55f, 0.65f, 1.0f);
};
