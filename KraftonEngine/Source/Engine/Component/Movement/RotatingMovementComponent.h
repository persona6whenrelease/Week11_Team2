#pragma once

#include "MovementComponent.h"
#include "Math/Quat.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"
#include "RotatingMovementComponent.generated.h"

// 런타임 동안 UpdatedComponent를 일정 각속도로 회전시키는 이동 컴포넌트
UCLASS()
class URotatingMovementComponent : public UMovementComponent
{
public:
	GENERATED_BODY()

	URotatingMovementComponent() = default;
	~URotatingMovementComponent() override = default;

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void Serialize(FArchive& Ar) override;

	const FRotator& GetRotationRate() const { return RotationRate; }
	void SetRotationRate(const FRotator& NewRate) { RotationRate = NewRate; }

	bool IsRotationInLocalSpace() const { return bRotationInLocalSpace; }
	void SetRotationInLocalSpace(bool bNewRotationInLocalSpace)
	{
		FPROPERTY(DisplayName="Rotation In Local Space", Type=Bool, min=0.0f, max=0.0f, speed=0.0f)
		bRotationInLocalSpace = bNewRotationInLocalSpace;
		bWorldPivotInitialized = false;
		CachedWorldPivotComponent = nullptr;
	}

	void SetPivotTranslation(const FVector& NewPivotTranslation)
	{
		FPROPERTY(DisplayName="Pivot Translation", Type=Vec3, min=0.0f, max=0.0f, speed=0.1f)
		PivotTranslation = NewPivotTranslation;
		bWorldPivotInitialized = false;
		CachedWorldPivotComponent = nullptr;
	}
	const FVector& GetPivotTranslation() const { return PivotTranslation; }

	void ResetWorldPivotCache()
	{
		bWorldPivotInitialized = false;
		CachedWorldPivotComponent = nullptr;
		CachedWorldPivotTranslation = FVector(0.0f, 0.0f, 0.0f);
		CachedWorldPivotLocation = FVector(0.0f, 0.0f, 0.0f);
	}

private:
	FPROPERTY(DisplayName="Rotation Rate", Type=Rotator, min=0.0f, max=0.0f, speed=0.1f)
	FRotator RotationRate = FRotator(0.0f, 90.0f, 0.0f);
	bool bRotationInLocalSpace = false;
	FVector PivotTranslation = FVector(0.0f, 0.0f, 0.0f);

	// World-space 공전 모드에서 고정 pivot을 유지하기 위한 런타임 캐시
	USceneComponent* CachedWorldPivotComponent = nullptr;
	FVector CachedWorldPivotTranslation = FVector(0.0f, 0.0f, 0.0f);
	FVector CachedWorldPivotLocation = FVector(0.0f, 0.0f, 0.0f);
	bool bWorldPivotInitialized = false;
};
