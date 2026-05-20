# AnimNotify Infra Scan & Integration Design

> Read-only scan 결과 + Sound / CameraShake 두 가지 effect 한정 통합 설계안.
> 모든 인용은 `KraftonEngine/Source/` 기준 상대 경로.

## 1. Executive Summary

현재 엔진의 AnimNotify 인프라는 **데이터 모델 / 직렬화 / 시간 판정**까지는 갖춰져 있고, **에디터 UI에서 notify를 추가·이동·삭제·인라인 편집**하는 흐름도 동작한다. 그러나 두 군데가 비어 있다:

1. **타입 시스템 부재** — `FAnimNotifyEvent`는 `TriggerTime / Duration / NotifyName` 3개 필드만 가진 평탄(POD)한 struct다. `UAnimNotify` / `UAnimNotifyState` 류의 polymorphic base나 sub-class가 존재하지 않으며, asset reference(SoundId, ShakeParams 등) 같은 payload를 담을 자리가 없다.
2. **Runtime dispatch 없음** — `UAnimInstance::Update`는 매 프레임 `[PreviousTime, CurrentTime]` 사이의 trigger를 정확히 판정해 `TriggeredNotifiesThisFrame`(FName 배열)에 누적까지만 한다. 이 배열을 **읽어서 실제 효과를 실행하는 dispatcher가 C++에 없고**, 현재는 Lua 바인딩(`GetTriggeredNotifies`)으로 게임 코드/스크립트가 직접 polling하는 구조다.

SoundManager(`FSoundManager` singleton)와 CameraShake(`APlayerCameraManager::StartCameraShake` / `APlayerController::StartCameraShake` 래퍼)는 **이미 호출 가능한 형태로 존재**한다. 따라서 추가 작업의 본질은 "notify 항목에 effect payload를 붙일 수 있게 데이터 모델을 확장하고, runtime에서 그 payload를 dispatch하는 한 곳을 만드는 것"이다.

---

## 2. Layer-by-Layer 현황

### 2.1 Data Model

| 항목 | 상태 | 근거 |
|---|---|---|
| Notify base class (`UAnimNotify` 등) | **미존재** | `grep "UAnimNotify\|UNotify\|UAnimNotifyState"` 결과 0건 |
| Notify state base class | **미존재** | 동상 |
| Track 항목 struct (`FAnimNotifyEvent`) | **존재 (평탄형)** | [AnimNotify.h:18-47](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:18) |
| 필드 | `TriggerTime`(float), `Duration`(float), `NotifyName`(FName) 세 개뿐 | [AnimNotify.h:20-22](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:20) |
| Loop-aware 판정 | **존재** | `IsTriggeredBetween` — prev>curr wrap 케이스 처리 [AnimNotify.h:35-46](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:35) |
| 직렬화 연산자 | **존재** (struct 자체에 `operator<<`) | [AnimNotify.h:24-30](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:24) |
| Sub-class (Sound/CameraShake/Particle 등) | **미존재** | `grep "SoundNotify\|CameraShakeNotify\|ParticleNotify\|HitNotify"` 0건 |
| NotifyTrack(다중 트랙) struct | **미존재** | `grep NotifyTrack` 0건 — Notify는 `UAnimSequence` 한 곳의 단일 `TArray<FAnimNotifyEvent>`로 평탄 저장됨 |

**Notify 디렉토리에는 헤더 1개만 존재**: `Engine/Asset/Animation/Notify/AnimNotify.h` (단독). cpp 없음.

### 2.2 Asset Serialization

| 항목 | 상태 | 근거 |
|---|---|---|
| Sequence가 notify 보관 | **존재** | `UAnimSequence::Notifies : TArray<FAnimNotifyEvent>` [AnimSequence.h:121](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:121) |
| Mutator | `AddNotify` / `GetNotifies` / `GetMutableNotifies` | [AnimSequence.h:108-110](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:108) |
| Serialize 함수 | **존재**, asset header(magic/type/version) + body | [AnimSequence.cpp:28-60](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:28) |
| Notify 직렬화 호출 | `Ar << Notifies;` (`TArray` operator + `FAnimNotifyEvent::operator<<` 결합) | [AnimSequence.cpp:59](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:59) |
| AssetVersion | `3u` | [AnimSequence.h:98](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:98) |
| 포맷 | binary (`FArchive`) | header struct + 본문 |
| Editor → asset 쓰기 경로 | **존재** | `FUAnimSequenceDataSource::AddNotify/RemoveNotify/UpdateNotify` 가 `Sequence->GetMutableNotifies()`에 직접 write [AnimSequenceDataSource.cpp:49-123](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceDataSource.cpp:49) |

**한계**: 현재 직렬화 포맷은 (TriggerTime, Duration, NotifyName) 3개만 직렬화한다. Sound / Shake payload를 더하려면 `FAnimNotifyEvent::operator<<`와 `AssetVersion` 둘 다 손봐야 한다(이번 task에서는 변경하지 않음).

### 2.3 Editor UI (AnimSequenceEditorTab)

| 항목 | 상태 | 근거 |
|---|---|---|
| 탭 클래스 | **존재** `FAnimSequenceEditorTab` | [AnimSequenceEditorTab.h:11](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.h:11) |
| Timeline UI | **존재** ImGui 기반, ruler / notify row / curve row / additive row / attribute row (RowCount=4) | [AnimSequenceEditorTab.cpp:395](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:395), [.cpp:427](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:427) |
| Notify 마커 표시 | **존재** | [.cpp:551-590](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:551) |
| Notify hit-test / 선택 / drag 이동 | **존재** `HitTestNotify` / `DraggingNotifyIndex` | [.cpp:433-475](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:433) |
| 컨텍스트 메뉴 "Add Notify at current frame" | **존재** (이름=`"Notify"`, Duration=0인 instant만 추가) | [.cpp:635-643](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:635) |
| 컨텍스트 메뉴 "Delete Notify" | **존재** | [.cpp:628-632](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:628) |
| 인라인 속성 편집 | **존재** Name / Trigger slider / Duration slider / Color / Delete | [.cpp:768-839](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:768) |
| **타입 선택 UI** | **미존재** — notify에 "type" 개념 자체가 없음 | UI 어디에도 type combo / asset picker / payload 입력 위젯 없음 |
| **Asset reference 입력 UI** | **미존재** | SoundId / ShakeParams 같은 필드를 표시·편집할 자리 없음 |
| Data source adapter | `FUAnimSequenceDataSource` (asset → 에디터용 `FAnimNotifyEntry`) | [AnimSequenceDataSource.cpp:49-123](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceDataSource.cpp:49) |
| 에디터 임시 struct `FAnimNotifyEntry` | Name, TriggerTime, Duration, ColorPacked | [AnimSequenceDataSource.h:12-18](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceDataSource.h:12) |

UI 흐름은 잘 잡혀 있고 add/remove/move/inline-edit이 작동한다. 빠진 것은 "타입"과 "타입별 payload"다.

### 2.4 Runtime Trigger

| 항목 | 상태 | 근거 |
|---|---|---|
| Update tick 진입점 | **존재** `UAnimInstance::Update(DeltaTime)` | [AnimInstance.cpp:23-85](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:23) |
| Prev/Curr 시간 추적 | **존재** `PreviousTime = CurrentTime; ... CurrentTime = NewTime(or wrapped)` | [.cpp:43-71](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:43) |
| Loop wrap 처리 | **존재** | [.cpp:46-54](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:46) |
| Notify 판정 (`prev..curr` 구간) | **존재** | [.cpp:74-84](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:74), `IsTriggeredBetween` 호출 |
| 활성 notify 배열 노출 | **존재** virtual hook `GetActiveNotifies()` | [AnimInstance.h:83](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:83) |
| Single-node 구현 | **존재** `UAnimSingleNodeInstance::GetActiveNotifies()` → `CurrentSequence->GetNotifies()` | [AnimSingleNodeInstance.cpp:75-78](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:75) |
| Trigger 결과 보관 | **존재** `TriggeredNotifiesThisFrame : TArray<FName>` | [AnimInstance.h:46,93](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:46) |
| **C++ Dispatcher (notify → 효과 호출)** | **미존재** — 누적된 FName 배열을 읽어 SoundManager/Camera로 보내는 코드 없음 | grep 결과 `TriggeredNotifiesThisFrame` 사용처는 ① AnimInstance 내부 push/clear, ② Lua binding 노출(`GetTriggeredNotifies`) — 두 곳뿐 [LuaSkeletalMeshBindings.cpp:456-476](KraftonEngine/Source/Engine/Scripting/LuaSkeletalMeshBindings.cpp:456) |
| Reverse 재생 처리 | **부분** `PlaybackSpeed`는 음수도 받지만 `IsTriggeredBetween`은 `prev<=curr`/`prev>curr` 두 분기로 forward wrap만 정확히 처리. 음의 진행 방향에서 wrap이 끼면 결과는 미정의(검증되지 않음). |
| Seek (직접 시간 set) 처리 | **부분** `SetEvaluationTime`은 prev=curr로 강제 동기화하므로 [AnimInstance.h:62](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:62), seek 직후 한 프레임은 notify가 발화되지 않는다 (의도된 보호 같음). |

핵심: **판정까지는 완성, dispatch는 없음.** SequencePlayer::Evaluate는 별개로 TRS/Slerp만 본다(이번 scan에서 별도 검증). Notify dispatch는 `EvaluateGraph`가 아니라 `Update()`의 끝부분 — 그 자리에 한 줄 hook이 필요하다.

### 2.5 Effect Dispatch Target

#### SoundManager — **존재, 사용법 명확**

- 위치: `Engine/Sound/SoundManager.{h,cpp}` [SoundManager.h:9](KraftonEngine/Source/Engine/Sound/SoundManager.h:9)
- 패턴: `TSingleton<FSoundManager>` — `FSoundManager::Get()` 로 접근.
- 이미 `Engine/Runtime/Engine.h`가 `#include "Sound/SoundManager.h"` 함 — Engine 모듈 어디서나 사용 가능.
- 모델: **ID 기반 (FSoundId = FString)**. asset path가 아니라 사전 로드된 ID로 재생.
- 주요 API:
  - `void LoadEffect(const FSoundId& ID, const std::wstring& FilePath)` — 효과음 사전 로드
  - `void PlayEffect(const FSoundId& ID)` — 재생 (loaded 아니면 UE_LOG 후 no-op) [SoundManager.cpp:97-107](KraftonEngine/Source/Engine/Sound/SoundManager.cpp:97)
  - `void StopEffect(const FSoundId& ID)`
  - `bool IsEffectPlaying(const FSoundId& ID) const`
  - `float GetEffectDuration(const FSoundId& ID) const`
  - 음악(Music) API도 별도 존재 — 본 task 범위 외.
- 백엔드: SFML(`sf::Sound`, `sf::SoundBuffer`).

**노티가이 호출 시 필요한 입력**: `FSoundId`(=FString) 하나. 볼륨/피치 등 파라미터는 현재 API에 없음 — 필요하면 SoundManager에 추가하거나 notify payload에서 SFML 객체 호출까지 풀어내야 함. 본 설계에서는 기본 PlayEffect만 사용.

#### Camera Shake — **존재**

- 카메라 모델: `APlayerCameraManager` 액터 + `UCameraModifier` 체인.
- 셰이크 modifier: `UCameraShakeModifier` ([CameraShakeModifier.h:34](KraftonEngine/Source/Engine/Camera/CameraShakeModifier.h:34))
- 파라미터 struct **`FCameraShakeParams`** ([CameraShakeModifier.h:12-32](KraftonEngine/Source/Engine/Camera/CameraShakeModifier.h:12)):
  - `ECameraShakePattern Pattern` (Sine / Perlin)
  - `float Duration` (기본 2.5)
  - `float BlendInTime, BlendOutTime`
  - `FVector LocationAmplitude`
  - `FRotator RotationAmplitude`
  - `float FOVAmplitude`
  - `float Frequency, Roughness`
  - `bool bApplyInCameraLocalSpace, bSingleInstance`
  - `uint32 Seed`
- Trigger API 두 단계:
  - Low-level: `UCameraShakeModifier* APlayerCameraManager::StartCameraShake(const FCameraShakeParams& Params)` [PlayerCameraManager.h:73](KraftonEngine/Source/Engine/Camera/PlayerCameraManager.h:73)
  - High-level 래퍼: `void APlayerController::StartCameraShake(Duration, LocationAmp, RotationAmp, Frequency, FOVAmplitude=0, bSingleInstance=false)` [PlayerController.h:47](KraftonEngine/Source/Engine/GameFramework/PlayerController.h:47), [PlayerController.cpp:336-362](KraftonEngine/Source/Engine/GameFramework/PlayerController.cpp:336)
- 실사용 예: [ParryComponent.cpp:237](KraftonEngine/Source/Games/Crossy/Components/ParryComponent.cpp:237) — `PC->StartCameraShake(0.2f, 0.12f, 1.2f, 40.0f);`
- 호출 가능 위치: `PlayerCameraManager.h`는 이미 `Engine/GameFramework/PlayerController.h`에서 include 되고, World에서 PlayerController에 접근 가능. Animation 코드에서 World로 가는 경로는 별도 확인 필요(7번 Open Questions 참조).

---

## 3. Gap Analysis

| Layer | 필요 작업 | 현재 상태 | 우선순위 |
|---|---|---|---|
| 1. Data Model | `FAnimNotifyEvent`에 타입/페이로드 추가 (또는 polymorphic 구조 도입) | 평탄 struct만 존재 | **P0** |
| 2. Serialize | 새 필드용 `operator<<` 갱신 + AssetVersion 4로 bump | v3 / 3필드만 직렬화 | **P0** |
| 3. Editor — 타입 선택 UI | "Add Notify" 시 타입 선택 (Sound/CameraShake) + 타입별 payload 위젯 | 평탄한 Name/Time/Duration만 편집 | **P0** |
| 3. Editor — Asset 입력 | SoundId 입력(text or asset picker), ShakeParams 편집기 | 없음 | **P0** |
| 4. Runtime — Dispatcher | `Update` 말미에 `TriggeredNotifiesThisFrame`(또는 더 풍부한 이벤트 리스트)을 보고 effect를 실행 | 없음 (Lua만 polling) | **P0** |
| 4. Runtime — Reverse/Seek 처리 | 음수 재생, seek 후 한 프레임 무발화 정책 정리 | 부분/미정의 | **P2** (이번 범위 외 가능) |
| 5. SoundManager | 그대로 사용 (ID 등록 후 `PlayEffect`) | 사용 가능 | — |
| 5. Camera Shake | 그대로 사용 (`APlayerCameraManager::StartCameraShake(FCameraShakeParams)`) | 사용 가능 | — |
| 통합 — 의존성 경로 | AnimInstance에서 World/PlayerController/SoundManager에 접근하는 방법 결정 | 미정 | **P1** |

---

## 4. 사용자 가설 vs 실제 Code 차이

| 사용자 진술 | 실제 scan 결과 | 일치 여부 |
|---|---|---|
| "AnimNotify의 data struct / track 일부는 존재" | `FAnimNotifyEvent` struct는 존재. 단 **"track"이라는 다중 행 구조는 없음** — Notify는 `UAnimSequence::Notifies` 하나의 평탄 배열에 통째로 저장됨. 또한 polymorphic class 계층 없음. | **부분 일치** |
| "editor에서 logic을 binding하는 dispatch는 없음" | **일치.** Runtime 측에서 `TriggeredNotifiesThisFrame`(FName 배열) 까지만 채우고, C++에서 SoundManager/Camera로 보내는 호출이 grep 결과 **0건**. Lua 바인딩이 polling 인터페이스로만 노출. | **일치** |
| "SoundManager는 singleton으로 존재" | **일치.** `TSingleton<FSoundManager>` 패턴, 단 효과음은 **ID 기반 사전 로드** 모델 — asset path를 노티에 직접 박는 게 아니라 `FSoundId(FString)`를 식별자로 쓴다. | **일치 (사용 모델 보강 필요)** |
| "Camera system은 존재할 가능성 높으나 확인 필요" | **존재.** `APlayerCameraManager` + `UCameraShakeModifier` + `FCameraShakeParams`. 트리거 API 2단(Manager 직접 호출 / Controller 래퍼) 모두 구현됨. 실사용 예 ParryComponent에 있음. | **존재 확정** |

추가로 발견된 사실 (사용자 진술에 없던 항목):
- Notify의 **시간 판정 코드(`IsTriggeredBetween`)는 loop wrap을 정확히 처리**하도록 이미 구현되어 있음. 따라서 dispatch만 붙이면 instant notify는 즉시 동작 가능.
- 에디터에는 **inline property editor가 이미 존재**하므로 타입별 payload UI는 그 자리에 끼워 넣으면 됨 (`RenderNotifyPropertyInline` [.cpp:768](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:768)).

---

## 5. 통합 설계안 (Sound & CameraShake 한정)

> 본 설계는 "어디에 무엇이 들어가야 하는가"의 청사진이다. 코드 변경은 후속 task에서 별도로 수행한다.

### 5.1 Notify 타입 정의

엔진이 polymorphic `UAnimNotify` 계층을 갖고 있지 않다는 점을 감안해, **두 가지 선택지** 중 하나를 택해야 한다 (이 task의 산출물은 청사진이므로 선택은 후속 결정에 위임 — Open Questions Q1 참조).

**옵션 A: `FAnimNotifyEvent`를 union/variant로 확장** (가벼움, 권장 후보)
```
enum class EAnimNotifyType : uint8 { None, Sound, CameraShake };

struct FAnimNotifyEvent
{
    float TriggerTime;
    float Duration;
    FName NotifyName;
    EAnimNotifyType Type;       // ← 추가
    // tagged payload — Sound와 CameraShake가 충돌하지 않으므로 flat field로 묶어도 무방
    FString SoundId;                   // Type == Sound 일 때만 의미 있음
    FCameraShakeParams ShakeParams;    // Type == CameraShake 일 때만 의미 있음
};
```
장점: 기존 `TArray<FAnimNotifyEvent>` 구조, serialize 흐름, hit-test, dragging UI를 거의 그대로 사용. AssetVersion만 4로 bump.
단점: type별 payload가 늘 메모리에 남음(가벼우므로 무시 가능 수준).

**옵션 B: polymorphic `UAnimNotifyBase` 도입**
- `USoundNotify : UAnimNotifyBase { FString SoundId; }`
- `UCameraShakeNotify : UAnimNotifyBase { FCameraShakeParams Params; }`
- `UAnimSequence::Notifies : TArray<UAnimNotifyBase*>`
- 단점: UObject ownership/serialize/팩토리/에디터 type picker 등 인프라가 무겁고, 본 엔진의 UObject 직렬화에 polymorphic pointer 컬렉션 패턴이 이미 자리 잡았는지 별도 검증 필요(Open Questions Q2).

**권장: 옵션 A로 시작.** Sound + CameraShake 두 타입만 다루는 범위에서는 polymorphic 인프라를 새로 깔 만한 ROI가 낮다. 타입이 셋 이상으로 늘어나는 시점에 옵션 B로 마이그레이션.

### 5.2 Editor 편집 흐름

기존 timeline UI에 다음 두 곳을 손본다 (코드 변경은 후속 task):

1. **"Add Notify at current frame" 메뉴** ([AnimSequenceEditorTab.cpp:635](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:635))
   - 현재: 메뉴 한 개 → `FAnimNotifyEntry` 생성 (이름 "Notify", Duration 0)
   - 변경 후: 서브메뉴 또는 분기 메뉴 → "Add Sound Notify" / "Add Camera Shake Notify" 두 개. 각 항목 클릭 시 `EAnimNotifyType`을 미리 채운 entry를 추가.
2. **`RenderNotifyPropertyInline`** ([AnimSequenceEditorTab.cpp:768](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:768))
   - 기존 Name / Trigger / Duration / Color / Delete 그대로 유지.
   - 그 아래에 `Edited.Type`에 따른 payload 위젯 분기:
     - Sound: `InputText("SoundId", ...)` (FSoundId는 FString이므로 그냥 텍스트 입력; 향후 asset picker로 승격 가능)
     - CameraShake: `FCameraShakeParams` 필드들 (Duration, LocationAmp xyz, RotationAmp roll/pitch/yaw, Frequency, Roughness, BlendIn/Out, FOVAmplitude) 슬라이더/InputFloat
   - 변경 시 기존 `DataSource->UpdateNotify(...)` 흐름 그대로 사용.
3. `IAnimSequenceDataSource`와 `FAnimNotifyEntry`에도 동일 필드 추가 — 어댑터가 asset의 `FAnimNotifyEvent`와 변환할 수 있도록 [AnimSequenceDataSource.h:12](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceDataSource.h:12), [AnimSequenceDataSource.cpp:49-123](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceDataSource.cpp:49) 양쪽.

### 5.3 Runtime 발화 흐름

발화 지점 선택:

- **A안 (권장)**: `UAnimInstance::Update`의 notify 판정 직후, 같은 함수 안에서 dispatch 호출 추가.
  - 위치: [AnimInstance.cpp:74-84](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:74) 의 for 루프 안 또는 직후.
  - 발화 방법: `Notify`(이미 잡아 둔 ref)로부터 type을 보고 분기.
- B안: dispatch를 별도 함수로 빼서 `Update` 끝에서 호출. 같은 결과지만 책임 분리가 명확.

Dispatch 분기 의사 코드(설계용, 구현 아님):
```
switch (Notify.Type)
{
case EAnimNotifyType::Sound:
    FSoundManager::Get().PlayEffect(Notify.SoundId);
    break;
case EAnimNotifyType::CameraShake:
    // 카메라 매니저로 가는 경로는 5.3-(b) 참조
    if (APlayerCameraManager* CamMgr = ResolveCameraManager()) {
        CamMgr->StartCameraShake(Notify.ShakeParams);
    }
    break;
}
```

**의존성 방향**:
- `Engine/Asset/Animation/Core/AnimInstance.cpp`에서 `Sound/SoundManager.h` include → OK (이미 `Engine/Runtime/Engine.h`가 같은 헤더를 include 중. 같은 Engine 모듈 내 이동이므로 순환 위험 없음).
- `Engine/Asset/Animation/Core/AnimInstance.cpp`에서 `Camera/PlayerCameraManager.h` include → OK (Camera는 Engine 모듈 하위). 단 **AnimInstance가 World/PlayerController로 가는 경로**가 명확하지 않음:
  - **(b)** `UAnimInstance`는 UObject지만 outer 체인을 통해 `USkeletalMeshComponent` → owning `AActor` → `UWorld` → `APlayerController` → `APlayerCameraManager`로 거슬러 올라가야 한다. 이 체인 helper가 이미 있는지는 별도 grep으로 확인 필요(Open Questions Q3).
  - 대안: AnimInstance에 `TFunction<void(const FAnimNotifyEvent&)>` 콜백을 외부(Component)에서 주입하고, 컴포넌트 측에서 World 조회 후 dispatch — 의존성 역전.
- 결론: **인터페이스 / 콜백 주입** (대안)이 깔끔하지만, 일단 효과가 두 종뿐이므로 **직접 호출(A안)** 도 받아들일 수 있다. 어느 쪽을 택할지는 의존성 정책에 따른 결정(Open Questions Q3).

### 5.4 Asset 저장 흐름

1. 에디터에서 사용자가 notify를 추가/수정 → `FUAnimSequenceDataSource::AddNotify/UpdateNotify`가 호출됨 (이미 존재).
2. 어댑터는 `FAnimNotifyEntry`(에디터용) → `FAnimNotifyEvent`(asset용)로 변환 후 `Sequence->GetMutableNotifies()`에 write — 이번 설계에서 type/payload 필드도 함께 옮기도록 확장한다.
3. 사용자가 asset 저장을 명령(기존 SaveAs 흐름, `animsequence_asset_saveas_design.md` 참조 — 본 task에서는 미검증)하면 `UAnimSequence::Serialize`가 호출되어 `Ar << Notifies` 가 새 `FAnimNotifyEvent::operator<<` 를 통해 type+payload까지 binary로 기록.
4. `AssetVersion`을 4로 올리고, load 시 v3 파일은 type=None(또는 legacy)으로 백필하거나 거부하는 정책을 결정해야 함 (Open Questions Q4).

### 5.5 Sequence Diagram (텍스트)

사용자 클릭(Add Sound Notify) → AnimSequenceEditorTab 컨텍스트 메뉴 분기 → DataSource.AddNotify(type=Sound, payload) → UAnimSequence.Notifies에 push (메모리) → 사용자 Save → UAnimSequence::Serialize → 디스크 .animseq → 게임 런타임에서 mesh 어태치 → USkeletalMeshComponent → UAnimInstance::Update(dt) → IsTriggeredBetween 매칭 → (확장된) Dispatch 분기 → FSoundManager::Get().PlayEffect(SoundId) 또는 APlayerCameraManager::StartCameraShake(ShakeParams).

---

## 6. Scope 경계 확인

본 설계가 **다루지 않는 것** (의도적으로 범위 밖):

- 다른 notify 타입: Particle, Hit, Event(BP-like), Sync marker, Footstep, MontageEnd 등 — 모두 범위 외. 옵션 A 설계는 type enum에 새 값을 추가하면 확장 가능하다는 점만 명시.
- `UAnimNotifyState`(범위형 notify의 begin/tick/end 콜백) — 본 설계의 Duration은 단순 범위 메타데이터로만 다루고, "state notify의 tick 콜백"은 구현하지 않는다.
- Network replication — 본 엔진의 replication 인프라는 이번 scan에서 검토하지 않음.
- Undo / Redo — 에디터 다른 패널의 undo 인프라 유무 자체 미확인.
- 동시에 같은 타입 여러 notify가 겹쳤을 때의 충돌(예: 같은 frame에 Sound 두 개)은 dispatch 측에서 별도 조정 안 함 — 둘 다 발화.
- AnimSequence 외의 컨테이너(예: AnimMontage 같은 상위 자산)는 본 엔진에 없거나 미확인이므로 다루지 않음.
- Reverse 재생 / 임의 seek 시의 정확한 발화 정책 — 4번 Layer scan에서 "부분/미정의"로 기록.
- AnimGraph state machine 노드의 `OnNotify` 전이 조건([AnimGraph_StateMachine.h:36](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:36)) — "B2에서 평가 false"라는 코멘트가 달려 있어 현재 미사용. 본 설계와 직교하므로 다루지 않음.

---

## 7. Open Questions

1. **Q1 (Data model 선택)**: 옵션 A(평탄 struct + type enum + flat payload)와 옵션 B(polymorphic UObject 계층) 중 어느 쪽? 본 문서의 권고는 A. 결정자: 사용자.
2. **Q2 (옵션 B를 택할 경우)**: 본 엔진의 UObject 직렬화가 `TArray<UObject*>`의 동적 타입 보존을 지원하는가? `UAnimDataModel`이 별도 객체로 분리돼 `Serialize`를 직접 호출받는 패턴([AnimSequence.cpp:48-57](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:48))은 있지만, **컬렉션의 dynamic dispatch 직렬화**는 본 scan에서 확인 안 됨.
3. **Q3 (AnimInstance → CameraManager 접근 경로)**: `UAnimInstance`에서 owning `USkeletalMeshComponent` / `AActor` / `UWorld` / `APlayerController` 로 거슬러 올라가는 helper가 기존에 있는가? 없다면 콜백 주입(컴포넌트가 dispatcher를 owns)이 의존성 면에서 깔끔하다. 두 접근 중 사용자 정책 필요.
4. **Q4 (AssetVersion 호환)**: 현재 v3로 저장된 .animseq 파일(있다면)을 v4로 어떻게 처리할 것인가? (a) 기존 파일은 type=None으로 백필하여 dispatch 시 no-op (b) 로드 거부 (c) migration tool 별도 작성. 우선은 (a)가 안전.
5. **Q5 (SoundId 입력 UX)**: 효과음은 "사전에 `LoadEffect`로 등록된 ID" 모델이다. 에디터에서 단순 FString 입력만 받을 것인지, 아니면 별도의 sound asset/registry browser를 만들 것인지? 이 task 범위에서 결정 불필요 — 1차에는 raw 텍스트 입력으로 충분.
6. **Q6 (사운드 ID의 등록 시점)**: 게임/엔진 부팅 시 `LoadEffect`로 등록되어야 `PlayEffect`가 동작한다. notify 발화 시점에 ID가 미등록이면 현재 SoundManager는 UE_LOG만 남기고 silently no-op([SoundManager.cpp:100-103](KraftonEngine/Source/Engine/Sound/SoundManager.cpp:100)) — 이 동작이 허용 가능한지 확인 필요.
7. **Q7 (Reverse/Seek 정확 발화 정책)**: `PlaybackSpeed < 0` 또는 `SetEvaluationTime`로 점프할 때 notify 발화 정책을 이번 작업에서 손볼지, 별도로 미룰지 결정 필요.

---

### Appendix. Scan으로 확인한 grep 결과 요약

- `grep "UAnimNotify\|UNotify\|UAnimNotifyState"` → 0건
- `grep "SoundNotify\|CameraShakeNotify\|ParticleNotify\|HitNotify"` → 0건
- `grep "NotifyTrack"` → 0건
- `grep "FAnimNotifyEvent\|NotifyTrack\|FNotify"` → 9개 파일 (모두 위 본문에서 인용)
- `grep "TriggeredNotifiesThisFrame\|GetTriggeredNotifies"` → AnimInstance 내부 + Lua 바인딩 1곳뿐
- `grep "StartCameraShake"` → PlayerController/PlayerCameraManager/Lua 바인딩/ParryComponent 사용처 확인

(끝)
