#pragma once
#include "ShapeComponent.h"
#include "Math/Vector.h"
#include "CollisionTypes.h"
#include "BoxComponent.generated.h"

UCLASS()
class UBoxComponent : public UShapeComponent
{
public:
	GENERATED_BODY()
	virtual ECollisionShapeType GetCollisionShapeType() const override
	{
		return ECollisionShapeType::Box;
	}

	virtual FBoundingBox GetWorldAABB() const override;
	void DrawDebugShape(FScene& Scene, const FColor& Color) const override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;
	void Serialize(FArchive& Ar) override;

	const FVector& GetBoxExtent() const { return BoxExtent; }
	void SetBoxExtent(const FVector& InExtent)
	{
		FPROPERTY(DisplayName="Box Extent", Type=Vec3, min=0.0f, max=10000.0f, speed=1.0f)
		BoxExtent = InExtent;
		MarkWorldBoundsDirty();
	}


private:
	FVector BoxExtent = FVector(0.5f, 0.5f, 0.5f);
};
