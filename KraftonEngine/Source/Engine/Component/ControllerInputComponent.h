#pragma once

#include "Component/ActorComponent.h"
#include "Math/Rotator.h"
#include "ControllerInputComponent.generated.h"

class APlayerController;
class UCameraComponent;
struct FInputFrame;
class FArchive;

enum class EControllerMovementFrame : int32
{
	World = 0,
	ControlRotation = 1,
	ViewCamera = 2,
};


UCLASS()
class UControllerInputComponent : public UActorComponent
{
public:
	GENERATED_BODY()

	UControllerInputComponent() = default;
	~UControllerInputComponent() override = default;

	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	bool ApplyInput(APlayerController* Controller, UCameraComponent* FallbackCamera, float DeltaTime, FInputFrame& InputFrame);
	bool ApplyMovementInput(APlayerController* Controller, UCameraComponent* FallbackCamera, float DeltaTime, FInputFrame& InputFrame);
	bool ApplyLookInput(APlayerController* Controller, UCameraComponent* FallbackCamera, float DeltaTime, FInputFrame& InputFrame);

	EControllerMovementFrame GetMovementFrame() const { return static_cast<EControllerMovementFrame>(MovementFrame); }
	void SetMovementFrame(EControllerMovementFrame InFrame);


	float GetMoveSpeed() const { return MoveSpeed; }
	void SetMoveSpeed(float InSpeed);

	float GetSprintMultiplier() const { return SprintMultiplier; }
	void SetSprintMultiplier(float InMultiplier);

	float GetLookSensitivity() const { return LookSensitivity; }
	void SetLookSensitivity(float InSensitivity);

	float GetMinPitch() const { return MinPitch; }
	void SetMinPitch(float InMinPitch);

	float GetMaxPitch() const { return MaxPitch; }
	void SetMaxPitch(float InMaxPitch);

	void RemapActorReferences(const TMap<uint32, uint32>& ActorUUIDRemap) override;

	private:
	UCameraComponent* ResolveTargetCamera(APlayerController* Controller, UCameraComponent* FallbackCamera) const;
	void NormalizeOptions();

private:
	int32 MovementFrame = static_cast<int32>(EControllerMovementFrame::ControlRotation);
	FPROPERTY(DisplayName="Move Speed", Type=Float, min=0.0f, max=100000.0f, speed=0.1f)
	float MoveSpeed = 10.0f;
	FPROPERTY(DisplayName="Sprint Multiplier", Type=Float, min=0.0f, max=100.0f, speed=0.1f)
	float SprintMultiplier = 2.5f;
	FPROPERTY(DisplayName="Look Sensitivity", Type=Float, min=0.0f, max=100.0f, speed=0.01f)
	float LookSensitivity = 0.08f;
	FPROPERTY(DisplayName="Min Pitch", Type=Float, min=-180.0f, max=180.0f, speed=0.1f)
	float MinPitch = -89.0f;
	FPROPERTY(DisplayName="Max Pitch", Type=Float, min=-180.0f, max=180.0f, speed=0.1f)
	float MaxPitch = 89.0f;
};
