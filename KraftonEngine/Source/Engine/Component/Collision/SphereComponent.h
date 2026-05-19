#pragma once
#include "ShapeComponent.h"
#include "CollisionTypes.h"
#include "SphereComponent.generated.h"

UCLASS()
class USphereComponent : public UShapeComponent
{
public:
	GENERATED_BODY()
	virtual ECollisionShapeType GetCollisionShapeType() const override
	{
		return ECollisionShapeType::Sphere;
	}

	virtual FBoundingBox GetWorldAABB() const override;
	void DrawDebugShape(FScene& Scene, const FColor& Color) const override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;
	void Serialize(FArchive& Ar) override;

	float GetSphereRadius() const { return SphereRadius; }
	void SetSphereRadius(float InRadius)
	{
		FPROPERTY(DisplayName="Sphere Radius", Type=Float, min=0.0f, max=10000.0f, speed=1.0f)
		SphereRadius = InRadius;
		MarkWorldBoundsDirty();
	}

private:
	float SphereRadius = 0.5f;
};
