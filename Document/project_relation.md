# KraftonEngine Animation 시스템 고도화 — 코드베이스 탐색 보고서

## Context

`Document/exploration_prompt.md`의 요구에 따라, Unreal-style Animation 시스템(8개 파트) 도입을 앞두고 KraftonEngine의 기존 코드베이스에서 **재사용 / 확장 / 신규 작성**이 필요한 위치를 파악했다. 결과는 각 파트별 표로 정리한다.

탐색 범위: `KraftonEngine/Source/` 전체 (Engine + Editor + Scripting + Serialization + Profiling + Render + UI). 모든 파일 경로는 `KraftonEngine/Source/...`(이하 `Source/`로 표기) 기준.

---

## 파트 1 — Animation Asset Pipeline

핵심 데이터 모델(`FAnimationClip`/`FBoneAnimTrack`/`FBoneAnimSample`)과 직렬화(`FArchive`)는 이미 견고하게 잡혀있어 **`UAnimSequence` 분리는 wrapper UObject만 추가하면 된다**. 가장 큰 결손은 **`USkeleton` 분리 부재**(현재 `FSkeletalMesh::Bones`에 embed)와 **Anim Notify 저장 구조 전무**. `FArchive`에 versioning 헤더가 없어 분리 시 기존 `.skm` 호환성 마이그레이션 별도 처리 필요.

| 과제 세부 항목 | 관련 위치 (파일:라인) | 관련 심볼/클래스 | 연관성 분류 | 비고 |
|---|---|---|---|---|
| Animation Data Model | `Source/Engine/Mesh/SkeletalMeshAsset.h:67-84` | `FAnimationClip` (Name/Duration/FrameRate/FrameCount/Tracks + `operator<<`) | 직접 사용 가능 | `UAnimSequence` 내부 보유 데이터로 그대로 재활용 |
| FBX Animation Import | `Source/Engine/Mesh/FBX/FbxAnimationParser.h:11-28` / `.cpp:21-166` | `FFbxAnimationParser::ParseSkeletonAnimations` | 확장 필요 | 사전 정보 그대로. 분리 후엔 `OutMesh` 대신 `UAnimSequence` 컬렉션으로 출력 받도록 시그니처 수정 필요 |
| Animation Asset Save / Load | `Source/Engine/Mesh/SkeletalMesh.cpp:18-35` + `Source/Engine/Serialization/Archive.h:1-91` + `Source/Engine/Serialization/WindowsArchive.h` | `USkeletalMesh::Serialize`, `FArchive::Serialize`, `FWindowsBinWriter/Reader` | 확장 필요 | 메커니즘은 직접 사용 가능. `UAnimSequence` 별도 `.uasset`-like 파일로 분리 시 신규 Save/Load 엔트리 + **versioning 헤더** 도입 권장 (현재 unversioned binary) |
| `UAnimSequence` (신규) | (없음) | (없음) | 미발견 | 신규 작성. `FAnimationClip` 데이터를 들고 `DECLARE_CLASS(UAnimSequence, UObject)`로 wrap. `Source/Engine/Mesh/`에 둘지 새 모듈 `Source/Engine/Animation/`을 만들지 선택 필요 |
| `UAnimDataModel` (신규) | (단서) `Source/Engine/Mesh/SkeletalMeshAsset.h:44-84` | `FBoneAnimTrack` + `FAnimationClip` | 단서만 발견 | UE5의 IAnimationDataModel 추상화는 미존재. 기존 데이터 구조를 internal model로 hold하는 신규 클래스 필요. 동일 디렉터리에 두는 게 자연스러움 |
| Bone Animation Track | `Source/Engine/Mesh/SkeletalMeshAsset.h:44-65` | `FBoneAnimTrack` (BoneIndex + `TArray<FBoneAnimSample>`), `FBoneAnimSample` (LocalMatrix only) | 직접 사용 가능 | T/R/S 분리 / curve / tangent 미보존 한계는 사전 정보대로. `FBoneAnimTrack`을 그대로 `UAnimSequence`로 이전 가능 |
| Skeleton reference | `Source/Engine/Mesh/SkeletalMeshAsset.h:21-36`, `:86-130` | `FBoneInfo` (Name/ParentIndex/LocalBindPose/InverseBindPose), `FSkeletalMesh::Bones` | 확장 필요 | `USkeleton` 신규 클래스로 `Bones` + bind pose만 분리, `FSkeletalMesh`에는 `USkeleton*` reference만 두는 리팩터 필요. `FFbxAnimationParser`의 `FFbxSkeletonMeta` (`Source/Engine/Mesh/FBX/FBXImportMeta.h:105-120`) 도 같이 손봐야 함 |
| Anim Notify 데이터 저장 | (없음) | (없음) | 미발견 | grep 결과 "Notify"/"Marker"/"Event" in Animation context 결과 0건. `FAnimationClip`에 `TArray<FAnimNotifyEvent>` 신규 멤버 추가 필요. 직렬화 인프라(`FArchive` + 기존 `friend operator<<` 패턴)는 그대로 활용 가능 |

---

## 파트 2 — Animation Runtime Core

런타임 재생 경로(time → keyframe sampling → local pose → mesh-space → skinning) **체인은 모두 `USkinnedMeshComponent` / `USkeletalMeshComponent`에 인라인으로 박혀있다**. UE의 `UAnimInstance` 같은 분리 객체는 부재. 가장 자연스러운 확장 경로는 `USkeletalMeshComponent`에서 시간/샘플링 로직을 떼어내 별도 `UAnimInstance`로 옮기고, 컴포넌트는 인스턴스 보유만 하는 형태. **Reverse play는 `BakedAnimPlaybackSpeed` 음수 + `fmod` 음수 보정으로 이미 작동**한다(`SkeletalMeshComponent.cpp:45-51`).

| 과제 세부 항목 | 관련 위치 (파일:라인) | 관련 심볼/클래스 | 연관성 분류 | 비고 |
|---|---|---|---|---|
| `UAnimInstance` (신규) | (단서) `Source/Engine/Component/SkeletalMeshComponent.h:13-35`, `.cpp:10-91` | `USkeletalMeshComponent::TickComponent` + `ApplyBakedAnimation` + 4개 멤버 | 단서만 발견 | 신규 작성. `USkeletalMeshComponent`의 anim 재생 멤버/메서드를 옮길 후보 위치 확보됨. `DECLARE_CLASS(UAnimInstance, UObject)` 권장 |
| `UAnimSingleNodeInstance` (신규) | (단서) `Source/Engine/Component/SkeletalMeshComponent.cpp:43-91` | `ApplyBakedAnimation` 의 단일 클립 재생 로직 | 단서만 발견 | 현재 `BakedAnimClipIndex` 단일 클립 재생 = 사실상 SingleNode 동작. 그대로 추출하여 base `UAnimInstance` 위에 placeholder로 만들기 자연스러움 |
| `PlayAnimation()` | `Source/Engine/Component/SkeletalMeshComponent.h:13-30` | `SetBakedAnimClipIndex / SetBakedAnimTime / SetBakedAnimPaused / SetBakedAnimPlaybackSpeed` | 확장 필요 | 동등 기능 4-개 setter로 분산. UE 시그니처 `PlayAnimation(UAnimSequence*, bool bLoop)`로 단일 진입점 재구성 필요 |
| Animation time update | `Source/Engine/Component/SkeletalMeshComponent.cpp:43-51` | `BakedAnimTime += DeltaTime * BakedAnimPlaybackSpeed; std::fmod(...)` | 직접 사용 가능 | 사전 정보. `UAnimInstance::Update`로 이전 권장 |
| Keyframe sampling | `Source/Engine/Component/SkeletalMeshComponent.cpp:53-78` | `FrameA/FrameB/Blend` 계산 + `LocalBonePoseMatrices[i] = lerp(A,B)` | 직접 사용 가능 | 사전 정보. **주의**: 4×4 matrix 전체 element-wise lerp(rotation slerp 아님). 코멘트(`cpp:81`)는 30fps에서 affine drift 무시 가능하다고 함 |
| Local Pose 계산 | `Source/Engine/Component/SkinnedMeshComponent.h` (`LocalBonePoseMatrices` 멤버) + `Source/Engine/Component/SkeletalMeshComponent.cpp:60-90` | `TArray<FMatrix> LocalBonePoseMatrices` | 확장 필요 | 멤버 위치를 컴포넌트 → `UAnimInstance::OutputLocalPose`로 이동 권장 |
| Component Space Pose 계산 | `Source/Engine/Component/SkinnedMeshComponent.cpp:309-335` | `RebuildMeshSpaceBoneMatrices` (FK 체인 — `MeshSpaceBone[i] = LocalBone[i] * MeshSpaceBone[ParentIndex]`) | 직접 사용 가능 | 사전 정보. ParentIndex < i 가정에 의존 (FBX importer가 보장하는 정렬) |
| Skinning Matrix 생성 | `Source/Engine/Component/SkinnedMeshComponent.cpp:337-393` | `SkinVerticesToReferencePose` 내부 `SkinMatrix = InverseBindPose * MeshSpaceBone` | 직접 사용 가능 | 사전 정보. `virtual`로 GPU/CPU 전환 hook 활용 가능 |
| Reverse Play | `Source/Engine/Component/SkeletalMeshComponent.cpp:45-51` | `BakedAnimPlaybackSpeed` 음수 + `fmod` 음수 보정 (`if (LoopedTime < 0.0f) LoopedTime += Clip.Duration;`) | 직접 사용 가능 | **사전 정보 추가 발견**: 음수 speed 자동 처리됨. UI toggle만 추가하면 됨 |

---

## 파트 3 — Animation Logic System

이 파트가 **가장 큰 결손**. Blending / State Machine / Notify dispatch 어느 것도 코드에 없음. 다만 인프라는 부분적으로 존재: **`TDelegate`(`Source/Engine/Runtime/Delegate.h`)가 multicast + member function binding을 지원**해 Notify dispatch 기반으로 활용 가능, **Lua(sol2 + LuaJIT) 통합도 풍부**하지만 SkeletalMeshComponent/Animation 바인딩은 미존재 → Lua State Machine은 신규 binding 추가 필요.

| 과제 세부 항목 | 관련 위치 (파일:라인) | 관련 심볼/클래스 | 연관성 분류 | 비고 |
|---|---|---|---|---|
| Animation Blending | (없음) | (없음) | 미발견 | grep 결과 0건. 신규 작성. `LocalBonePoseMatrices` 단계에서 두 개 이상의 pose를 weighted blend하는 단계 신설 필요 |
| Animation State Machine | (없음) | (없음) | 미발견 | grep "StateMachine"/"FSM"/"AnimGraph" 0건. 신규 작성 |
| Lua Animation State Machine | `Source/Engine/Scripting/LuaScriptSubsystem.h:17-133`, `Source/Engine/Scripting/LuaBindings.h:1-44`, `Source/Engine/Scripting/LuaPropertyBridge.h:1-30`, `Source/Engine/Component/Script/LuaScriptComponent.h` | sol2 + LuaJIT 기반 `FLuaScriptSubsystem`, per-component sol::environment, 핫리로드 지원 | 확장 필요 | Lua 인프라 자체는 **직접 사용 가능**. 다만 `LuaBindings.h`에 `RegisterSkeletalMeshComponentBinding` / `RegisterAnimSequenceBinding` 신규 추가 필요. `vcpkg.json`에 `luajit`/`sol2` 이미 등록됨 |
| Anim Notify Runtime | (없음) | (없음) | 미발견 | 데이터 저장(파트 1) + dispatch 메커니즘(아래) 양쪽 모두 신규. 트리거는 `ApplyBakedAnimation` 내 prev/curr time 비교 위치 추가 필요 |
| Transition 조건 처리 | (없음) | (없음) | 미발견 | 신규. State Machine과 같이 작성 |
| State별 animation 연결 | (없음) | (없음) | 미발견 | 신규 |
| Notify dispatch | `Source/Engine/Runtime/Delegate.h:29-177` | `TDelegate<...>::BroadCast / Add / AddDynamic / RemoveAllByInstance` | 단서만 발견 | Animation 직접 연관 코드 없으나 **multicast delegate + member function binding 인프라 그대로 활용 가능**. `UAnimInstance::OnNotify`를 `TDelegate<const FAnimNotifyEvent&>`로 선언 권장 |

---

## 파트 4 — Animation Sequence Viewer

Preview 인프라는 **`FEditorSkeletalMeshViewerWidget`이 이미 PreviewWorld + PreviewMeshComponent + PreviewViewport + PreviewViewportClient + bone selection 까지 갖춘 완성된 형태**(사전 정보보다 광범위). Animation asset 선택은 FBX를 ContentBrowser에서 더블클릭하면 viewer로 열리고 클립 dropdown에서 선택. 실질적으로 **viewer는 그대로 쓰고, 내부 데이터 소스만 `FAnimationClip` → `UAnimSequence`로 교체**하면 됨.

| 과제 세부 항목 | 관련 위치 (파일:라인) | 관련 심볼/클래스 | 연관성 분류 | 비고 |
|---|---|---|---|---|
| Animation Sequence Viewer | `Source/Editor/UI/EditorSkeletalMeshViewerWidget.h:1-67`, `.cpp:1195-1260` | `FEditorSkeletalMeshViewerWidget`, `RenderAnimationPlaybackPanel` | 직접 사용 가능 | 사전 정보. UAnimSequence 분리 후 panel 데이터 소스 swap만 필요 |
| Preview Viewport | `Source/Editor/UI/EditorSkeletalMeshViewerWidget.cpp:915-1010` (`EnsurePreviewScene`) + `Source/Editor/Viewport/SkeletalMeshViewerViewportClient.{h,cpp}` + `Source/Engine/UI/ImGui/ImGuiViewportPresenter.h` | `PreviewWorld` / `FViewport` / `FSkeletalMeshViewerViewportClient` / `FImGuiViewportPresenter::DrawInCurrentWindow` | 직접 사용 가능 | ImGui::Image와 SRV 합성이 `FImGuiViewportPresenter`로 이미 wrap됨 |
| Preview SkeletalMeshComponent | `Source/Editor/UI/EditorSkeletalMeshViewerWidget.h` (`USkeletalMeshComponent* PreviewMeshComponent`) | `PreviewMeshComponent`, `PreviewSkeletalMesh` | 직접 사용 가능 | |
| Animation asset 선택 | `Source/Editor/UI/EditorSkeletalMeshViewerWidget.cpp:1214` (`ImGui::BeginCombo("Clip", ...)`) + `Source/Editor/UI/ContentBrowser/ContentBrowserElement.{h,cpp}` (`FBXElement::OnDoubleLeftClicked`) | combo box 기반 클립 dropdown, ContentBrowser 더블클릭 import flow | 확장 필요 | 현재는 FBX 내부 클립만 list. UAnimSequence 분리 후 별도 `UAnimSequence` content item / asset picker dropdown 추가 권장 |
| 재생 결과 확인 | `Source/Editor/UI/EditorSkeletalMeshViewerWidget.cpp:1233-1260` (Play/Pause/Reset/Time/Speed slider) + bone visualization (`UpdateBoneDebugLines`, `.cpp:730+`) | playback panel + octahedral bone debug draw | 직접 사용 가능 | |

---

## 파트 5 — Timeline / Notify Editor UI

현재는 **`ImGui::SliderFloat("Time(s)")` 단일 줄짜리 scrubber**. 진짜 timeline UI(track / marker / 멀티트랙)는 부재. ImGui DrawList를 이용해 신규 작성해야 함. Frame step / Loop toggle도 없음. **Anim Notify Track UI는 데이터 모델(파트1)부터 신규**라 바닥부터.

| 과제 세부 항목 | 관련 위치 (파일:라인) | 관련 심볼/클래스 | 연관성 분류 | 비고 |
|---|---|---|---|---|
| Timeline UI | `Source/Editor/UI/EditorSkeletalMeshViewerWidget.cpp:1247` | `ImGui::SliderFloat("Time (s)", &Time, 0.0f, Clip.Duration, "%.3f")` | 확장 필요 | 1차원 scrubber만 존재. ImGui DrawList(`ImGui::GetWindowDrawList()->AddRectFilled/AddLine`) 사용 신규 timeline 작성 |
| Play / Pause / Stop | `Source/Editor/UI/EditorSkeletalMeshViewerWidget.cpp:1233-1238` | Play/Pause 토글 button + Reset button (Stop 별도 없음, Reset이 동등) | 직접 사용 가능 | Stop=Pause+Reset 조합으로 충분 |
| Frame step | (없음) | (없음) | 미발견 | 신규. `BakedAnimTime += 1.0f / FrameRate` 호출만 추가하면 됨 |
| Scrubbing | `Source/Editor/UI/EditorSkeletalMeshViewerWidget.cpp:1247` | `ImGui::SliderFloat` | 직접 사용 가능 | 기본 scrub 작동. 다만 marker 위 hover/drag는 신규 timeline UI에서 별도 구현 |
| Loop / Reverse toggle | `Source/Engine/Component/SkeletalMeshComponent.cpp:46-51` (loop은 fmod로 항상 ON, reverse는 음수 speed) | (UI 없음) | 단서만 발견 | 런타임 동작은 작동. UI toggle 없음 → ImGui::Checkbox 추가 + 현재 항상-loop인 거 끌 수 있게 멤버 추가 |
| Current time / frame 표시 | `Source/Editor/UI/EditorSkeletalMeshViewerWidget.cpp:1247` | slider format `%.3f` (시간만) | 확장 필요 | 프레임 번호 표시 신규 (`int Frame = Time * Clip.FrameRate`) |
| Anim Notify Track UI | (없음) | (없음) | 미발견 | 신규. ImGui DrawList 위에 marker rectangle 그리기 |
| Notify marker / name 표시 | (없음) | (없음) | 미발견 | 신규. `ImGui::IsMouseHoveringRect` + `ImGui::SetTooltip` 패턴 권장 |

---

## 파트 6 — Skinning Rendering / Debug View

현재 **CPU skinning 결과 → `RuntimeMeshBuffer` (dynamic VB) → 일반 PNCTT 정점**으로 흐르므로 GPU vertex가 BoneID/Weight를 가지지 않음(`Source/Engine/Render/Types/VertexTypes.h:36-43`). GPU skinning 도입 시 **새 vertex format + cbuffer/structured buffer + 새 shader + `SkinVerticesToReferencePose` virtual override**가 필요. 다행히 `SkinVerticesToReferencePose`가 virtual이라 **CPU/GPU 전환 hook 자체는 이미 존재**. Profiling 매크로(`SCOPE_STAT`/`GPU_SCOPE_STAT`)는 사전 정보대로 그대로 활용.

| 과제 세부 항목 | 관련 위치 (파일:라인) | 관련 심볼/클래스 | 연관성 분류 | 비고 |
|---|---|---|---|---|
| GPU Skinning | (없음) | (없음) | 미발견 | 신규. Shader + cbuffer + 새 vertex format 일체 신규 |
| CPU / GPU Skinning 전환 | `Source/Engine/Component/SkinnedMeshComponent.cpp:337-393` | `virtual void SkinVerticesToReferencePose()` | 확장 필요 | virtual 이미 있음 → derived class에서 GPU path override 가능. 또는 `bUseGPUSkinning` 분기 추가 |
| Bone Matrix Buffer | `Source/Engine/Render/Resource/Buffer.h` (FVertexBuffer/FIndexBuffer/FMeshBuffer 존재) | `FMeshBuffer::CreateDynamic` 패턴 참고 가능 | 단서만 발견 | StructuredBuffer / ConstantBuffer 래퍼는 별도 확인 필요. 같은 파일에 `FConstantBuffer`/`FStructuredBuffer` 존재할 가능성 → 신규 작성 시 동일 디렉터리 |
| GPU Skinning Shader | `Shaders/` 디렉터리 (탐색 결과 skinning 전용 shader 없음) | (없음) | 미발견 | 신규 작성. 새 vertex format(BoneID/Weight 포함)도 같이 추가 |
| Skinning Performance Stat | `Source/Engine/Profiling/Stats.h:143-152` (`SCOPE_STAT`) + `Source/Engine/Profiling/GPUProfiler.h:74-97` (`GPU_SCOPE_STAT`) | 매크로만 추가하면 측정됨 | 직접 사용 가능 | 사전 정보. CPU path는 `SCOPE_STAT("SkinVerticesToReferencePose")`, GPU path는 `GPU_SCOPE_STAT("GPU_Skinning")` |
| Bone Weight Heatmap | `Source/Engine/Debug/DebugDrawQueue.{h,cpp}` / `DrawDebugHelpers.{h,cpp}` (디렉터리 존재 확인) + `Source/Editor/UI/EditorSkeletalMeshViewerWidget.cpp:730+` (`UpdateBoneDebugLines`, `DrawDebugOctahedralBone`) | 디버그 라인 / 색상 visualization 인프라 | 단서만 발견 | 정점 색상 override나 별도 debug shader 신규 필요. 위치는 viewer 내부에 추가가 자연스러움 |

---

## 파트 7 — Crash Debug System

**MiniDump는 이미 완성**(`Source/Engine/Platform/CrashDump.{h,cpp}`). 다만 호출부(SEH `__try`/`__except` wrapper 또는 `SetUnhandledExceptionFilter`)가 어디서 wire되는지는 본 탐색에서 미확인 — 별도 grep 추가 권장. **콘솔 명령어 시스템(`IConsoleCommand`/`RegisterCommand`)은 완전 부재**. 단, `FEditorConsoleWidget`(in-game/editor console UI)은 존재하고 자체 명령 dispatch 갖춤. `CauseCrash` 같은 명령은 거기에 신규 등록 필요.

| 과제 세부 항목 | 관련 위치 (파일:라인) | 관련 심볼/클래스 | 연관성 분류 | 비고 |
|---|---|---|---|---|
| MiniDump | `Source/Engine/Platform/CrashDump.h:1-7`, `.cpp:1-52` | `WriteCrashDump(EXCEPTION_POINTERS*)` (uses `MiniDumpWriteDump` + `DbgHelp.lib` via pragma) | 직접 사용 가능 | 출력 경로: `KraftonEngine/Saves/Dump/Crash_YYYYMMDD_HHMMSS.dmp`. 타입은 `MiniDumpWithDataSegs` |
| Crash Report | `Source/Engine/Platform/CrashDump.cpp:42-48` | `MessageBoxW` 표시 + dump 경로 알림 | 직접 사용 가능 | 단순 `MessageBox`. Crash report uploader는 미존재 |
| `CauseCrash` 콘솔 명령어 | `Source/Editor/UI/EditorConsoleWidget.h:36-119` (`RegisterDefaultCommands` + 명령 입력 + autocomplete) | `FEditorConsoleWidget` 내부 명령 dispatch | 확장 필요 | 콘솔 명령 시스템 자체는 console widget 안에 있음. `CauseCrash` 명령을 거기에 register하면 됨. (별도 글로벌 console command framework 없음) |
| `.dmp` 저장 | `Source/Engine/Platform/CrashDump.cpp:23-40` | `MiniDumpWriteDump(...)` | 직접 사용 가능 | 사실상 (1)과 같은 코드 |
| `.pdb` 기반 callstack 확인 | `Source/Engine/Platform/CrashDump.cpp:1-2` (`#include <DbgHelp.h>` + `#pragma comment(lib, "DbgHelp.lib")`) | DbgHelp 링크는 되어있으나 `CaptureStackBackTrace` / `SymFromAddr` 호출은 미확인 | 단서만 발견 | DbgHelp 인프라는 있으나 callstack capture 코드 없음. dump 분석은 외부 도구(WinDbg/Visual Studio)로 .dmp + .pdb 열어서 보는 형태. 런타임 in-process callstack 출력은 신규 작성 필요 |

---

## 파트 8 — Reflection / Editor Property System

Reflection은 사전 정보대로 **사소한 매크로(`DECLARE_CLASS`/`DEFINE_CLASS`/`IMPLEMENT_CLASS`)**만 존재 — UClass 등록 + `IsA()` + `StaticClass()` 외엔 property metadata 없음. **Property reflection은 별도 경로**: `Source/Engine/Core/PropertyTypes.h`의 `FPropertyDescriptor`를 component가 `virtual void GetEditableProperties(TArray<FPropertyDescriptor>&)`로 직접 채워주는 **opt-in 방식** (UE의 codegen 없음). Editor의 `FEditorPropertyWidget`이 이 디스크립터를 읽어 ImGui control을 그림. **Animation asset(현재 raw struct)은 UObject가 아니라 이 시스템에 노출 안 됨** → `UAnimSequence` UObject 분리 후에야 details panel에 띄울 수 있음.

| 과제 세부 항목 | 관련 위치 (파일:라인) | 관련 심볼/클래스 | 연관성 분류 | 비고 |
|---|---|---|---|---|
| Property Reflection System | `Source/Engine/Core/PropertyTypes.h:1-62` + `Source/Engine/Object/Object.h:14-31` (`DECLARE_CLASS`/`DEFINE_CLASS`) + `Source/Engine/Object/UClass.h:16-56` + `Source/Engine/Object/ObjectFactory.h:7-53` (`IMPLEMENT_CLASS` + 문자열 기반 `FObjectFactory`) | `EPropertyType`, `FPropertyDescriptor`, `UClass`, `FClassRegistrar` | 확장 필요 | UClass 시스템 + `FPropertyDescriptor`는 **직접 사용 가능**. 단 codegen 없으므로 새 UObject 마다 `GetEditableProperties()` 수동 작성 필요 |
| Property metadata | `Source/Engine/Core/PropertyTypes.h:11-62` | `FPropertyDescriptor` 멤버 (Min/Max/Speed/Tooltip/Category/Flags/EnumNames) | 직접 사용 가능 | UE의 `meta=(ClampMin, ToolTip, Category, ...)` 동등 metadata 모두 존재 |
| Details Panel 노출 | `Source/Editor/UI/EditorPropertyWidget.{h,cpp}` (`RenderComponentTree`, `RenderDetails`, `RenderComponentProperties`, `RenderPropertyWidget`, `PropagatePropertyChange`) | `FEditorPropertyWidget` | 직접 사용 가능 | ImGui slider/input/checkbox/combo가 type별로 이미 매핑됨 |
| Animation asset 설정값 편집 | (대상 없음 — `FAnimationClip`/`UAnimSequence`가 현재 details panel 호출 대상 아님) | (없음) | 미발견 | `UAnimSequence` 분리(파트1) → `GetEditableProperties` 구현 → `EditorPropertyWidget`이 자동으로 그림. 분리 작업 후 자연 해결 |
| State Machine / Notify 설정값 편집 | (없음) | (없음) | 미발견 | State Machine / Notify 자체가 없으므로 신규(파트3) → 그 다음 동일 패턴(`GetEditableProperties`)으로 노출 |

---

## 탐색 중 의문점

- **`SkinVerticesToReferencePose`가 `virtual`인 이유**: GPU skinning을 미리 염두에 둔 hook인지, 아니면 단순한 OOP 관행인지 코드만으로는 단정 불가. 의도된 hook이라면 derived class 패턴이 자연스럽고, 아니라면 component 자체에 `bUseGPUSkinning` 분기가 더 깔끔. 사용자 의도 확인 필요.
- **`FAnimationClip`을 `UAnimSequence`로 분리할 때 `FSkeletalMesh` 내부 보유 vs reference**: 현재 `FSkeletalMesh::AnimationClips`가 monolithic. 완전 분리 시 기존 `.skm` asset 호환성(unversioned binary)이 깨짐 → migration tool이 필요한지, 아니면 신규 import만 분리 형태로 가는지 결정 필요.
- **Console command system 도입 범위**: `FEditorConsoleWidget` 내부 명령 dispatch가 있긴 하나 editor 전용. UE의 `IConsoleManager` 같은 글로벌 시스템이 이번 과제 범위인지(특히 `CauseCrash` 항목), 아니면 `EditorConsoleWidget`에 명령 등록만으로 충분한지.
- **Lua를 어디까지 노출**: 파트3에 "Lua Animation State Machine"이 있는데, 이게 (a) Lua DSL로 state graph 정의 / (b) Lua callback으로 transition condition 평가 / (c) 단순 anim playback 호출 중 어느 단계까지를 의미하는지 모호. sol2 인프라 자체는 충분.
- **Bone Animation Track 정밀도 업그레이드 여부**: 현재 `FBoneAnimSample`이 `FMatrix` 단일 저장 → element-wise lerp(rotation slerp 아님)로 affine drift 잠재. UE 스타일로 가면 보통 T/R/S 분리 + slerp인데, 이 변경이 이번 과제 범위인지 30fps 가정 유지인지 확인 필요.

---

## Verification

- 본 보고서는 **읽기 전용 탐색** 결과이며 코드 수정은 없다. 검증은 다음과 같이 가능:
  - 표의 각 파일/라인 번호를 IDE로 직접 열어 심볼 일치 확인.
  - "미발견" 분류 항목은 `rg -i "<keyword>" Source/`로 재검색하여 0건 확인.
  - 사전 정보와 다른 발견(파트2 Reverse Play 음수 speed 자동 처리, 파트4 viewer 인프라 완성도 등)은 해당 .cpp 파일의 명시 라인에서 직접 확인.
