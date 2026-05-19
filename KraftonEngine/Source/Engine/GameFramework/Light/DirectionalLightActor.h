#pragma once
#include "GameFramework/AActor.h"
#include "DirectionalLightActor.generated.h"

class UBillboardComponent;
class UDirectionalLightComponent;

UCLASS()
class ADirectionalLightActor : public AActor
{
public:
	GENERATED_BODY()

	void InitDefaultComponents();

private:
	UDirectionalLightComponent* LightComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};
