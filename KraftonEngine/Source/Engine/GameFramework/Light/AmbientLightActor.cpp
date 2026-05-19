#include "AmbientLightActor.h"
#include "Component/BillboardComponent.h"
#include "Component/Light/LightComponentBase.h"
#include "Component/Light/AmbientLightComponent.h"
#include "Asset/Material/MaterialManager.h"

REGISTER_FACTORY(AAmbientLightActor)

void AAmbientLightActor::InitDefaultComponents()
{
	if (LightComponent && GetRootComponent() == LightComponent)
	{
		BillboardComponent = LightComponent->EnsureEditorBillboard();
		return;
	}

	if (UAmbientLightComponent* RootLight = Cast<UAmbientLightComponent>(GetRootComponent()))
	{
		LightComponent = RootLight;
		BillboardComponent = LightComponent->EnsureEditorBillboard();
		return;
	}

	for (UActorComponent* Component : GetComponents())
	{
		if (UAmbientLightComponent* Light = Cast<UAmbientLightComponent>(Component))
		{
			LightComponent = Light;
			BillboardComponent = LightComponent->EnsureEditorBillboard();
			return;
		}
	}

	if (!GetRootComponent())
	{
		LightComponent = AddComponent<UAmbientLightComponent>();
		SetRootComponent(LightComponent);
		BillboardComponent = LightComponent->EnsureEditorBillboard();
	}
}
