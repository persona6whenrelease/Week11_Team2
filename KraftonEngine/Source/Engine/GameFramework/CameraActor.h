#pragma once

#include "GameFramework/AActor.h"
#include "CameraActor.generated.h"

class UCameraComponent;

UCLASS()
class ACameraActor : public AActor
{
public:
	GENERATED_BODY()

	ACameraActor() = default;
	~ACameraActor() override = default;

	void InitDefaultComponents() override;
	UCameraComponent* GetCameraComponent() const { return CameraComponent; }

private:
	UCameraComponent* CameraComponent = nullptr;
};
