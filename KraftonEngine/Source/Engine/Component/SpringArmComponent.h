#pragma once

#include "Component/SceneComponent.h"
#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Math/Rotator.h"
#include "Math/MathUtils.h"
#include "SpringArmComponent.generated.h"

class UCameraComponent;
class FArchive;

UCLASS()
class USpringArmComponent : public USceneComponent
{
public:
	GENERATED_BODY()

	USpringArmComponent();

	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	float GetTargetArmLength() const { return TargetArmLength; }
	void SetTargetArmLength(float InLength);

	const FVector& GetTargetOffset() const { return TargetOffset; }
	void SetTargetOffset(const FVector& InOffset) { TargetOffset = InOffset; }

	const FVector& GetSocketOffset() const { return SocketOffset; }
	void SetSocketOffset(const FVector& InOffset) { SocketOffset = InOffset; }

	bool IsCollisionTestEnabled() const { return bDoCollisionTest; }
	void SetDoCollisionTest(bool bEnabled) { bDoCollisionTest = bEnabled; }

	float GetProbeSize() const { return ProbeSize; }
	void SetProbeSize(float InProbeSize);

	float GetMinArmLength() const { return MinArmLength; }
	void SetMinArmLength(float InLength)
	{
		FPROPERTY(DisplayName="Min Arm Length", Type=Float, min=0.0f, max=10000.0f, speed=0.01f)
		MinArmLength = FMath::Max(InLength, 0.0f);
		NormalizeOption();
	}

	bool UsesPawnControlRotation() const { return bUsePawnControlRotation; }
	void SetUsePawnControlRotation(bool bEnabled) { bUsePawnControlRotation = bEnabled; }

	bool InheritsPitch() const { return bInheritPitch; }
	void SetInheritPitch(bool bEnabled) { bInheritPitch = bEnabled; }

	bool InheritsYaw() const { return bInheritYaw; }
	void SetInheritYaw(bool bEnabled) { bInheritYaw = bEnabled; }

	bool InheritsRoll() const { return bInheritRoll; }
	void SetInheritRoll(bool bEnabled) { bInheritRoll = bEnabled; }

	bool IsCameraLagEnabled() const { return bEnableCameraLag; }
	void SetEnableCameraLag(bool bEnabled) { bEnableCameraLag = bEnabled; }

	float GetCameraLagSpeed() const { return CameraLagSpeed; }
	void SetCameraLagSpeed(float InSpeed);

	float GetCollisionPullInSpeed() const { return CollisionPullInSpeed; }
	void SetCollisionPullInSpeed(float InSpeed)
	{
		FPROPERTY(DisplayName="Collision Pull In Speed", Type=Float, min=0.0f, max=1000.0f, speed=1.0f)
		CollisionPullInSpeed = FMath::Max(InSpeed, 0.0f);
	}

	float GetCollisionRecoverSpeed() const { return CollisionRecoverSpeed; }
	void SetCollisionRecoverSpeed(float InSpeed)
	{
		FPROPERTY(DisplayName="Collision Recover Speed", Type=Float, min=0.0f, max=1000.0f, speed=0.1f)
		CollisionRecoverSpeed = FMath::Max(InSpeed, 0.0f);
	}

	bool IsCollisionFixApplied() const { return bCollisionFixApplied; }
	float GetCurrentArmLength() const { return CurrentArmLength; }
	
	void RefreshCameraTransform(float DeltaTime = 0.0f);

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	void NormalizeOption();
	FQuat GetDesiredRotation() const;
	FVector ComputeDesiredCameraLocation(const FVector& Pivot, const FQuat& DesiredRotation) const;
	FVector ResolveCollision(const FVector& Pivot, const FVector& DesiredCameraLocation, float DeltaTime);
	void UpdateCameraChildren(const FVector& NewCameraLocation, const FQuat& CameraRotation);

private:
	FPROPERTY(DisplayName="Target Arm Length", Type=Float, min=0.0f, max=10000.0f, speed=0.1f)
	float TargetArmLength = 5.0f;
	FPROPERTY(DisplayName="Target Offset", Type=Vec3, min=0.0f, max=0.0f, speed=0.1f)
	FVector TargetOffset = FVector::ZeroVector;
	FPROPERTY(DisplayName="Socket Offset", Type=Vec3, min=0.0f, max=0.0f, speed=0.1f)
	FVector SocketOffset = FVector::ZeroVector;

	FPROPERTY(DisplayName="Do Collision Test", Type=Bool)
	bool bDoCollisionTest = true;
	FPROPERTY(DisplayName="Probe Size", Type=Float, min=0.0f, max=1000.0f, speed=0.01f)
	float ProbeSize = 0.2f;
	float MinArmLength = 0.25f;

	FPROPERTY(DisplayName="Use Pawn Control Rotation", Type=Bool)
	bool bUsePawnControlRotation = true;
	FPROPERTY(DisplayName="Inherit Pitch", Type=Bool)
	bool bInheritPitch = true;
	FPROPERTY(DisplayName="Inherit Yaw", Type=Bool)
	bool bInheritYaw = true;
	FPROPERTY(DisplayName="Inherit Roll", Type=Bool)
	bool bInheritRoll = false;

	FPROPERTY(DisplayName="Enable Camera Lag", Type=Bool)
	bool bEnableCameraLag = true;
	FPROPERTY(DisplayName="Camera Lag Speed", Type=Float, min=0.0f, max=100.0f, speed=0.1f)
	float CameraLagSpeed = 12.0f;
	float CollisionPullInSpeed = 100.0f;
	float CollisionRecoverSpeed = 10.0f;

	float CurrentArmLength = 5.0f;
	FVector LaggedCameraLocation = FVector::ZeroVector;
	bool bHasLaggedCameraLocation = false;
	bool bCollisionFixApplied = false;
};