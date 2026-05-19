#pragma once
#include "Component/Light/LightComponentBase.h"
#include "LightComponent.generated.h"

UCLASS()
class ULightComponent : public ULightComponentBase
{
public:
	GENERATED_BODY()

	virtual void Serialize(FArchive& Ar) override;

	float GetShadowResolutionScale() const { return ShadowResolutionScale; }
	float GetShadowBias() const { return ShadowBias; }
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	float GetShadowNormalBias() const { return ShadowNormalBias; } 
	float GetShadowSharpen() const { return ShadowSharpen; }

protected:
	FPROPERTY(DisplayName="Shadow Resolution Scale", Type=Float, min=0.1f, max=4.0f, speed=0.1f)
	float ShadowResolutionScale = 1.0f;
	FPROPERTY(DisplayName="Shadow Bias", Type=Float, min=-0.2f, max=0.2f, speed=0.0001f)
	float ShadowBias = -0.0001f;
	FPROPERTY(DisplayName="Shadow Slope Bias", Type=Float, min=-0.2f, max=0.2f, speed=0.0001f)
	float ShadowSlopeBias = 0.0001f;
	FPROPERTY(DisplayName="Shadow Normal Bias", Type=Float, min=-0.2f, max=0.2f, speed=0.0001f)
	float ShadowNormalBias = -0.0020f;
	FPROPERTY(DisplayName="Shadow Sharpen", Type=Float, min=0.0f, max=1.0f, speed=0.05f)
	float ShadowSharpen = 0.67f;
};
