#pragma once
#include "Component/Light/LightComponent.h"
#include "PointLightComponent.generated.h"

UCLASS()
class UPointLightComponent : public ULightComponent
{
public:
	GENERATED_BODY()
	virtual ELightComponentType GetLightType() const override { return ELightComponentType::Point; }
	virtual void ContributeSelectedVisuals(FScene& Scene) const override;
	virtual void PushToScene() override;
	virtual void DestroyFromScene() override;
	virtual void Serialize(FArchive& Ar) override;
	virtual bool GetLightViewProj(FLightViewProjResult& OutResult, const UCameraComponent* Camera, int32 FaceIndex) const override;

	float GetAttenuationRadius() const { return AttenuationRadius; }

protected:
	FPROPERTY(Type=Float, min=0.05f, max=1000.f, speed=0.01f)
	float AttenuationRadius = 1.f;
	FPROPERTY(Type=Float, min=0.05f, max=10.f, speed=0.01f)
	float LightFalloffExponent = 1.f;
};
