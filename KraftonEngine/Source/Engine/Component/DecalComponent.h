#pragma once
#include "PrimitiveComponent.h"
#include "Core/ResourceTypes.h"
#include "Collision/ConvexVolume.h"
#include "DecalComponent.generated.h"

class UStaticMeshComponent;

// class DecalProxy;

UCLASS()
class UDecalComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()

	UDecalComponent() = default;
	~UDecalComponent() override = default;

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	FPrimitiveSceneProxy* CreateSceneProxy() override;

	// Property Editor 지원
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;
	
	void Serialize(FArchive& Ar) override;
	void PostDuplicate() override;

	// Color (with Color)
	void SetColor(FVector4 InColor) { Color = InColor; }
	FVector4 GetColor() const;

	// --- Material ---
	void SetMaterial(class UMaterial* InMaterial);
	class UMaterial* GetMaterial() const { return Material; }

	const FConvexVolume GetDecalVolume() { return ConvexVolume; }
	void UpdateDecalVolumeFromTransform();
	void OnTransformDirty() override;

	const TArray<UStaticMeshComponent*>& GetReceivers() const { return Receivers; }

	class UBillboardComponent* EnsureEditorBillboard();

private:
	void HandleFade(float DeltaTime);
	void UpdateReceivers();
	void DrawDebugBox();

private:
	FConvexVolume ConvexVolume;
	TArray<UStaticMeshComponent*> Receivers;
	FPROPERTY(DisplayName="Material", Type=MaterialSlot)
	FMaterialSlot MaterialSlot;
	UMaterial* Material = nullptr;
	FPROPERTY(Type=Vec4)
	FVector4 Color = {1,1,1,1};
	FPROPERTY(Type=Float)
	float FadeInDelay = 0;
	FPROPERTY(Type=Float)
	float FadeInDuration = 0;
	FPROPERTY(Type=Float)
	float FadeOutDelay = 0;
	FPROPERTY(Type=Float)
	float FadeOutDuration = 0;
	float FadeTimer = 0;
	float FadeOpacity = 1.0f;		// 페이드 효과 사용 시 Color.A에 곱함
};
