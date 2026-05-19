#pragma once
#include "Component/Light/PointLightComponent.h"
#include "SpotLightComponent.generated.h"

UCLASS()
class USpotLightComponent : public UPointLightComponent
{
public:
	GENERATED_BODY()
	virtual ELightComponentType GetLightType() const override { return ELightComponentType::Spot; }
	virtual void ContributeSelectedVisuals(FScene& Scene) const override;
	virtual void PushToScene() override;
	virtual void DestroyFromScene() override;
	virtual void Serialize(FArchive& Ar) override;
	virtual bool GetLightViewProj(FLightViewProjResult& OutResult, const UCameraComponent* Camera = nullptr, int32 FaceIndex = 0) const override;

	float GetOuterConeAngle() const { return OuterConeAngle; }

protected:
	float InnerConeAngle = 20.0f;	// Inner Cone Angle in degrees
	float OuterConeAngle = 40.0f;	// Outer Cone Angle in degrees
};
