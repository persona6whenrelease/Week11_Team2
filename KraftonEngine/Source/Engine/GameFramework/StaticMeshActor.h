#pragma once

#include "GameFramework/AActor.h"
#include "StaticMeshActor.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class USubUVComponent;
class UBoxComponent;

UCLASS()
class AStaticMeshActor : public AActor
{
public:
	GENERATED_BODY()
	AStaticMeshActor() {}

	//void InitDefaultComponents(const FString& UStaticMeshFileName = "Data/BasicShape/Cylinder.obj");
	void InitDefaultComponents(const FString& UStaticMeshFileName = "Data/FireEngine/Fire_Engine.obj");


private:
	UStaticMeshComponent* StaticMeshComponent = nullptr;
	
	UBoxComponent* BoxComponent = nullptr;
	//UTextRenderComponent* TextRenderComponent = nullptr;
	//USubUVComponent* SubUVComponent = nullptr;
};
