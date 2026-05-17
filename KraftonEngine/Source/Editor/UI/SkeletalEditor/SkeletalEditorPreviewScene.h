#pragma once

#include "Core/CoreTypes.h"

class UWorld;
class AActor;
class ADirectionalLightActor;
class USkeletalMesh;
class USkeletalMeshComponent;
class FViewport;
class FSkeletalMeshViewerViewportClient;

// 한 탭이 자체적으로 보유하는 preview 씬 (월드/뷰포트/메시 컴포넌트).
// SkeletalMesh / AnimSequence 모드 무관하게 동일하므로 컴포지션으로 공유한다.
struct FSkeletalEditorPreviewScene
{
	~FSkeletalEditorPreviewScene();

	void Ensure();
	void Release();
	void Tick(float DeltaTime);
	void SetPreviewMesh(USkeletalMesh* InMesh, bool bResetCamera = true);
	bool IsReady() const { return PreviewWorld != nullptr; }

	UWorld* PreviewWorld = nullptr;
	AActor* PreviewActor = nullptr;
	ADirectionalLightActor* PreviewDirectionalLightActor = nullptr;
	USkeletalMeshComponent* PreviewMeshComponent = nullptr;
	FViewport* PreviewViewport = nullptr;
	FSkeletalMeshViewerViewportClient* PreviewViewportClient = nullptr;
};
