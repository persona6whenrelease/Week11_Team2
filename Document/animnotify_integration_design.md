# AnimNotify Integration Design (Confirmed)

## 0. Reference

- Scan report: [Document/animnotify_infra_scan.md](Document/animnotify_infra_scan.md)
- 본 문서는 위 scan의 후속이며, 사용자 확정 9개 결정사항(D1~D9)을 적용한 **구현 직전 확정 설계안**이다.
- 코드 인용 경로는 모두 `KraftonEngine/Source/` 기준 상대 경로.

---

## 1. Decision Summary (사용자 확정 9개)

| # | 결정 | 의미 |
|---|---|---|
| D1 | **옵션 A — flat struct + enum** | `FAnimNotifyEvent`에 `EAnimNotifyType Type`, `FString SoundId`, `FCameraShakeParams ShakeParams` 추가. polymorphic 계층 도입 안 함. |
| D2 | **(a) 직접 호출** | `UAnimInstance`가 outer chain(Component → Actor → World → PlayerController → CameraManager / Singleton SoundManager) 직접 사용. 콜백 주입 안 함. |
| D3 | **(a) v3 백필** | v3 파일 로드 시 모든 notify를 `Type=None`으로 채움. dispatch에서 `Type==None`이면 skip. `AssetVersion`은 4로 bump. |
| D4 | **B안 — 별도 함수 분리** | `UAnimInstance::Update` 안에서 dispatch를 별도 함수로 분리하여 호출 (`DispatchTriggeredNotifies()`). |
| D5 | **(a) 텍스트 입력** | 에디터에서 `SoundId`는 `ImGui::InputText`로 입력. Asset picker는 추후 작업. |
| D6 | **(a) silent no-op** | 미등록 `SoundId` 호출 시 `FSoundManager`의 기존 `UE_LOG + no-op` 동작 그대로 둠. |
| D7 | **(a) 범위 외** | Reverse 재생 / `SetEvaluationTime` seek 후 발화 정확성은 본 작업 범위 외. forward 정상 재생만 검증. |
| D8 | **(a) label 유지** | `NotifyName : FName`은 사용자가 정한 식별 label로 유지 (예: `"Footstep_L"`). dispatch와 직접 연결 X — dispatch는 `Type`으로만 분기. |
| D9 | **(a) 2-layer 유지** | `FAnimNotifyEntry`(에디터용)와 `FAnimNotifyEvent`(asset용) 둘 다 type/payload 필드를 가짐. 어댑터에서 변환. |

---

## 2. Scope

**다루는 것** — Sound, CameraShake 두 effect 타입에 대한 다음 5계층:

1. Data model 확장 (`FAnimNotifyEvent` + `EAnimNotifyType` + payload 필드)
2. 직렬화 (operator<<, AssetVersion 4, v3 백필)
3. Editor UI (Add 메뉴 분기, payload 위젯, DataSource adapter 양방향 변환)
4. Runtime dispatch (`DispatchTriggeredNotifies()` 신설 + outer chain 호출)
5. Effect target 호출 (`FSoundManager::PlayEffect`, `APlayerCameraManager::StartCameraShake`)

**다루지 않는 것** — scan report [Section 6](Document/animnotify_infra_scan.md) 그대로 + 추가:

- 콜백 주입 / 의존성 역전 패턴 (D2에서 직접 호출 선택했으므로 미적용)
- Sound asset picker (D5에서 텍스트 입력 선택)
- 미등록 SoundId 사전 검증 UI (D6에서 silent no-op 유지)
- Reverse / Seek 시 발화 정확성 (D7에서 범위 외)
- Particle / Hit / Event / Sync / UAnimNotifyState — 본 설계의 `EAnimNotifyType`에 enum 값을 추가하면 확장 가능하다는 점만 명시.

---

## 3. Data Model 확정안

### 3.1 `FAnimNotifyEvent` 변경 후 모습

위치: [Engine/Asset/Animation/Notify/AnimNotify.h:18-47](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:18)

| 필드 | 타입 | 의미 | 현재 / 변경 |
|---|---|---|---|
| `TriggerTime` | `float` | sequence 내 발화 시각(초) | 유지 |
| `Duration` | `float` | state notify 범위 길이(초). 0이면 instant. | 유지 (시맨틱 그대로 — D8과 무관) |
| `NotifyName` | `FName` | 사용자 label (예: `"Footstep_L"`) | 유지. **dispatch와 직접 연결 X** (D8). |
| `Type` | `EAnimNotifyType` *(신규)* | 어떤 effect 타입인가. dispatch 분기 키. | 신규 |
| `SoundId` | `FString` *(신규)* | `Type==Sound`일 때 의미. `FSoundId = FString`이므로 그대로 호환. | 신규 |
| `ShakeParams` | `FCameraShakeParams` *(신규)* | `Type==CameraShake`일 때 의미. | 신규 |

**Type별 활성 payload 매핑**:

| Type | 의미 있는 payload | 무시 필드 |
|---|---|---|
| `None` | (없음 — v3 백필 결과, dispatch skip) | `SoundId`, `ShakeParams` |
| `Sound` | `SoundId` | `ShakeParams` |
| `CameraShake` | `ShakeParams` | `SoundId` |

(`SoundId`와 `ShakeParams`는 항상 멤버로 존재하지만 Type에 맞지 않으면 dispatch에서 읽지 않음 — 메모리 오버헤드는 `FString` empty + `FCameraShakeParams`(POD, ~64B) 수준으로 무시 가능.)

### 3.2 `EAnimNotifyType` 정의

```
enum class EAnimNotifyType : uint8
{
    None = 0,        // v3 백필용. dispatch skip.
    Sound = 1,
    CameraShake = 2,
};
```

- 직렬화 시 `uint8`로 저장한다 (forward-compat: 254개까지 enum 추가 가능).
- 새 타입을 추가하면 enum 끝에 append만 한다 — 기존 v4 파일의 값 안정성을 위해 **중간 삽입 금지**.

### 3.3 `operator<<` 변경

위치: [Engine/Asset/Animation/Notify/AnimNotify.h:24-30](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:24)

**핵심**: `FAnimNotifyEvent`의 `operator<<`는 archive에 *version*을 모르므로, v3/v4 분기는 **상위(=`UAnimSequence::Serialize`)에서 처리**한다. struct 자체의 operator는 v4 포맷(모든 새 필드 포함)으로만 동작한다.

새 v4 직렬화 순서(설계 명세, 구현 코드 아님):

```
TriggerTime → Duration → NotifyName → (uint8)Type → SoundId → ShakeParams
```

`FCameraShakeParams`는 현재 `operator<<`가 없으므로 직렬화 함수가 신설되어야 한다 — 13개 필드(enum 1 + float 9 + FVector 1 + FRotator 1 + bool 2 + uint32 1) 순서를 [CameraShakeModifier.h:12-32](KraftonEngine/Source/Engine/Camera/CameraShakeModifier.h:12) 그대로 따른다. (구현 시 새 헤더에 정의하되, 의존성 방향이 깨지지 않게 `Notify/AnimNotify.h` 가 `Camera/CameraShakeModifier.h`(또는 그 일부)를 include하는 형태가 된다 — 5.4 의존성 메모 참조.)

---

## 4. Verify Result — Outer Chain 1회성 검증

| 단계 | 검증 내용 | 결과 | 근거 |
|---|---|---|---|
| 1 | `UAnimInstance` → owning `USkeletalMeshComponent` | ⚠️ **다른 이름 (UObject Outer 체인 사용)** | `UAnimInstance`에 back-pointer 멤버는 **없음**. 대신 `UObject::GetTypedOuter<T>()`가 존재 [Object.h:51-63](KraftonEngine/Source/Engine/Object/Object.h:51) — UE의 `GetTypedOuter<T>`와 동일 시맨틱. AnimInstance는 `UObjectManager::Get().CreateObject<...>(this)`로 컴포넌트를 Outer로 삼아 생성됨 [SkeletalMeshComponent.cpp:139-146](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:139). 따라서 `GetTypedOuter<USkeletalMeshComponent>()` 호출로 owning 컴포넌트 획득 가능. |
| 2 | `USkeletalMeshComponent` → `AActor` | ✅ | `UActorComponent::GetOwner() const` returns `AActor*` [ActorComponent.h:53](KraftonEngine/Source/Engine/Component/ActorComponent.h:53). `USkeletalMeshComponent`는 `UActorComponent` 후손이므로 그대로 사용. |
| 3 | `AActor` → `UWorld` | ✅ | `UWorld* AActor::GetWorld() const` 선언 [AActor.h:89](KraftonEngine/Source/Engine/GameFramework/AActor.h:89), 정의 [AActor.cpp:162](KraftonEngine/Source/Engine/GameFramework/AActor.cpp:162). |
| 4 | `UWorld` → `APlayerController` | ⚠️ **다른 이름** | `GetFirstPlayerController`는 없음. **`UWorld::GetPlayerController(int32 Index = 0) const`** [World.h:111](KraftonEngine/Source/Engine/GameFramework/World.h:111)가 정확한 대체. `Index=0` 호출로 첫 번째 컨트롤러 획득. 다중 컨트롤러는 `GetPlayerControllers()` [World.h:112](KraftonEngine/Source/Engine/GameFramework/World.h:112)로도 노출됨. 본 task에서는 인덱스 0만 사용. |
| 5 | `APlayerController` → `APlayerCameraManager` | ✅ | `APlayerController::GetCameraManager()` (ref), `GetCameraManagerPtr()` (nullable), `EnsureCameraManager()` (lazy create) 모두 존재 [PlayerController.h:61-76](KraftonEngine/Source/Engine/GameFramework/PlayerController.h:61). nullptr 안전성을 위해 `GetCameraManagerPtr()` 사용 권장. |
| 6 | `APlayerCameraManager::StartCameraShake(FCameraShakeParams)` signature | ✅ | `UCameraShakeModifier* APlayerCameraManager::StartCameraShake(const FCameraShakeParams& Params)` 선언 [PlayerCameraManager.h:73](KraftonEngine/Source/Engine/Camera/PlayerCameraManager.h:73), 정의 [PlayerCameraManager.cpp:418](KraftonEngine/Source/Engine/Camera/PlayerCameraManager.cpp:418). 반환값은 무시해도 무방. |
| 7 | `FSoundManager::Get()`이 `UAnimInstance` 컴파일 단위에서 유효 | ✅ | `FSoundManager : TSingleton<FSoundManager>` [SoundManager.h:9](KraftonEngine/Source/Engine/Sound/SoundManager.h:9). `Engine/Runtime/Engine.h` 가 이미 같은 헤더 include함 — 같은 Engine 모듈 내 이동이라 `AnimInstance.cpp`에서 `#include "Sound/SoundManager.h"` 추가는 자유롭고 순환 위험 없음. |

**판정**: ❌ 없음. 두 곳의 ⚠️는 모두 "다른 이름의 동등 API가 존재"하는 케이스이며, 후속 구현 prompt에서 `GetTypedOuter<USkeletalMeshComponent>()`와 `UWorld::GetPlayerController(0)`를 사용하도록 명시하면 그대로 진행 가능. **STOP 불필요**.

---

## 5. Layer별 변경 청사진

### 5.1 Layer 1 (Data Model) — Before / After

**Before**: scan report [Section 2.1](Document/animnotify_infra_scan.md) — `FAnimNotifyEvent` = (TriggerTime, Duration, NotifyName) 3필드, polymorphic 계층 없음.

**After**: Section 3 참조 — 동일 struct에 `EAnimNotifyType Type`, `FString SoundId`, `FCameraShakeParams ShakeParams` 3필드 추가.

**변경 파일**:
- [Engine/Asset/Animation/Notify/AnimNotify.h](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h) — enum 추가, struct 확장.
- 새 include 필요: `Camera/CameraShakeModifier.h` (또는 forward decl + cpp에 include) — 의존성 노트는 5.4.

### 5.2 Layer 2 (Asset Serialization)

**Before**: `AssetVersion = 3u` [AnimSequence.h:98](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:98), `FAnimNotifyEvent::operator<<`는 3필드만 직렬화 [AnimNotify.h:24-30](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:24), `Ar << Notifies;` 한 줄로 전체 배열 직렬화 [AnimSequence.cpp:59](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:59).

**After**:
- `AssetVersion = 4u` (bump). v3은 더 이상 정식 포맷이 아니지만 load는 백필로 호환.
- `FAnimNotifyEvent::operator<<`는 **v4 포맷 전용**: `Type → SoundId → ShakeParams`까지 직렬화.
- v3 호환은 **`UAnimSequence::Serialize` 측에서 분기**한다. `FArchive`에는 버전 컨텍스트가 없으므로 `Header.Version` 값을 보고 분기.

**변경 파일**:
- [Engine/Asset/Animation/Notify/AnimNotify.h](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h) — operator<< 확장 + (필요 시) `FCameraShakeParams` 직렬화 helper.
- [Engine/Asset/Animation/Core/AnimSequence.h:98](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:98) — `AssetVersion = 4u`로 bump.
- [Engine/Asset/Animation/Core/AnimSequence.cpp:28-60](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:28) — Serialize 함수에 v3/v4 분기.

**백필 로직 의사 코드** (구현 코드 아님):

```
void UAnimSequence::Serialize(Ar):
    if (saving): Header.Version = AssetVersion (=4)
    Ar << Header
    // header check는 기존: AssetType == AnimSequence && Version >= MIN_SUPPORTED && Version <= AssetVersion
    // → 현재 IsValid(...)는 정확히 매치만 허용 — D3 위해 "<= AssetVersion" 허용으로 살짝 완화 필요.

    Ar << SequenceName
    Ar << SkeletonAssetPath
    Ar << bHasDataModel
    if (bHasDataModel): DataModel->Serialize(Ar)

    if (Header.Version >= 4):
        Ar << Notifies                         // v4 operator<<: 6필드 모두
    else: // v3 load
        uint32 Count; Ar << Count
        Notifies.resize(Count)
        for each Notify in Notifies:
            Ar << TriggerTime
            Ar << Duration
            Ar << NotifyName
            Notify.Type = EAnimNotifyType::None    // 백필
            // SoundId, ShakeParams는 default-construct 그대로
        // saving 측에서는 항상 v4로 저장하므로 이 분기는 reach 안 함.
```

> **주의**: 현재 `FAssetFileHeader::IsValid(type, version)`는 정확한 version 일치만 허용한다 ([AnimSequence.cpp:38-43](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:38)에서 `Header.IsValid(EAssetType::AnimSequence, AssetVersion)` 호출). v3 호환을 위해 (a) AnimSequence::Serialize에서 `IsValid` 호출을 우회/완화하거나, (b) `FAssetFileHeader`에 minVersion 인자를 추가하는 변경이 필요. → **T2의 일부 작업으로 명시**.

### 5.3 Layer 3 (Editor UI)

**Before**: scan report [Section 2.3](Document/animnotify_infra_scan.md) — Name/Time/Duration/Color 평탄 편집, "Add Notify at current frame" 단일 메뉴 [AnimSequenceEditorTab.cpp:635-643](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:635), inline property editor [.cpp:768-839](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:768).

**After**:

**(a) Add 메뉴 분기** — 컨텍스트 메뉴를 두 항목으로 분리.
- "Add Sound Notify at current frame" → `Type=EAnimNotifyType::Sound`, 빈 `SoundId`로 entry 생성.
- "Add Camera Shake Notify at current frame" → `Type=EAnimNotifyType::CameraShake`, default `FCameraShakeParams`로 entry 생성.
- D8에 따라 `NotifyName`은 기본값 `"SoundNotify"` / `"CameraShakeNotify"`(label) — 사용자가 자유롭게 수정.

**(b) `RenderNotifyPropertyInline` 확장**.
- 기존 Name / Trigger slider / Duration slider / Color / Go / Delete 유지.
- 그 아래 `Edited.Type` 분기 위젯 추가:
  - `Type == Sound`: `ImGui::InputText("SoundId", ...)` (D5) — 등록된 ID인지 검사는 안 함(D6).
  - `Type == CameraShake`: `FCameraShakeParams` 13개 필드 위젯 — Pattern combo, Duration / BlendIn / BlendOut / Frequency / Roughness / FOVAmplitude InputFloat, LocationAmplitude xyz / RotationAmplitude rpy InputFloat3, bApplyInCameraLocalSpace / bSingleInstance Checkbox, Seed InputScalar(uint32).
  - `Type == None`: 편집 위젯 없음 (v3 백필된 항목만 해당; 사용자가 이를 다른 타입으로 승격하고 싶다면 별도 "Convert to..." 버튼을 추가 가능 — 본 task 범위 외, Open Risks 참조).
- 변경 시 기존 `DataSource->UpdateNotify(...)` 흐름 그대로 사용.

**(c) `IAnimSequenceDataSource` / `FAnimNotifyEntry` 확장 (D9)**.
- `FAnimNotifyEntry`에 같은 신규 필드 3개 추가 [AnimSequenceDataSource.h:12-18](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceDataSource.h:12).
- `FUAnimSequenceDataSource::AddNotify / UpdateNotify / RebuildNotifyCache / WriteNotifyToAsset`이 새 필드를 양방향 복사하도록 확장 [AnimSequenceDataSource.cpp:49-123](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceDataSource.cpp:49).

**변경 파일**:
- [Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp) — Add 메뉴 + RenderNotifyPropertyInline.
- [Editor/UI/SkeletalEditor/AnimSequenceDataSource.h](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceDataSource.h) — Entry struct 확장.
- [Editor/UI/SkeletalEditor/AnimSequenceDataSource.cpp](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceDataSource.cpp) — adapter 변환 양방향.

### 5.4 Layer 4 (Runtime Dispatch) — D2 / D4 핵심

**Before**: `UAnimInstance::Update`가 `TriggeredNotifiesThisFrame : TArray<FName>`에 발화된 notify의 *이름만* 누적 [AnimInstance.cpp:74-84](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:74). C++ dispatcher 부재.

**After (D4 — 별도 함수 분리)**:

1. **`TriggeredNotifiesThisFrame`의 타입을 확장하지 않는다** — 외부 API 호환을 위해 `TArray<FName>` 그대로 유지 (Lua binding이 이미 의존 [LuaSkeletalMeshBindings.cpp:456-476](KraftonEngine/Source/Engine/Scripting/LuaSkeletalMeshBindings.cpp:456)).
2. Dispatch에는 `FAnimNotifyEvent` 전체가 필요하므로, `Update` 안에서 trigger 매칭 시 **별도의 로컬 임시 배열**(또는 인덱스 배열)에 `const FAnimNotifyEvent*` 또는 복사본을 쌓고, 판정 루프가 끝난 직후 `DispatchTriggeredNotifies(...)` 한 함수에 넘겨 dispatch한다.
3. 새 함수: `void UAnimInstance::DispatchTriggeredNotifies(const TArray<const FAnimNotifyEvent*>& InTriggered);` — protected. virtual 불필요(파생 클래스가 override해야 할 이유 없음; 단일 진실 경로 유지).
4. 함수 본문 분기는 D3 호환 포함:

```
for each Notify* in InTriggered:
    switch (Notify->Type):
    case None:        continue                          // D3: v3 백필 항목 skip
    case Sound:       FSoundManager::Get().PlayEffect(Notify->SoundId); break
    case CameraShake: APlayerCameraManager* CM = ResolveCameraManager();
                      if (CM) CM->StartCameraShake(Notify->ShakeParams);
                      break
```

5. `ResolveCameraManager()` 의사 코드 (private helper, D2 outer chain — Section 4의 검증 결과 적용):

```
APlayerCameraManager* UAnimInstance::ResolveCameraManager() const:
    USkeletalMeshComponent* Comp = GetTypedOuter<USkeletalMeshComponent>();   // 단계 1 (⚠️ Outer 체인)
    if (!Comp) return nullptr
    AActor* Owner = Comp->GetOwner();                                          // 단계 2
    if (!Owner) return nullptr
    UWorld* World = Owner->GetWorld();                                         // 단계 3
    if (!World) return nullptr
    APlayerController* PC = World->GetPlayerController(0);                     // 단계 4 (⚠️ GetPlayerController(0))
    if (!PC) return nullptr
    return PC->GetCameraManagerPtr();                                          // 단계 5 (nullable)
```

> 각 단계 nullptr 가드 필수 — headless / dedicated server / preview tab에서는 PC가 없을 수 있음. nullptr 시 silently no-op (사운드의 D6 정책과 동일한 톤).

6. `Update`의 마지막 줄에서 `DispatchTriggeredNotifies(LocalTriggered)` 호출. paused 분기와 `Length<=0` 분기는 dispatch 미실행이 자연스럽다(이미 early return).

**의존성 영향** — `AnimInstance.cpp`가 새로 include할 헤더:

- `"Sound/SoundManager.h"` (FSoundManager 사용)
- `"Component/SkeletalMeshComponent.h"` (GetTypedOuter 결과 캐스팅용 완전 타입 필요)
- `"GameFramework/AActor.h"` (이미 transitive로 들어와 있을 가능성 높지만 명시 권장)
- `"GameFramework/World.h"` (`UWorld::GetPlayerController` 호출용)
- `"GameFramework/PlayerController.h"` (PC->GetCameraManagerPtr)
- `"Camera/PlayerCameraManager.h"` (이미 PlayerController.h가 transitive로 들고 있음 [PlayerController.h:4](KraftonEngine/Source/Engine/GameFramework/PlayerController.h:4))

**모듈 순환 위험 분석**:
- 모든 추가 헤더가 **같은 Engine 모듈 내부**.
- `Animation/Core` 디렉토리는 그동안 `Component/`나 `GameFramework/`에 의존하지 않았으나, 디렉토리 간 단방향 의존이 강제되는 빌드 시스템 증거는 본 scan에서 발견되지 않음. 솔루션 파일은 `KraftonEngine.vcxproj` 하나의 단일 모듈 — 헤더 단방향 가시성만 지키면 됨.
- 역방향 의존(예: `Component/SkeletalMeshComponent.h`가 이미 `Asset/Animation/Core/AnimInstance.h`를 include함 [SkeletalMeshComponent.h:5](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:5))이 존재하므로, `AnimInstance.h`에서 컴포넌트 헤더를 include하면 **순환 #include 위험**.
- 해결: `AnimInstance.h`에는 `forward declaration`만 두고, **모든 outer chain include는 `AnimInstance.cpp`에 한정**. `DispatchTriggeredNotifies` 선언도 헤더에서는 forward decl로 충분(파라미터를 `const TArray<const FAnimNotifyEvent*>&`로 잡기 때문에 `FAnimNotifyEvent`만 알면 됨 — 이미 헤더에서 include됨).

**변경 파일**:
- [Engine/Asset/Animation/Core/AnimInstance.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h) — `DispatchTriggeredNotifies` / `ResolveCameraManager` 선언 추가, forward decl 추가.
- [Engine/Asset/Animation/Core/AnimInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) — 새 include 묶음 + 두 함수 정의 + `Update` 끝 호출.

### 5.5 Layer 5 (Effect Dispatch Target)

**변경 없음.** `FSoundManager`와 `APlayerCameraManager`는 이미 호출 가능. 본 task에서 호출 측 코드만 추가한다.

확정 시그니처(재참조 편의):

| API | 헤더 | 시그니처 |
|---|---|---|
| Sound 재생 | [Sound/SoundManager.h](KraftonEngine/Source/Engine/Sound/SoundManager.h) | `FSoundManager::Get().PlayEffect(const FSoundId& /* = FString */ ID)` |
| Camera shake 시작 | [Camera/PlayerCameraManager.h:73](KraftonEngine/Source/Engine/Camera/PlayerCameraManager.h:73) | `UCameraShakeModifier* APlayerCameraManager::StartCameraShake(const FCameraShakeParams& Params)` |

호출 측이 반환값을 보관할 필요 없음 (shake 강제 종료는 본 task 범위 외).

---

## 6. Sequence Diagram (텍스트)

### 6.1 Add Sound Notify 흐름 (에디터)

사용자 우클릭(timeline 위) → 컨텍스트 메뉴 → "Add Sound Notify at current frame" 클릭 → 핸들러가 `FAnimNotifyEntry` 생성 (`Type=Sound`, 빈 `SoundId`) → `DataSource->AddNotify(Entry)` → adapter가 `FAnimNotifyEntry → FAnimNotifyEvent` 복사 (D9) → `Sequence->AddNotify(Event)` → `Sequence::Notifies`에 push → 캐시 갱신 → 사용자가 inline property panel에서 SoundId 텍스트 입력 → `UpdateNotify` 동일 경로 → 사용자 Save → `UAnimSequence::Serialize` (v4) → 디스크.

### 6.2 Sound 발화 흐름 (재생)

게임 tick → `USkeletalMeshComponent::TickComponent`(또는 동등 경로) → `AnimInstance->Update(dt)` → 시간 누적 + loop wrap → `GetActiveNotifies()`에서 시퀀스의 `Notifies` 획득 → 각 notify에 대해 `IsTriggeredBetween(prev,curr,len)` 판정 → 로컬 배열에 trigger된 notify 포인터 push (+ 기존대로 `TriggeredNotifiesThisFrame`에 이름도 push, Lua 호환) → 판정 루프 종료 직후 `DispatchTriggeredNotifies(local)` 호출 → 각 notify에 대해 `Type==Sound` 분기 → `FSoundManager::Get().PlayEffect(Notify.SoundId)` → SFML이 재생 (미등록이면 UE_LOG + no-op, D6).

### 6.3 CameraShake 발화 흐름 (재생)

6.2와 동일하게 `DispatchTriggeredNotifies`까지 진행 → `Type==CameraShake` 분기 → `ResolveCameraManager()` 호출 → outer chain (`GetTypedOuter<USkeletalMeshComponent>` → `GetOwner` → `GetWorld` → `GetPlayerController(0)` → `GetCameraManagerPtr`) → nullptr 시 no-op → `CM->StartCameraShake(Notify.ShakeParams)` → `UCameraShakeModifier`가 modifier list에 push되어 카메라 view에 매 프레임 offset 적용.

---

## 7. 구현 작업 분해 (Task Breakdown)

권장 순서는 변경 의존성의 ascending(아래로 갈수록 위 작업 결과를 가정). 각 T는 독립 PR / 커밋 단위로 끊을 수 있도록 설계됨.

| # | 작업 | 변경 파일 | 의존 |
|---|---|---|---|
| **T1** | `EAnimNotifyType` enum 정의 + `FAnimNotifyEvent`에 3개 신규 필드 추가 (`Type`, `SoundId`, `ShakeParams`). 빌드 깨지지 않게 default 값으로 채움. | `AnimNotify.h` (+ 필요 시 forward decl/include for `FCameraShakeParams`) | — |
| **T2** | `operator<<` 갱신(v4 포맷) + `FCameraShakeParams` 직렬화 helper + `AnimSequence::AssetVersion = 4u` bump + `AnimSequence::Serialize`에 v3 백필 분기. `FAssetFileHeader::IsValid` 의 version 검사 완화(또는 minVersion 인자 도입). 저장→재로드 라운드트립 단위 테스트 권장. | `AnimNotify.h`, `AnimSequence.h`, `AnimSequence.cpp`, (필요 시) `AssetFileHeader.h` | T1 |
| **T3** | `FAnimNotifyEntry`에 같은 3개 필드 추가 + `FUAnimSequenceDataSource::AddNotify/UpdateNotify/RebuildNotifyCache/WriteNotifyToAsset` 양방향 복사 갱신 (D9). | `AnimSequenceDataSource.{h,cpp}` | T1 |
| **T4** | 에디터 컨텍스트 메뉴 분기: "Add Sound Notify" / "Add Camera Shake Notify". | `AnimSequenceEditorTab.cpp` ([컨텍스트 메뉴 부근:635](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:635)) | T3 |
| **T5** | `RenderNotifyPropertyInline`에 Type별 payload 위젯 분기 (Sound: InputText; CameraShake: 13필드 묶음; None: read-only/안내). | `AnimSequenceEditorTab.cpp` ([:768](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:768)) | T3 |
| **T6** | `DispatchTriggeredNotifies(const TArray<const FAnimNotifyEvent*>&)` 함수 신설 + `Update` 의 판정 루프에서 trigger된 notify의 포인터를 로컬 배열에 누적 + 루프 종료 직후 dispatch 호출. 본 T에서는 Type=None 만 분기(skip)하고 Sound/CameraShake는 stub log로 둔다. | `AnimInstance.h`, `AnimInstance.cpp` | T1 |
| **T7** | T6의 Sound 분기 채우기: `FSoundManager::Get().PlayEffect(Notify.SoundId)`. include 추가. | `AnimInstance.cpp` (+ `#include "Sound/SoundManager.h"`) | T6 |
| **T8** | T6의 CameraShake 분기 + `ResolveCameraManager()` private helper 추가 (outer chain). 각 단계 nullptr 가드. include 추가. | `AnimInstance.h`(helper 선언), `AnimInstance.cpp`(+ component/world/PC/CamMgr 헤더) | T6 |
| **T9** | (Optional) 통합 smoke test 시나리오 — 1 sound notify + 1 shake notify를 가진 .animseq를 ContentBrowser에서 로드, Game 모드 재생 시 양쪽 모두 발화. (자동화 안 되어 있으면 manual checklist로) | (no code) | T7, T8 |

> T6를 stub log로 먼저 합치는 이유: outer chain의 nullptr가드와 dispatch 분기 자체를 먼저 검증한 뒤, 외부 시스템(Sound/Camera) 호출을 별 PR로 격리하기 위함이다. T6 단독으로 합쳤을 때 동작은 "no-op + log" — 안전.

---

## 8. Open Risks (실수 가능성이 높은 곳)

| Risk | 위치 / 시나리오 | 권장 대응 |
|---|---|---|
| **R1** v3 백필 분기에서 read 순서 실수 시 file corruption | T2 [AnimSequence.cpp:28-60](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:28) Serialize에서 분기 | 저장→재로드 라운드트립 단위 테스트 1개라도 작성. v3 fixture(=현재 코드로 저장된 .animseq)를 v4 코드로 로드 → 모든 notify가 Type=None인지 확인 → 저장 → 다시 로드 시 동일 결과. |
| **R2** Outer chain nullptr 미가드로 인한 crash | T8 `ResolveCameraManager` 5단계 | 5단계 모두 `if (!X) return nullptr` 명시. 특히 **headless/dedicated server**에서는 PlayerController가 없을 수 있음 — 정책은 "silent no-op"(D6 톤). PIE/preview tab에서 dispatch가 발생하는지 별도 확인(아래 R5). |
| **R3** ImGui payload 위젯의 매 프레임 string 복사 | T5 `RenderNotifyPropertyInline` 의 `SoundId` InputText 부근. 현재 `RenderNotifyPropertyInline`은 이미 `FAnimNotifyEntry`를 매 프레임 복사(`Edited = Notifies[Idx]`)하므로 신규 string 복사 비용은 한 줄 추가 수준 — 실측 전까지 캐싱 도입 보류. | 캐싱 정책 도입은 보류. 다만 InputText buffer는 char[]로 별도 보유(기존 `NameBuf`와 같은 패턴) — `Edited.SoundId.c_str()`를 직접 InputText에 넣지 말 것. |
| **R4** AnimInstance가 PlayerController/CameraManager 헤더를 알게 되면서 발생하는 **의존성 방향 변화** | 5.4 의존성 메모 참조 | `AnimInstance.h`에는 **forward decl만**, include는 모두 `.cpp`로 한정. `Component/` 폴더 헤더의 transitive include로 인한 순환 가능성도 cpp 격리로 해결. |
| **R5** **에디터 preview 재생 시 dispatch가 일어나는가?** | preview에서 AnimSequence가 `UAnimSingleNodeInstance`로 재생됨. `Update`는 호출되므로 dispatch도 호출됨 — outer chain의 World가 **에디터 preview world**일 가능성 있음. 그 world에 PlayerController가 없으면 CameraShake는 silent no-op이지만, Sound는 **그대로 재생됨**. | 정책 확인 필요: 에디터 preview에서 사운드가 들리는 게 의도인가? 안 들리는 게 맞다면 dispatch에 "Game 모드에서만" 가드가 필요 — 본 task 범위 외이지만 **사용자 사전 확인 권장 (Open Question)**. |
| **R6** `FAssetFileHeader::IsValid` 가 정확한 version 일치만 통과시키는 현재 동작과 D3(v3 백필)의 충돌 | T2 [AnimSequence.cpp:38-43](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:38) | 두 옵션: (a) AnimSequence::Serialize에서 IsValid 호출을 우회하고 직접 type+version range 검사 (b) FAssetFileHeader::IsValid에 minVersion 인자 추가. **(b)가 더 깔끔하지만 다른 asset class 영향 확인 필요** — T2 진입 직전 결정 필요. |
| **R7** Type=None 으로 백필된 notify를 사용자가 에디터에서 어떻게 처리하는가 | T5 inline editor의 `Type==None` 분기 — 위에 "(unset)" 표시? 다른 타입으로 변환 버튼? | 1차에서는 **read-only "(no type — legacy)" 안내만**. 사용자가 새 타입으로 바꾸려면 삭제 후 재추가. 변환 UI는 추후. |

**사용자 사전 확인 권장 사항** (구현 진입 전):
- **R5**: 에디터 preview 재생 시 Sound notify가 들리길 원하는가? (정책 미정 — 본 문서는 "기본은 발화" 가정)
- **R6**: `FAssetFileHeader::IsValid` 시그니처를 minVersion 인자 추가로 바꿔도 되는가? (asset class 전반에 영향 — 다른 asset (.skel, .mesh 등)도 같은 헤더를 쓰면 행동 변화 가능)

---

## 9. Out of Scope (재확인)

- Particle, Hit, Event(BP-like), Sync marker 등 다른 notify 타입 (enum 확장만으로 지원 가능하다는 점만 명시).
- `UAnimNotifyState` (begin/tick/end 콜백 모델).
- Network replication.
- Undo / Redo.
- Reverse playback / `SetEvaluationTime` seek 시 발화 정확성 (D7).
- AnimMontage 등 상위 컨테이너.
- 콜백 주입 / 의존성 역전 패턴 (D2에서 직접 호출 선택).
- Sound asset picker UI (D5에서 텍스트 입력 선택).
- 미등록 SoundId 사전 검증 UI (D6에서 silent no-op 유지).
- 동시 발화 충돌 조정 (같은 frame에 Sound 2개 → 둘 다 발화).
- StateMachine 노드의 `OnNotify` 전이 조건([AnimGraph_StateMachine.h:36](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph_StateMachine.h:36)) — 본 설계와 직교.

---

## 10. Next Step

본 문서가 확정되면, 후속 구현 prompt는 [Section 7](#7-구현-작업-분해-task-breakdown)의 **T1 → T9** 를 순서대로 분할하여 작성한다. 각 후속 prompt는:

1. 직전 T의 산출 상태를 가정한 짧은 도입.
2. 본 문서의 해당 섹션 인용(예: T2 prompt는 5.2 + R1 + R6 인용).
3. 변경 파일 목록과 검증 방법(빌드 / 라운드트립 / 수동 발화 확인) 명시.

R5와 R6의 사용자 확인이 우선 필요한 경우, T2 진입 전 짧은 "정책 확정 prompt" 한 번을 끼워 넣는다.

(끝)
