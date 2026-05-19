#pragma once

#include "Component/ActorComponent.h"
#include "Runtime/PooledObjectInterface.h"
#include "ParryableProjectileComponent.generated.h"

UCLASS()
class UParryableProjectileComponent : public UActorComponent, public IPooledObjectInterface
{
public:
	GENERATED_BODY()

	bool IsParried() const { return bParried; }
	void SetParried(bool bInParried) { bParried = bInParried; }

	void OnSpawnFromPool() override { ResetParryState(); }
	void OnReturnToPool() override { ResetParryState(); }

private:
	void ResetParryState() { bParried = false; }

	bool bParried = false;
};
