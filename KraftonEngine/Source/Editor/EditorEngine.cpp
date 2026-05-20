#include "Editor/EditorEngine.h"

#include "Profiling/StartupProfiler.h"
#include "Core/Notification.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Engine/Serialization/SceneSaveManager.h"
#include "Engine/Platform/DirectoryWatcher.h"
#include "Component/CameraComponent.h"
#include "Component/GizmoComponent.h"
#include "GameFramework/World.h"
#include "Viewport/GameViewportClient.h"
#include "Viewport/ViewportPresentationTypes.h"
#include "Editor/EditorRenderPipeline.h"
#include "Editor/UI/EditorFileUtils.h"
#include "Editor/Viewport/LevelEditorViewportClient.h"
#include "Object/ObjectFactory.h"
#include "Asset/Import/MeshManager.h"
#include "Asset/Import/FBX/Types/FBXSceneAsset.h"
#include "Core/ProjectSettings.h"
#include "Input/InputSystem.h"
#include "Profiling/FrameProfiler.h"
#include "GameFramework/AActor.h"
#include "GameFramework/PlayerController.h"
#include "Asset/Material/MaterialManager.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "Engine/Platform/Paths.h"
#include "Engine/Runtime/LoadingScreen.h"
#include "Runtime/ActorPoolSystem.h"
#include "Runtime/EngineFactory.h"
#include "GameClient/LinkedRuntimeModules.h"
#include <cmath>
#include <filesystem>

REGISTER_FACTORY(UEditorEngine)

namespace
{
	std::wstring GetDefaultFbxImportDirectory()
	{
		const std::filesystem::path Preferred = std::filesystem::path(FPaths::AssetDir()) / L"FBX";
		if (std::filesystem::exists(Preferred) && std::filesystem::is_directory(Preferred))
		{
			return Preferred.wstring();
		}

		const std::filesystem::path AssetDir(FPaths::AssetDir());
		if (std::filesystem::exists(AssetDir) && std::filesystem::is_directory(AssetDir))
		{
			return AssetDir.wstring();
		}

		return FPaths::RootDir();
	}

	FString TryMakeProjectRelativePath(const std::filesystem::path& AbsolutePath)
	{
		const std::filesystem::path RootPath = std::filesystem::path(FPaths::RootDir()).lexically_normal();
		const std::filesystem::path NormalizedPath = AbsolutePath.lexically_normal();
		if (!FPaths::IsPathInsideRoot(RootPath, NormalizedPath))
		{
			return FString();
		}

		const std::filesystem::path RelativePath = NormalizedPath.lexically_relative(RootPath);
		if (RelativePath.empty())
		{
			return FString();
		}

		for (const std::filesystem::path& Part : RelativePath)
		{
			if (Part == L"..")
			{
				return FString();
			}
		}

		return FPaths::ToUtf8(RelativePath.generic_wstring());
	}

	class FScopedEditorLoadingScreen
	{
	public:
		explicit FScopedEditorLoadingScreen(FWindowsWindow* InWindow, bool bOverlay = false)
		{
			if (InWindow)
			{
				LoadingScreen.Begin(InWindow->GetHWND(), bOverlay);
				bActive = true;
			}
		}

		~FScopedEditorLoadingScreen()
		{
			if (bActive)
			{
				LoadingScreen.End();
			}
		}

		void Update(const wchar_t* StatusText, int Percent = -1)
		{
			if (bActive)
			{
				LoadingScreen.Update(StatusText, Percent);
			}
		}

	private:
		FLoadingScreen LoadingScreen;
		bool bActive = false;
	};

	UEngine* CreateEditorEngine()
	{
		return UObjectManager::Get().CreateObject<UEditorEngine>();
	}

	FEngineFactoryRegistrar GEditorEngineRegistrar("Editor", &CreateEditorEngine);
}

namespace
{
FString BuildScenePathFromStem(const FString& InStem)
{
	std::filesystem::path ScenePath = std::filesystem::path(FSceneSaveManager::GetSceneDirectory())
		/ (FPaths::ToWide(InStem) + FSceneSaveManager::SceneExtension);
	return FPaths::ToUtf8(ScenePath.wstring());
}

FString GetFileStem(const FString& InPath)
{
	const std::filesystem::path Path(FPaths::ToWide(InPath));
	return FPaths::ToUtf8(Path.stem().wstring());
}
}

void UEditorEngine::Init(FWindowsWindow* InWindow)
{
	RegisterLinkedRuntimeModules();
	
	FProjectSettings::Get().LoadFromFile(FProjectSettings::GetDefaultPath());
	GetRuntimeModules().LoadModules(FProjectSettings::Get().RuntimeModules);

	// ?붿쭊 怨듯넻 珥덇린??(Renderer, D3D, ?깃?????
	UEngine::Init(InWindow);

	{
		SCOPE_STARTUP_STAT("MeshManager::ScanMeshAssets");
		FMeshManager::ScanMeshAssets();
	}

	{
		SCOPE_STARTUP_STAT("MeshManager::ScanFbxSourceFiles");
		FMeshManager::ScanFbxSourceFiles();
	}

	{
		SCOPE_STARTUP_STAT("MaterialManager::ScanAssets");
		FMaterialManager::Get().ScanMaterialAssets();
	}

	// ?먮뵒???꾩슜 珥덇린??
	FEditorSettings::Get().LoadFromFile(FEditorSettings::GetDefaultSettingsPath());

	{
		SCOPE_STARTUP_STAT("EditorMainPanel::Create");
		MainPanel.Create(Window, Renderer, this);
	}

	// 湲곕낯 ?붾뱶 ?앹꽦 ??紐⑤뱺 ?쒕툕?쒖뒪??珥덇린?붿쓽 湲곕컲
	CreateWorldContext(EWorldType::Editor, FName("Default"));
	SetActiveWorld(WorldList[0].ContextHandle);
	GetWorld()->InitWorld();

	// Selection & Gizmo
	SelectionManager.Init();
	SelectionManager.SetWorld(GetWorld());

	// 酉고룷???덉씠?꾩썐 珥덇린??+ ??λ맂 ?ㅼ젙 蹂듭썝
	ViewportLayout.Initialize(this, Window, Renderer, &SelectionManager);
	ViewportLayout.LoadFromSettings();

	{
		SCOPE_STARTUP_STAT("Editor::LoadStartLevel");
		LoadStartLevel();
	}
	ApplyTransformSettingsToGizmo();

	// Editor render pipeline
	{
		SCOPE_STARTUP_STAT("EditorRenderPipeline::Create");
		SetRenderPipeline(std::make_unique<FEditorRenderPipeline>(this, Renderer));
	}
}

void UEditorEngine::Shutdown()
{
	// ?먮뵒???댁젣 (?붿쭊蹂대떎 癒쇱?)
	ViewportLayout.SaveToSettings();
	MainPanel.SaveToSettings();
	FProjectSettings::Get().SaveToFile(FProjectSettings::GetDefaultPath());
	FEditorSettings::Get().SaveToFile(FEditorSettings::GetDefaultSettingsPath());
	CloseScene();
	SelectionManager.Shutdown();
	MainPanel.Release();

	// 酉고룷???덉씠?꾩썐 ?댁젣
	ViewportLayout.Release();

	// ?붿쭊 怨듯넻 ?댁젣 (Renderer, D3D ??
	UEngine::Shutdown();
}

void UEditorEngine::OnWindowResized(uint32 Width, uint32 Height)
{
	UEngine::OnWindowResized(Width, Height);
	// ?덈룄??由ъ궗?댁쫰 ?쒖뿉??ImGui ?⑤꼸???ㅼ젣 ?ш린瑜?寃곗젙?섎?濡?
	// FViewport RT??SSplitter ?덉씠?꾩썐?먯꽌 吏??由ъ궗?댁쫰濡?泥섎━??
}

void UEditorEngine::Tick(float DeltaTime)
{
	const float RawDeltaTime = DeltaTime;
	FFrameProfiler::BeginFrame();

	// --- PIE ?붿껌 泥섎━ (?꾨젅??寃쎄퀎?먯꽌 泥섎━?섎룄濡?Tick ?좊몢?먯꽌 ?뚮퉬) ---
	if (bRequestEndPlayMapQueued)
	{
		bRequestEndPlayMapQueued = false;
		EndPlayMap();
	}
	if (PlaySessionRequest.has_value())
	{
		StartQueuedPlaySessionRequest();
	}

	float WorldDeltaTime = RawDeltaTime;
	if (IsPlayingInEditor() || (GetWorld() && GetWorld()->HasBegunPlay()))
	{
		GetTimeManager().Update(RawDeltaTime);
		WorldDeltaTime = GetTimeManager().GetGameDeltaTime();
	}

	ApplyTransformSettingsToGizmo();
	FDirectoryWatcher::Get().ProcessChanges();
	FNotificationManager::Get().Tick(RawDeltaTime);
	InputSystem::Get().Tick();
	// 異뷀썑 寃뚯엫 ?꾩슜 Task 遺꾨━ ??WorldDeltaTime ?곸슜 ?щ? 寃??
	TaskScheduler.Tick(RawDeltaTime);
	MainPanel.Update(RawDeltaTime);
	InputSystem::Get().RefreshSnapshot();


	for (FEditorViewportClient* VC : ViewportLayout.GetAllViewportClients())
	{
		VC->Tick(RawDeltaTime);
	}

	WorldTick(WorldDeltaTime, RawDeltaTime);
	Render(RawDeltaTime);
	SelectionManager.Tick();
}

UCameraComponent* UEditorEngine::GetCamera() const
{
	if (FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport())
	{
		return ActiveVC->GetCamera();
	}
	return nullptr;
}

void UEditorEngine::RenderUI(float DeltaTime)
{
	MainPanel.Render(DeltaTime);

	// PIE 以묒뿉??寃뚯엫 UI??媛숈? UGameViewportClient 湲곗??쇰줈 ?뚮뜑?쒕떎.
	// RmlUi renderer媛 PresentationRect濡?scissor瑜?嫄멸린 ?뚮Ц???먮뵒??酉고룷???⑤꼸 諛뽰쑝濡??섍?吏 ?딅뒗??
	if (IsPlayingInEditor())
	{
		if (UGameViewportClient* GameViewportClient = GetGameViewportClient())
		{
			if (IViewportUiLayer* UiLayer = GameViewportClient->GetUiLayer())
			{
				UiLayer->SetLayerVisible("PauseMenu", false);
				UiLayer->Update(DeltaTime);
				UiLayer->Render();
			}
		}
	}
}

void UEditorEngine::ToggleCoordSystem()
{
	FEditorSettings& Settings = FEditorSettings::Get();
	Settings.CoordSystem = (Settings.CoordSystem == EEditorCoordSystem::World)
		? EEditorCoordSystem::Local
		: EEditorCoordSystem::World;
	ApplyTransformSettingsToGizmo();
}

void UEditorEngine::ApplyTransformSettingsToGizmo()
{
	ApplyTransformSettingsToGizmo(GetGizmo());
}

void UEditorEngine::ApplyTransformSettingsToGizmo(UGizmoComponent* Gizmo)
{
	if (!Gizmo)
	{
		return;
	}

	const FEditorSettings& Settings = FEditorSettings::Get();
	const bool bForceLocalForScale = Gizmo->GetMode() == EGizmoMode::Scale;
	Gizmo->SetWorldSpace(bForceLocalForScale ? false : (Settings.CoordSystem == EEditorCoordSystem::World));
	// ?먮뵒???ㅼ젙??醫뚰몴怨??ㅻ깄 媛믪쓣 留??꾨젅??Gizmo ?곹깭? ?숆린?뷀븳??
	Gizmo->SetSnapSettings(
		Settings.bEnableTranslationSnap, Settings.TranslationSnapSize,
		Settings.bEnableRotationSnap, Settings.RotationSnapSize,
		Settings.bEnableScaleSnap, Settings.ScaleSnapSize);
}

// ??? PIE (Play In Ediator) ????????????????????????????????
// UE ?⑦꽩 ?붿빟: Request???⑥씪 ?щ’(std::optional)????λ쭔 ?섍퀬 利됱떆 ?ㅽ뻾?섏? ?딅뒗??
// ?ㅼ젣 StartPIE???ㅼ쓬 Tick ?좊몢??StartQueuedPlaySessionRequest?먯꽌 ?쇱뼱?쒕떎.
// ?댁쑀??UI 肄쒕갚/?몃옖??뀡 ?꾩쨷 媛숈? 遺덉븞?뺥븳 ??대컢???쇳븯湲??꾪븿.

void UEditorEngine::RequestPlaySession(const FRequestPlaySessionParams& InParams)
{
	// ?숈떆 ?붿껌? UE? ?숈씪?섍쾶 ??뼱?대떎 (吏꾩쭨 ???꾨떂 ???⑥씪 ?щ’).
	PlaySessionRequest = InParams;
}

void UEditorEngine::CancelRequestPlaySession()
{
	PlaySessionRequest.reset();
}

void UEditorEngine::RequestEndPlayMap()
{
	if (!PlayInEditorSessionInfo.has_value())
	{
		return;
	}
	bRequestEndPlayMapQueued = true;
}

void UEditorEngine::StartQueuedPlaySessionRequest()
{
	if (!PlaySessionRequest.has_value())
	{
		return;
	}

	const FRequestPlaySessionParams Params = *PlaySessionRequest;
	PlaySessionRequest.reset();

	// ?대? PIE 以묒씠硫?湲곗〈 ?몄뀡???뺣━ ???덈줈 ?쒖옉 (?⑥닚??.
	if (PlayInEditorSessionInfo.has_value())
	{
		EndPlayMap();
	}

	switch (Params.SessionDestination)
	{
	case EPIESessionDestination::InProcess:
		StartPlayInEditorSession(Params);
		break;
	}
}

void UEditorEngine::StartPlayInEditorSession(const FRequestPlaySessionParams& Params)
{
	InputSystem::Get().ResetAllKeyStates();
	InputSystem::Get().ResetTransientState();
	InputSystem::Get().ClearGuiCapture();

	// 1) ?꾩옱 ?먮뵒???붾뱶瑜?蹂듭젣??PIE ?붾뱶 ?앹꽦 (UE??CreatePIEWorldByDuplication ???.
	UWorld* EditorWorld = GetWorld();
	if (!EditorWorld)
	{
		return;
	}
	// DuplicateAs(PIE)濡?蹂듭젣?섎㈃ Actor 蹂듭젣 ?꾩뿉 WorldType???ㅼ젙?섏뼱
	// EditorOnly 而댄룷?뚰듃???꾨줉?쒓? ?꾩삁 ?앹꽦?섏? ?딆쓬.
	UWorld* PIEWorld = EditorWorld->DuplicateAs(EWorldType::PIE);
	if (!PIEWorld)
	{
		return;
	}

	// 2) PIE WorldContext瑜?WorldList???깅줉.
	FWorldContext Ctx;
	Ctx.WorldType = EWorldType::PIE;
	Ctx.ContextHandle = FName("PIE");
	Ctx.ContextName = "PIE";
	Ctx.World = PIEWorld;
	WorldList.push_back(Ctx);

	// 3) ?몄뀡 ?뺣낫 湲곕줉 (?댁쟾 ?쒖꽦 ?몃뱾 ?ы븿 ??EndPlayMap?먯꽌 蹂듭썝).
	FPlayInEditorSessionInfo Info;
	Info.OriginalRequestParams = Params;
	Info.PIEStartTime = 0.0;
	Info.PreviousActiveWorldHandle = GetActiveWorldHandle();
	if (FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport())
	{
		if (UCameraComponent* VCCamera = ActiveVC->GetCamera())
		{
			Info.SavedViewportCamera.Location = VCCamera->GetWorldLocation();
			Info.SavedViewportCamera.Rotation = VCCamera->GetRelativeRotation();
			Info.SavedViewportCamera.CameraState = VCCamera->GetCameraState();
			Info.SavedViewportCamera.bValid = true;
		}
	}
	PlayInEditorSessionInfo = Info;

	// 4) ActiveWorldHandle??PIE濡??꾪솚 ???댄썑 GetWorld()??PIE ?붾뱶瑜?諛섑솚.
	SetActiveWorld(FName("PIE"));

	TaskScheduler.Clear();
	GetRuntimeModules().OnWorldCreated(PIEWorld);

	// GPU Occlusion readback? ProxyId 湲곕컲?대씪 ?붾뱶媛 媛덈━硫?stale.
	// ?댁쟾 ?꾨젅??寃곌낵瑜?臾댄슚?뷀빐??wrong-proxy hit 諛⑹?.
	if (IRenderPipeline* Pipeline = GetRenderPipeline())
	{
		Pipeline->OnSceneCleared();
	}

	// 5) PIE??移대찓?쇰뒗 ?먮뵒??酉고룷??移대찓?쇨? ?꾨땲??PIE World ?덉쓽 CameraComponent瑜??곗꽑 ?ъ슜?쒕떎.
	//    ?먮뵒??酉고룷??移대찓?쇰? ActiveCamera濡??ｌ쑝硫?Pawn/Controller/ActiveCamera???꾨? ?고쉶?섏뼱
	//    移대찓?쇰? 異붽??대룄 ?뚮젅???붾㈃???꾨Т 諛섏쓳???녿뒗 臾몄젣媛 ?앷릿??
	PIEWorld->AutoWirePlayerController();
	if (UCameraComponent* GameplayCamera = PIEWorld->ResolveGameplayViewCamera())
	{
		PIEWorld->SetActiveCamera(GameplayCamera);
		PIEWorld->SetViewCamera(GameplayCamera);
	}
	else if (FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport())
	{
		PIEWorld->SetActiveCamera(ActiveVC->GetCamera());
		PIEWorld->SetViewCamera(ActiveVC->GetCamera());
	}

	// 6) Selection??PIE ?붾뱶 湲곗??쇰줈 ?щ컮?몃뵫 ???먮뵒???≫꽣瑜?媛由ы궓 梨꾨줈 ?먮㈃
	//    ?쏀궧(=PIE ?붾뱶) / outliner / outline ?뚮뜑媛 紐⑤몢 ?닿툔?쒕떎.
	SelectionManager.ClearSelection();
	//SelectionManager.SetGizmoEnabled(false); //PIE媛 ?쒖옉?섎㈃ gizmo 鍮꾪솢?깊솕
	SelectionManager.SetWorld(PIEWorld);

	if (!GetGameViewportClient())
	{
		UGameViewportClient* PIEViewportClient = UObjectManager::Get().CreateObject<UGameViewportClient>();
		SetGameViewportClient(PIEViewportClient);
	}
	if (UGameViewportClient* PIEViewportClient = GetGameViewportClient())
	{
		if (Window)
		{
			PIEViewportClient->SetOwnerWindow(Window->GetHWND());
		}
		APlayerController* PIEController = PIEWorld->FindOrCreatePlayerController();
		PIEWorld->AutoWirePlayerController(PIEController);
		UCameraComponent* InitialTargetCamera = PIEWorld->ResolveGameplayViewCamera(PIEController);
		FViewport* InitialViewport = nullptr;
		if (FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport())
		{
			if (!InitialTargetCamera)
			{
				InitialTargetCamera = ActiveVC->GetCamera();
			}
			InitialViewport = ActiveVC->GetViewport();
			const FRect& ActiveRect = ActiveVC->GetViewportScreenRect();
			const FViewportPresentationRect PresentationRect(
				ActiveRect.X,
				ActiveRect.Y,
				ActiveRect.Width,
				ActiveRect.Height);
			PIEViewportClient->SetPresentationRect(PresentationRect);
			PIEViewportClient->SetCursorClipRect(PresentationRect);
		}
		PIEViewportClient->SetPlayerController(PIEController);
		PIEViewportClient->OnBeginPIE(InitialTargetCamera, InitialViewport);

		FViewportModuleContext ModuleContext;
		ModuleContext.Engine = this;
		ModuleContext.Window = Window;
		ModuleContext.Renderer = &Renderer;
		ModuleContext.ViewportClient = PIEViewportClient;
		ModuleContext.UiCommands.ExecuteCommand = [this](const FString& CommandName)
		{
			if (CommandName == "Viewport.Resume" || CommandName == "Viewport.ClosePauseMenu" || CommandName == "Application.Exit")
			{
				RequestEndPlayMap();
			}
			else if (CommandName == "Application.RestartSession")
			{
				FRequestPlaySessionParams Params;
				RequestPlaySession(Params);
			}
		};
		GetRuntimeModules().OnViewportCreated(ModuleContext);

	}
	EnterPIEPossessedMode();
	
	//??肄붾뱶? ??묐릺??寃??꾨옒 EndPlayMap()???덉쓬.
	//MainPanel.HideEditorWindowsForPIE(); //PIE 以묒뿉???먮뵒???⑤꼸???④?.
	//ViewportLayout.DisableWorldAxisForPIE(); //PIE 以묒뿉???붾뱶 異??뚮뜑留곸쓣 鍮꾪솢?깊솕.

	// 7) BeginPlay ?몃━嫄???紐⑤뱺 ?깅줉/諛붿씤?⑹씠 ?앸궃 ?ㅼ쓬 泥?Tick ?댁쟾???몄텧.
	//    UWorld::BeginPlay媛 bHasBegunPlay瑜?癒쇱? ?명똿?섎?濡?BeginPlay ?꾩쨷
	//    SpawnActor濡?留뚮뱺 ?좉퇋 ?≫꽣???먮룞?쇰줈 BeginPlay?쒕떎.
	PIEWorld->BeginPlay();
	FSoundManager::Get().PlayBGM();
}

void UEditorEngine::EndPlayMap()
{
	if (!PlayInEditorSessionInfo.has_value())
	{
		return;
	}

	UWorld* PIEWorld = nullptr;
	if (FWorldContext* PIEContext = GetWorldContextFromHandle(FName("PIE")))
	{
		PIEWorld = PIEContext->World;
	}

	TaskScheduler.Clear();

	// PIE ?고???Row ?≫꽣???ㅼ젣 ??젣?⑸땲??
	GetRuntimeModules().OnPreWorldReset(PIEWorld);

	// ????ㅼ뼱媛 ?덈뜕 PIE ?≫꽣 李몄“?????붾뱶 湲곗??쇰줈留??딆뒿?덈떎.
	// FActorPoolSystem::Shutdown()? ?꾩껜 ???吏?곕?濡?PIE?먯꽌??ClearWorld媛 ???덉쟾?⑸땲??
	if (PIEWorld)
	{
		FActorPoolSystem::Get().ClearWorld(PIEWorld);
	}

	// ?쒖꽦 ?붾뱶瑜?PIE ?쒖옉 ???몃뱾濡?蹂듭썝.
	const FName PrevHandle = PlayInEditorSessionInfo->PreviousActiveWorldHandle;
	SetActiveWorld(PrevHandle);

	// 蹂듦???Editor ?붾뱶??VisibleProxies/罹먯떆??移대찓???곹깭瑜?媛뺤젣 臾댄슚??
	// PIE 以?Editor WorldTick??skip?섏뼱 罹먯떆媛 PIE ?쒖옉 ???쒖젏 洹몃?濡??⑥븘 ?덇퀬,
	// NeedsVisibleProxyRebuild()媛 移대찓??蹂??湲곕컲?대씪 false瑜?諛섑솚?섎㈃ stale
	// VisibleProxies媛 洹몃?濡??ъ궗?⑸릺??dangling proxy 李몄“濡??щ옒?쒓? ?????덈떎.
	//
	// ?먰븳 Renderer::PerObjectCBPool? ProxyId濡??몃뜳?깅릺???붾뱶 媛?怨듭쑀 ??대씪,
	// PIE 以?PIE ?꾨줉?쒓? ??뼱???щ’??洹몃?濡??⑥븘 ?덉쑝硫?Editor ?꾨줉?쒖쓽
	// bPerObjectCBDirty=false ?곹깭濡??명빐 ?낅줈?쒓? skip?섏뼱 PIE 留덉?留?transform?쇰줈
	// ?뚮뜑?쒕떎. 紐⑤뱺 Editor ?꾨줉?쒕? PerObjectCB dirty濡?留덊궧???ъ뾽濡쒕뱶 媛뺤젣.
	if (UWorld* EditorWorld = GetWorld())
	{
		EditorWorld->GetScene().MarkAllPerObjectCBDirty();

		// ActiveCamera??PIE ?쒖옉 ??PIE ?붾뱶濡???꺼議뚭퀬 PIE ?붾뱶? ?④퍡 ?뚭눼?먮떎.
		// Editor ?붾뱶??ActiveCamera???ъ쟾??洹?dangling ?ъ씤?곕? 媛由ы궗 ???덉쑝誘濡?
		// ?쒖꽦 酉고룷?몄쓽 移대찓?쇰줈 ?ㅼ떆 諛붿씤?⑺빐 以섏빞 frustum culling???뺤긽 ?숈옉?쒕떎.
		if (FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport())
		{
			if (UCameraComponent* VCCamera = ActiveVC->GetCamera())
			{
				if (PlayInEditorSessionInfo->SavedViewportCamera.bValid)
				{
					const FPIEViewportCameraSnapshot& SavedCamera = PlayInEditorSessionInfo->SavedViewportCamera;
					VCCamera->SetWorldLocation(SavedCamera.Location);
					VCCamera->SetRelativeRotation(SavedCamera.Rotation);
					VCCamera->SetCameraState(SavedCamera.CameraState);
				}

				EditorWorld->SetActiveCamera(VCCamera);
			}
		}
	}

	// Selection???먮뵒???붾뱶濡?蹂듭썝 ??PIE ?≫꽣??怨??뚭눼?섎?濡?癒쇱? 鍮꾩슫??
	SelectionManager.ClearSelection();
	//SelectionManager.SetGizmoEnabled(true); //PIE媛 ?앸굹硫?gizmo ?쒖꽦??
	SelectionManager.SetWorld(GetWorld());
	
	//??肄붾뱶? ??묐릺??寃??꾩쓽 StartPlayInEditorSession()???덉쓬.
	//MainPanel.RestoreEditorWindowsAfterPIE();
	//ViewportLayout.RestoreWorldAxisAfterPIE();

	if (UGameViewportClient* PIEViewportClient = GetGameViewportClient())
	{
		PIEViewportClient->OnEndPIE();
		UObjectManager::Get().DestroyObject(PIEViewportClient);
		SetGameViewportClient(nullptr);
	}

	// PIE WorldContext ?쒓굅 (DestroyWorldContext媛 EndPlay + DestroyObject ?섑뻾).
	DestroyWorldContext(FName("PIE"));

	// PIE ?붾뱶???꾨줉?쒓? 紐⑤몢 ?뚭눼?먯쑝誘濡?GPU Occlusion readback 臾댄슚??
	if (IRenderPipeline* Pipeline = GetRenderPipeline())
	{
		Pipeline->OnSceneCleared();
	}

	PlayInEditorSessionInfo.reset();
	PIEControlMode = EPIEControlMode::Possessed;
	InputSystem::Get().ResetCaptureStateForPIEEnd();
	FSoundManager::Get().StopBGM();
}

bool UEditorEngine::TogglePIEControlMode()
{
	if (!IsPlayingInEditor())
	{
		return false;
	}

	if (PIEControlMode == EPIEControlMode::Possessed)
	{
		return EnterPIEEjectedMode();
	}
	return EnterPIEPossessedMode();
}

bool UEditorEngine::EnterPIEPossessedMode()
{
	if (!IsPlayingInEditor())
	{
		return false;
	}

	PIEControlMode = EPIEControlMode::Possessed;
	SyncGameViewportPIEControlState(true);
	InputSystem::Get().SetUseRawMouse(true);
	InputSystem::Get().ResetAllKeyStates();
	InputSystem::Get().ResetTransientState();
	InputSystem::Get().ClearGuiCapture();
	return true;
}

bool UEditorEngine::EnterPIEEjectedMode()
{
	if (!IsPlayingInEditor())
	{
		return false;
	}

	PIEControlMode = EPIEControlMode::Ejected;
	SyncGameViewportPIEControlState(false);
	InputSystem::Get().SetUseRawMouse(false);
	InputSystem::Get().ResetAllKeyStates();
	InputSystem::Get().ResetTransientState();
	InputSystem::Get().ClearGuiCapture();
	return true;
}

void UEditorEngine::SyncGameViewportPIEControlState(bool bPossessedMode)
{
	UGameViewportClient* PIEViewportClient = GetGameViewportClient();
	if (!PIEViewportClient)
	{
		return;
	}

	PIEViewportClient->SetPIEPossessedInputEnabled(bPossessedMode);
	if (!bPossessedMode)
	{
		return;
	}

	if (Window)
	{
		PIEViewportClient->SetOwnerWindow(Window->GetHWND());
	}

	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->FindOrCreatePlayerController() : nullptr;
	if (World)
	{
		World->AutoWirePlayerController(Controller);
	}
	UCameraComponent* Camera = World ? World->ResolveGameplayViewCamera(Controller) : nullptr;
	PIEViewportClient->SetPlayerController(Controller);

	if (FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport())
	{
		if (!Camera)
		{
			Camera = ActiveVC->GetCamera();
		}
		if (PIEViewportClient->GetViewport() != ActiveVC->GetViewport())
		{
			PIEViewportClient->SetViewport(ActiveVC->GetViewport());
		}
		const FRect& ActiveRect = ActiveVC->GetViewportScreenRect();
		const FViewportPresentationRect PresentationRect(
			ActiveRect.X,
			ActiveRect.Y,
			ActiveRect.Width,
			ActiveRect.Height);
		PIEViewportClient->SetPresentationRect(PresentationRect);
		PIEViewportClient->SetCursorClipRect(PresentationRect);
	}
	PIEViewportClient->Possess(Camera);
}

// ??? 湲곗〈 硫붿꽌????????????????????????????????????????????

void UEditorEngine::ResetViewport()
{
	ViewportLayout.ResetViewport(GetWorld());
}

void UEditorEngine::CloseScene()
{
	ClearScene();
}

void UEditorEngine::NewScene()
{
	StopPlayInEditorImmediate();
	ClearScene();
	FWorldContext& Ctx = CreateWorldContext(EWorldType::Editor, FName("NewScene"), "New Scene");
	Ctx.World->InitWorld();
	SetActiveWorld(Ctx.ContextHandle);
	SelectionManager.SetWorld(GetWorld());

	ResetViewport();
	CurrentLevelFilePath.clear();
}

void UEditorEngine::LoadStartLevel()
{
	const FString& StartLevel = FEditorSettings::Get().EditorStartLevel;
	if (StartLevel.empty())
	{
		return;
	}

	std::filesystem::path ScenePath = std::filesystem::path(FSceneSaveManager::GetSceneDirectory())
		/ (FPaths::ToWide(StartLevel) + FSceneSaveManager::SceneExtension);
	FString FilePath = FPaths::ToUtf8(ScenePath.wstring());

	if (!LoadSceneFromPath(FilePath))
	{
		// 濡쒕뱶 ?ㅽ뙣 ??鍮??ъ쑝濡?蹂듦뎄
		NewScene();
	}
}

void UEditorEngine::ClearScene()
{
	StopPlayInEditorImmediate();
	SelectionManager.ClearSelection();
	SelectionManager.SetWorld(nullptr);

	// ???꾨줉???뚭눼 ??GPU Occlusion ?ㅽ뀒?댁쭠 ?곗씠??臾댄슚??
	if (IRenderPipeline* Pipeline = GetRenderPipeline())
		Pipeline->OnSceneCleared();

	for (FWorldContext& Ctx : WorldList)
	{
		Ctx.World->EndPlay();
		UObjectManager::Get().DestroyObject(Ctx.World);
	}

	WorldList.clear();
	ActiveWorldHandle = FName::None;
	CurrentLevelFilePath.clear();

	ViewportLayout.DestroyAllCameras();
}

UCameraComponent* UEditorEngine::FindSceneViewportCamera() const
{
	for (FLevelEditorViewportClient* VC : ViewportLayout.GetLevelViewportClients())
	{
		if (!VC)
		{
			continue;
		}

		if (VC->GetRenderOptions().ViewportType == ELevelViewportType::Perspective
			|| VC->GetRenderOptions().ViewportType == ELevelViewportType::FreeOrthographic)
		{
			return VC->GetCamera();
		}
	}

	return nullptr;
}

void UEditorEngine::RestoreViewportCamera(const FPerspectiveCameraData& CamData)
{
	if (!CamData.bValid)
	{
		return;
	}

	if (UCameraComponent* Camera = FindSceneViewportCamera())
	{
		Camera->SetWorldLocation(CamData.Location);
		Camera->SetRelativeRotation(CamData.Rotation);
		FCameraState CameraState = Camera->GetCameraState();
		CameraState.FOV = CamData.FOV;
		CameraState.AspectRatio = CamData.AspectRatio;
		CameraState.NearZ = CamData.NearClip;
		CameraState.FarZ = CamData.FarClip;
		CameraState.OrthoWidth = CamData.OrthoWidth;
		CameraState.bIsOrthogonal = CamData.bOrthographic;
		Camera->SetCameraState(CameraState);
	}
}

bool UEditorEngine::SaveSceneAs(const FString& InSceneName)
{
	if (InSceneName.empty())
	{
		return false;
	}

	StopPlayInEditorImmediate();
	FWorldContext* Context = GetWorldContextFromHandle(GetActiveWorldHandle());
	if (!Context || !Context->World)
	{
		return false;
	}

	FSceneSaveManager::SaveSceneAsJSON(InSceneName, *Context, FindSceneViewportCamera());
	CurrentLevelFilePath = BuildScenePathFromStem(InSceneName);
	return true;
}

bool UEditorEngine::SaveScene()
{
	if (HasCurrentLevelFilePath())
	{
		return SaveSceneAs(GetFileStem(CurrentLevelFilePath));
	}

	return SaveSceneAsWithDialog();
}

bool UEditorEngine::SaveSceneAsWithDialog()
{
	const std::wstring InitialDir = FSceneSaveManager::GetSceneDirectory();
	const std::wstring DefaultFile = HasCurrentLevelFilePath()
		? std::filesystem::path(FPaths::ToWide(CurrentLevelFilePath)).filename().wstring()
		: std::wstring(L"Untitled.Scene");
	const FString SelectedPath = FEditorFileUtils::SaveFileDialog({
		.Filter = L"Scene Files (*.Scene)\0*.Scene\0All Files (*.*)\0*.*\0",
		.Title = L"Save Scene As",
		.DefaultExtension = L"Scene",
		.InitialDirectory = InitialDir.c_str(),
		.DefaultFileName = DefaultFile.c_str(),
		.OwnerWindowHandle = Window ? Window->GetHWND() : nullptr,
		.bFileMustExist = false,
		.bPathMustExist = true,
		.bPromptOverwrite = true,
		.bReturnRelativeToProjectRoot = false,
	});
	if (SelectedPath.empty())
	{
		return false;
	}

	return SaveSceneAs(GetFileStem(SelectedPath));
}

bool UEditorEngine::LoadSceneFromPath(const FString& InScenePath)
{
	if (InScenePath.empty())
	{
		return false;
	}

	StopPlayInEditorImmediate();
	ClearScene();

	FWorldContext LoadContext;
	FPerspectiveCameraData CameraData;
	FSceneSaveManager::LoadSceneFromJSON(InScenePath, LoadContext, CameraData);
	if (!LoadContext.World)
	{
		return false;
	}

	WorldList.push_back(LoadContext);
	SetActiveWorld(LoadContext.ContextHandle);
	SelectionManager.SetWorld(LoadContext.World);
	LoadContext.World->WarmupPickingData();
	ResetViewport();
	RestoreViewportCamera(CameraData);

	CurrentLevelFilePath = InScenePath;
	return true;
}

bool UEditorEngine::ImportFbxAsSkeletalMeshAssetWithDialog()
{
	const std::wstring InitialDir = GetDefaultFbxImportDirectory();
	const FString SelectedPath = FEditorFileUtils::OpenFileDialog({
		.Filter = L"FBX Files (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0",
		.Title = L"Import FBX as SkeletalMesh Asset",
		.InitialDirectory = InitialDir.c_str(),
		.OwnerWindowHandle = Window ? Window->GetHWND() : nullptr,
		.bFileMustExist = true,
		.bPathMustExist = true,
		.bPromptOverwrite = false,
		.bReturnRelativeToProjectRoot = false,
	});
	if (SelectedPath.empty())
	{
		return false;
	}

	const std::filesystem::path SelectedDiskPath = std::filesystem::path(FPaths::ToWide(SelectedPath)).lexically_normal();
	const FString RelativeFbxPath = TryMakeProjectRelativePath(SelectedDiskPath);
	if (RelativeFbxPath.empty())
	{
		const FString Error = "FBX import only supports files inside the current project folder.";
		UE_LOG("[EditorEngine] %s Selected=%s", Error.c_str(), SelectedPath.c_str());
		FNotificationManager::Get().AddNotification(Error, ENotificationType::Error, 4.0f);
		return false;
	}

	FScopedEditorLoadingScreen LoadingScreen(Window, true);
	auto ProgressCb = [&LoadingScreen](int Percent, const wchar_t* Status)
	{
		LoadingScreen.Update(Status, Percent);
	};
	UFBXSceneAsset* SceneAsset = FMeshManager::LoadFbxScene(RelativeFbxPath, ProgressCb);
	if (!SceneAsset)
	{
		const FString Error = "Failed to import FBX scene: " + RelativeFbxPath;
		UE_LOG("[EditorEngine] %s", Error.c_str());
		FNotificationManager::Get().AddNotification(Error, ENotificationType::Error, 4.0f);
		return false;
	}

	const TArray<USkeletalMesh*>& SkeletalMeshes = SceneAsset->GetSkeletalMeshes();
	if (SkeletalMeshes.empty() || !SkeletalMeshes[0] || !SkeletalMeshes[0]->GetSkeletalMeshAsset())
	{
		const FString Error = "No SkeletalMesh found in FBX: " + RelativeFbxPath;
		UE_LOG("[EditorEngine] %s", Error.c_str());
		FNotificationManager::Get().AddNotification(Error, ENotificationType::Error, 4.0f);
		return false;
	}

	std::filesystem::create_directories(
		std::filesystem::path(FPaths::RootDir()) / L"Asset" / L"Runtime" / L"SkeletalMesh");
	const std::filesystem::path AssetDiskPath =
		std::filesystem::path(FPaths::RootDir()) / L"Asset" / L"Runtime" / L"SkeletalMesh" /
		(SelectedDiskPath.stem().wstring() + L".asset");
	const FString RelativeAssetPath = TryMakeProjectRelativePath(AssetDiskPath);
	if (RelativeAssetPath.empty())
	{
		const FString Error = "Failed to build destination asset path for: " + RelativeFbxPath;
		UE_LOG("[EditorEngine] %s", Error.c_str());
		FNotificationManager::Get().AddNotification(Error, ENotificationType::Error, 4.0f);
		return false;
	}

	USkeletalMesh* SkeletalMesh = SkeletalMeshes[0];
	if (FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset())
	{
		MeshAsset->PathFileName = RelativeAssetPath;
	}

	LoadingScreen.Update(L"Saving SkeletalMesh asset...", 100);
	if (!FMeshManager::SaveSkeletalMeshToFile(SkeletalMesh, RelativeAssetPath))
	{
		const FString Error = "Failed to save SkeletalMesh asset: " + RelativeAssetPath;
		UE_LOG("[EditorEngine] %s", Error.c_str());
		FNotificationManager::Get().AddNotification(Error, ENotificationType::Error, 4.0f);
		return false;
	}

	LoadingScreen.Update(L"Opening Skeletal Editor...", 100);
	RefreshContentBrowser();
	FNotificationManager::Get().AddNotification(
		"Imported SkeletalMesh asset: " + RelativeAssetPath,
		ENotificationType::Success,
		3.0f);
	return OpenSkeletalMeshViewerAsset(RelativeAssetPath);
}

bool UEditorEngine::ImportFbxWithOptions(const FString& FbxPath, const FFBXImportOptions& Options)
{
	if (FbxPath.empty())
	{
		return false;
	}

	const std::filesystem::path SelectedDiskPath =
		std::filesystem::path(FPaths::ToWide(FbxPath)).lexically_normal();

	// Resolve to project-relative path
	FString RelativeFbxPath = TryMakeProjectRelativePath(SelectedDiskPath);
	if (RelativeFbxPath.empty())
	{
		// Try treating it as already relative
		RelativeFbxPath = FbxPath;
	}

	FScopedEditorLoadingScreen LoadingScreen(Window, true);
	auto ProgressCb = [&LoadingScreen](int Percent, const wchar_t* Status)
	{
		LoadingScreen.Update(Status, Percent);
	};

	UFBXSceneAsset* SceneAsset = FMeshManager::LoadFbxScene(RelativeFbxPath, ProgressCb, &Options);
	if (!SceneAsset)
	{
		const FString Error = "Failed to import FBX scene: " + RelativeFbxPath;
		UE_LOG("[EditorEngine] %s", Error.c_str());
		FNotificationManager::Get().AddNotification(Error, ENotificationType::Error, 4.0f);
		return false;
	}

	const TArray<USkeletalMesh*>& SkeletalMeshes = SceneAsset->GetSkeletalMeshes();
	if (SkeletalMeshes.empty() || !SkeletalMeshes[0] || !SkeletalMeshes[0]->GetSkeletalMeshAsset())
	{
		const FString Error = "No SkeletalMesh found in FBX: " + RelativeFbxPath;
		UE_LOG("[EditorEngine] %s", Error.c_str());
		FNotificationManager::Get().AddNotification(Error, ENotificationType::Error, 4.0f);
		return false;
	}

	// Build .asset path in Asset/Runtime/SkeletalMesh/
	std::filesystem::create_directories(
		std::filesystem::path(FPaths::RootDir()) / L"Asset" / L"Runtime" / L"SkeletalMesh");
	const std::filesystem::path AssetDiskPath =
		std::filesystem::path(FPaths::RootDir()) / L"Asset" / L"Runtime" / L"SkeletalMesh" /
		(SelectedDiskPath.stem().wstring() + L".asset");
	const FString RelativeAssetPath = TryMakeProjectRelativePath(AssetDiskPath);
	if (RelativeAssetPath.empty())
	{
		const FString Error = "Failed to build destination asset path for: " + RelativeFbxPath;
		UE_LOG("[EditorEngine] %s", Error.c_str());
		FNotificationManager::Get().AddNotification(Error, ENotificationType::Error, 4.0f);
		return false;
	}

	USkeletalMesh* SkeletalMesh = SkeletalMeshes[0];
	if (FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset())
	{
		MeshAsset->PathFileName = RelativeAssetPath;
	}

	LoadingScreen.Update(L"Saving SkeletalMesh asset...", 100);
	if (!FMeshManager::SaveSkeletalMeshToFile(SkeletalMesh, RelativeAssetPath))
	{
		const FString Error = "Failed to save SkeletalMesh asset: " + RelativeAssetPath;
		UE_LOG("[EditorEngine] %s", Error.c_str());
		FNotificationManager::Get().AddNotification(Error, ENotificationType::Error, 4.0f);
		return false;
	}

	// Save selected animations to Asset/Runtime/Animation/
	std::filesystem::create_directories(
		std::filesystem::path(FPaths::RootDir()) / L"Asset" / L"Runtime" / L"Animation");
	const std::wstring AnimRuntimeDir =
		(std::filesystem::path(FPaths::RootDir()) / L"Asset" / L"Runtime" / L"Animation" / L"").wstring();
	const std::wstring FbxStem = SelectedDiskPath.stem().wstring();

	for (UAnimSequence* Seq : SceneAsset->GetAnimSequences())
	{
		if (!Seq) continue;
		const FString& AnimName = Seq->GetSequenceName();
		// Read the actual baked FPS from the data model so the suffix is always accurate
		const UAnimDataModel* SeqModel = Seq->GetDataModel();
		const float BakedFPS = SeqModel ? SeqModel->GetFrameRate().AsDecimal() : 30.0f;
		wchar_t FpsSuffix[32];
		swprintf_s(FpsSuffix, L"_%dfps", static_cast<int>(std::round(BakedFPS)));
		const std::wstring AnimFileName =
			FbxStem + L"_" + (AnimName.empty() ? L"Anim" : FPaths::ToWide(AnimName)) + FpsSuffix + L".asset";
		const FString RelAnimPath = TryMakeProjectRelativePath(
			std::filesystem::path(AnimRuntimeDir) / AnimFileName);
		if (!RelAnimPath.empty())
		{
			FMeshManager::SaveAnimSequenceToFile(Seq, RelAnimPath);
		}
	}

	LoadingScreen.Update(L"Opening Skeletal Editor...", 100);
	RefreshContentBrowser();
	FNotificationManager::Get().AddNotification(
		"Imported SkeletalMesh asset: " + RelativeAssetPath,
		ENotificationType::Success,
		3.0f);
	return OpenSkeletalMeshViewerAsset(RelativeAssetPath);
}

bool UEditorEngine::LoadSceneWithDialog()
{
	const std::wstring InitialDir = FSceneSaveManager::GetSceneDirectory();
	const FString SelectedPath = FEditorFileUtils::OpenFileDialog({
		.Filter = L"Scene Files (*.Scene)\0*.Scene\0All Files (*.*)\0*.*\0",
		.Title = L"Load Scene",
		.InitialDirectory = InitialDir.c_str(),
		.OwnerWindowHandle = Window ? Window->GetHWND() : nullptr,
		.bFileMustExist = true,
		.bPathMustExist = true,
		.bPromptOverwrite = false,
		.bReturnRelativeToProjectRoot = false,
	});
	if (SelectedPath.empty())
	{
		return false;
	}

	return LoadSceneFromPath(SelectedPath);
}

void UEditorEngine::RenderSkeletalMeshViewerPreview(
	UWorld* PreviewWorld,
	FViewport* PreviewViewport,
	FSkeletalMeshViewerViewportClient* PreviewClient)
{
	auto EditorPipeline = static_cast<FEditorRenderPipeline*>(GetRenderPipeline());
	if (!EditorPipeline)
	{
		return;
	}

	EditorPipeline->RenderPreviewViewport(
		PreviewWorld,
		PreviewViewport,
		PreviewClient,
		Renderer);

	Renderer.BeginFrame();
}

