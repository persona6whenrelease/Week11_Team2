#pragma once

#include "Component/ActorComponent.h"
#include "Math/Vector.h"
#include "MovementComponent.generated.h"

class USceneComponent;
struct FHitResult;

struct FControllerMovementInput
{
	FVector LocalInput = FVector::ZeroVector;      // X Forward, Y Right, Z Up
	FVector WorldDirection = FVector::ZeroVector; // Normalized world movement direction
	FVector WorldDelta = FVector::ZeroVector;     // Immediate movement delta for simple movement components
	FVector MovementForward = FVector::ForwardVector; // World-space forward basis used when the input was built
	FVector MovementRight = FVector::RightVector;     // World-space right basis used when the input was built
	float DeltaTime = 0.0f;
	float MoveSpeed = 0.0f;
	float SpeedMultiplier = 1.0f;
};

//TODO : 해당 컴포넌트 베이스 역할을 하고 고유의 기능은 없기에 오브젝트에 부여할 수 없도록 바꿔야 합니다!

/**
 * 런타임(PIE, Game mode) 동안
 * USceneComponent를 움직이는 로직들의 베이스 클래스.
 * 실제 이동 로직은 자식 클래스에서 담당합니다.
 */
UCLASS()
class UMovementComponent : public UActorComponent
{
public:
	GENERATED_BODY()

	UMovementComponent() = default;
	~UMovementComponent() override = default;

	void BeginPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void Serialize(FArchive& Ar) override;
	void PostEditProperty(const char* PropertyName) override;

	void SetUpdatedComponent(USceneComponent* NewUpdatedComponent);
	USceneComponent* GetUpdatedComponent() const;
	bool HasValidUpdatedComponent() const { return GetUpdatedComponent() != nullptr; }
	const FString& GetUpdatedComponentPath() const { return UpdatedComponentPath; }
	FString GetUpdatedComponentDisplayName() const;
	TArray<USceneComponent*> GetOwnerSceneComponents() const;
	bool ResolveUpdatedComponent();
	FString BuildUpdatedComponentPath(const USceneComponent* TargetComponent) const;
	void ClearUpdatedComponentIfMatches(const USceneComponent* RemovedComponent);
	virtual bool CanReceiveControllerInput() const { return bReceiveControllerInput; }
	void RecordControllerMovementInput(const FControllerMovementInput& Input);
	const FVector& GetLastControllerWorldDirection() const { return LastControllerWorldDirection; }
	const FVector& GetLastMovementInput() const { return LastControllerWorldDirection; }
	const FVector& GetLastControllerWorldDelta() const { return LastControllerWorldDelta; }
	const FVector& GetLastControllerMovementForward() const { return LastControllerMovementForward; }
	const FVector& GetLastControllerMovementRight() const { return LastControllerMovementRight; }
	float GetLastControllerInputTime() const { return LastControllerInputTime; }
	virtual const FVector& GetVelocity() const { return MovementVelocity; }
	virtual FVector GetMovementVelocity() const { return GetVelocity(); }
	bool HasMovementInput() const { return !LastControllerWorldDirection.IsNearlyZero(); }
	bool IsMoving() const { return !GetVelocity().IsNearlyZero(); }
	virtual bool ApplyControllerMovementInput(const FControllerMovementInput& Input);
	int32 GetControllerInputPriority() const { return ControllerInputPriority; }

protected:
	bool SafeMoveUpdatedComponent(const FVector& Delta, FHitResult* OutHit = nullptr);
	bool SafeMoveUpdatedComponentClipped(const FVector& Delta, FVector* OutAppliedDelta = nullptr, FHitResult* OutHit = nullptr);
	bool SafeMoveUpdatedComponentPreserveAxes(const FVector& Delta, FVector* OutAppliedDelta = nullptr, FHitResult* OutHit = nullptr);
	bool SafeMoveUpdatedComponentPreserveInputAxes(const FVector& Delta, const FVector& InputForward, const FVector& InputRight, FVector* OutAppliedDelta = nullptr, FHitResult* OutHit = nullptr);
	bool SafeMoveUpdatedComponentPreserveInputAxes2D(const FVector& Delta, const FVector& InputForward, const FVector& InputRight, FVector* OutAppliedDelta = nullptr, FHitResult* OutHit = nullptr);
	bool IsUpdatedComponentBlockingOverlapped(FHitResult* OutHit = nullptr) const;
	bool TryResolveBlockingOverlap(const FVector& DesiredDelta, const FVector& InputForward, const FVector& InputRight, FVector* OutAppliedDelta = nullptr, FHitResult* OutHit = nullptr);
	bool SafeMoveUpdatedComponent2D(const FVector& Delta, float PlaneZ, FHitResult* OutHit = nullptr);
	bool SafeMoveUpdatedComponentClipped2D(const FVector& Delta, float PlaneZ, FVector* OutAppliedDelta = nullptr, FHitResult* OutHit = nullptr);
	bool TryResolveBlockingOverlap2D(const FVector& DesiredDelta, const FVector& InputForward, const FVector& InputRight, float PlaneZ, FVector* OutAppliedDelta = nullptr, FHitResult* OutHit = nullptr);
	void TryAutoRegisterUpdatedComponent();
	USceneComponent* FindUpdatedComponentByPath(const FString& InPath) const;

	USceneComponent* UpdatedComponent = nullptr; // 움직일 대상
	FPROPERTY(DisplayName="Auto Register Updated", Type=Bool)
	bool bAutoRegisterUpdatedComponent = true;
	FPROPERTY(DisplayName="Updated Component", Type=ObjectRef, Class=USceneComponent)
	FString UpdatedComponentPath;
	FPROPERTY(DisplayName="Receive Controller Input", Type=Bool)
	bool bReceiveControllerInput = false;
	FPROPERTY(DisplayName="Controller Input Priority", Type=Int, min=-100.0f, max=100.0f, speed=1.0f)
	int32 ControllerInputPriority = 0;

	FPROPERTY(DisplayName="Last Controller Direction", Type=Vec3, min=0.0f, max=0.0f, speed=0.1f)
	FVector LastControllerWorldDirection = FVector::ZeroVector;
	FPROPERTY(DisplayName="Last Controller Delta", Type=Vec3, min=0.0f, max=0.0f, speed=0.1f)
	FVector LastControllerWorldDelta = FVector::ZeroVector;
	FVector LastControllerMovementForward = FVector::ForwardVector;
	FVector LastControllerMovementRight = FVector::RightVector;
	float LastControllerInputTime = 0.0f;
	FVector MovementVelocity = FVector::ZeroVector;
};
