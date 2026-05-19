#pragma once

#include "GameFramework/AActor.h"
#include "AmbientLightActor.generated.h"

class UAmbientLightComponent;
class UBillboardComponent;

UCLASS()
class AAmbientLightActor : public AActor
{
public:
	GENERATED_BODY()

	void InitDefaultComponents();

private:
	UAmbientLightComponent* LightComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};
