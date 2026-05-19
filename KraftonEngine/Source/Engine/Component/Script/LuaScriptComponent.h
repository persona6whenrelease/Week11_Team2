#pragma once

#include "Component/ActorComponent.h"
#include "Core/PropertyTypes.h"
#include "Runtime/PooledObjectInterface.h"
#include "LuaScriptComponent.generated.h"

UCLASS()
class ULuaScriptComponent : public UActorComponent, public IPooledObjectInterface
{
public:
	GENERATED_BODY()

	virtual void BeginPlay() override;
	virtual void EndPlay() override;
	void OnSpawnFromPool() override;
	void OnReturnToPool() override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostEditProperty(const char* PropertyName) override;

	const FString& GetScriptPath() const { return ScriptPath; }
	void SetScriptPath(const FString& InScriptPath);
	bool ReloadScript();

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	FPROPERTY(Type=String)
	FString ScriptPath;
};
