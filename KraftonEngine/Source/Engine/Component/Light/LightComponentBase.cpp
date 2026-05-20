#include "LightComponentBase.h"
#include "Serialization/Archive.h"
#include "Object/ObjectFactory.h"
#include "GameFramework/AActor.h"
#include "Component/BillboardComponent.h"
#include "Asset/Material/MaterialManager.h"

REGISTER_FACTORY(ULightComponentBase)
HIDE_FROM_COMPONENT_LIST(ULightComponentBase)

void ULightComponentBase::Serialize(FArchive& Ar)
{
	USceneComponent::Serialize(Ar);
	Ar << Intensity;
	Ar << LightColor;
	Ar << bVisible;
	Ar << bCastShadows;
}

UBillboardComponent* ULightComponentBase::EnsureEditorBillboard()
{
	if (!Owner)
	{
		return nullptr;
	}

	const char* IconMaterialPath = nullptr;
	switch (GetLightType())
	{
	case ELightComponentType::Ambient:
		IconMaterialPath = "Asset/Runtime/Material/Editor/AmbientLight.mat";
		break;
	case ELightComponentType::Directional:
		IconMaterialPath = "Asset/Runtime/Material/Editor/DirectionalLight.mat";
		break;
	case ELightComponentType::Point:
		IconMaterialPath = "Asset/Runtime/Material/Editor/PointLight.mat";
		break;
	case ELightComponentType::Spot:
		IconMaterialPath = "Asset/Runtime/Material/Editor/SpotLight.mat";
		break;
	}

	if (!IconMaterialPath)
	{
		return nullptr;
	}

	for (USceneComponent* Child : GetChildren())
	{
		UBillboardComponent* Billboard = Cast<UBillboardComponent>(Child);
		if (Billboard && Billboard->IsEditorOnlyComponent())
		{
			// 에디터 아이콘 빌보드는 부모 스케일과 컴포넌트 트리 기본 표시에서 분리한다.
			Billboard->SetAbsoluteScale(true);
			Billboard->SetHiddenInComponentTree(true);
			return Billboard;
		}
	}

	UBillboardComponent* Billboard = Owner->AddComponent<UBillboardComponent>();
	if (Billboard)
	{
		Billboard->AttachToComponent(this);
		// 에디터 아이콘 빌보드는 부모 스케일과 컴포넌트 트리 기본 표시에서 분리한다.
		Billboard->SetAbsoluteScale(true);
		Billboard->SetEditorOnlyComponent(true);
		Billboard->SetHiddenInComponentTree(true);
		auto Material = FMaterialManager::Get().GetOrCreateMaterial(IconMaterialPath);
		Billboard->SetMaterial(Material);
	}

	return Billboard;
}
