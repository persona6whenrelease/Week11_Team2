#include "Component/Light/LightComponent.h"
#include "Serialization/Archive.h"
#include "Object/ObjectFactory.h"

REGISTER_FACTORY(ULightComponent)
HIDE_FROM_COMPONENT_LIST(ULightComponent)

void ULightComponent::Serialize(FArchive& Ar)
{
	ULightComponentBase::Serialize(Ar);
	Ar << ShadowResolutionScale;
	Ar << ShadowBias;
	Ar << ShadowSlopeBias;
	Ar << ShadowNormalBias;
	Ar << ShadowSharpen;
}

