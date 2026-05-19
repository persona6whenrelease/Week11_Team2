#pragma once

#include "GameFramework/AActor.h"
#include "PointLightActor.generated.h"

class UBillboardComponent;
class UPointLightComponent;

UCLASS()
class APointLightActor : public AActor
{
public:
	GENERATED_BODY()

	void InitDefaultComponents();

private:
	UPointLightComponent* LightComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};
