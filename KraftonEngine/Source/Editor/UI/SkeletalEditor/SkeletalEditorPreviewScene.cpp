#include "Editor/UI/SkeletalEditor/SkeletalEditorPreviewScene.h"

#include "Runtime/Engine.h"
#include "GameFramework/World.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "Component/SkeletalMeshComponent.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "Viewport/Viewport.h"
#include "Editor/Viewport/SkeletalMeshViewerViewportClient.h"
#include "Render/Pipeline/Renderer.h"

FSkeletalEditorPreviewScene::~FSkeletalEditorPreviewScene()
{
	Release();
}

void FSkeletalEditorPreviewScene::Ensure()
{
	if (PreviewWorld)
	{
		return;
	}

	PreviewWorld = UObjectManager::Get().CreateObject<UWorld>();
	PreviewWorld->SetWorldType(EWorldType::Editor);
	PreviewWorld->InitWorld();

	PreviewActor = PreviewWorld->SpawnActor<AActor>();
	PreviewActor->bTickInEditor = true;

	PreviewMeshComponent = PreviewActor->AddComponent<USkeletalMeshComponent>();
	PreviewActor->SetRootComponent(PreviewMeshComponent);

	PreviewDirectionalLightActor = PreviewWorld->SpawnActor<ADirectionalLightActor>();
	if (PreviewDirectionalLightActor)
	{
		PreviewDirectionalLightActor->InitDefaultComponents();
		PreviewDirectionalLightActor->bTickInEditor = true;
		PreviewDirectionalLightActor->SetActorLocation(FVector(5.0f, 0.0f, 5.0f));
		PreviewDirectionalLightActor->SetActorRotation(FRotator(15.0f, 180.0f, 0.0f));
	}

	PreviewViewportClient = new FSkeletalMeshViewerViewportClient();
	PreviewViewportClient->Initialize();
	PreviewViewportClient->SetPreviewWorld(PreviewWorld);

	PreviewViewport = new FViewport();

	ID3D11Device* Device = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
	if (Device)
	{
		PreviewViewport->Initialize(Device, 512, 512);
		PreviewViewport->SetClient(PreviewViewportClient);
	}
}

void FSkeletalEditorPreviewScene::Release()
{
	if (PreviewViewport)
	{
		PreviewViewport->Release();
		delete PreviewViewport;
		PreviewViewport = nullptr;
	}

	if (PreviewViewportClient)
	{
		PreviewViewportClient->Shutdown();
		delete PreviewViewportClient;
		PreviewViewportClient = nullptr;
	}

	PreviewMeshComponent = nullptr;
	PreviewDirectionalLightActor = nullptr;
	PreviewActor = nullptr;

	if (PreviewWorld)
	{
		PreviewWorld->EndPlay();
		UObjectManager::Get().DestroyObject(PreviewWorld);
		PreviewWorld = nullptr;
	}
}

void FSkeletalEditorPreviewScene::SetPreviewMesh(USkeletalMesh* InMesh, bool bResetCamera)
{
	Ensure();

	if (!PreviewMeshComponent)
	{
		return;
	}

	PreviewMeshComponent->SetSkeletalMesh(InMesh);

	// 사용자가 명시적으로 Play를 누르도록 viewer는 일시정지 상태로 시작.
	PreviewMeshComponent->SetBakedAnimPaused(true);
	PreviewMeshComponent->SetBakedAnimTime(0.0f);
	PreviewMeshComponent->SetBakedAnimPlaybackSpeed(1.0f);

	if (PreviewViewportClient)
	{
		PreviewViewportClient->GetBoneSelectionManager().SetTargetSkeletalMesh(PreviewMeshComponent);
	}

	FSkeletalMesh* MeshAsset = InMesh ? InMesh->GetSkeletalMeshAsset() : nullptr;
	if (MeshAsset)
	{
		if (!MeshAsset->bBoundsValid)
		{
			MeshAsset->CacheBounds();
		}

		const FVector Center = MeshAsset->BoundsCenter;
		PreviewMeshComponent->SetRelativeLocation(FVector(-Center.X, -Center.Y, -Center.Z));
	}

	if (bResetCamera && PreviewViewportClient)
	{
		PreviewViewportClient->FrameMesh(MeshAsset);
	}
}

void FSkeletalEditorPreviewScene::Tick(float DeltaTime)
{
	if (!PreviewWorld)
	{
		return;
	}

	PreviewWorld->Tick(DeltaTime, DeltaTime, LEVELTICK_ViewportsOnly);
}
