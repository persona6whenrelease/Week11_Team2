#pragma once

#include "Component/Movement/MovementComponent.h"
#include "Math/Vector.h"
#include "PawnMovementComponent.generated.h"

class FArchive;

UCLASS()
class UPawnMovementComponent : public UMovementComponent
{
public:
	GENERATED_BODY()

	UPawnMovementComponent();
	~UPawnMovementComponent() override = default;

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void Serialize(FArchive& Ar) override;

	void AddMovementInput(const FVector& Direction, float Scale = 1.0f);
	FVector ConsumeMovementInputVector();
	FVector GetPendingMovementInputVector() const { return PendingMovementInput; }
	void ApplyPendingMovement(float DeltaTime = 0.0f);
	bool ApplyControllerMovementInput(const FControllerMovementInput& Input) override;

private:
	FVector PendingMovementInput = FVector::ZeroVector;
};
