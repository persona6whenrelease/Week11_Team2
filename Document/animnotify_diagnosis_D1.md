# AnimNotify 진단 D1 — 현황 + H2 + Lua infra 사전조사

조사 방법: code view + grep only. git log 미사용. 핸드오프 문서 mapping은 검증 대상이지 근거가 아님.

조사 일자 컨텍스트: 2026-05-20 기준 working tree.

---

## 1. 요약

- **목표 A 결과**: P1~P5 모든 5개 layer **완전 존재(✓)**. 부분/부재 없음.
- **목표 B 결과**: H2 **통과**. `DispatchTriggeredNotifies`의 Sound 분기가 `FSoundManager::Get().PlayEffect(Notify.SoundId)`까지 컴파일 가능한 형태로 살아있음. 주석/`#if 0`/early return/TODO 없음.
- **목표 C 결과**: SoundManager binding **존재** (`PlayEffect` 포함). CameraManager 직접 binding **부재** (PlayerController 경유 primitive-args 우회 binding만 존재). `OnAnimNotify` 등가 Lua callback hook **부재**. `TriggeredNotifiesThisFrame`은 `TArray<FName>`로 이름만 보관.
- **상관관계 메모**: H2는 통과지만, 에디터 preview의 silent 증상은 본 함수 내부가 아니라 호출 측(상류) `bPaused=true` 강제로 `Update`가 조기 return 하기 때문일 가능성이 있다. 자세한 내용은 5절 참조.

---

## 2. 목표 A — 5 Layer 현황

### L1 Data Model
- **파일:라인**: [AnimNotify.h:20-25](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:20) (enum), [AnimNotify.h:47-90](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:47) (struct)
- **존재 여부**: ✓ 완전 존재
- **누락/이상**: 없음. enum None/Sound/CameraShake 3종이 모두 정의되어 있고, struct에 Type/SoundId/ShakeParams 3개 payload 필드가 평탄하게 보관됨. `FCameraShakeParams`는 [CameraShakeModifier.h:12-32](KraftonEngine/Source/Engine/Camera/CameraShakeModifier.h:12)에 13필드 정의.
- **핵심 발췌**:
```cpp
// AnimNotify.h:20-25
enum class EAnimNotifyType : uint8
{
    None = 0,
    Sound = 1,
    CameraShake = 2,
};

// AnimNotify.h:47-56
struct FAnimNotifyEvent
{
    float TriggerTime = 0.0f;
    float Duration = 0.0f;
    FName NotifyName;

    EAnimNotifyType    Type = EAnimNotifyType::None;
    FString            SoundId;
    FCameraShakeParams ShakeParams;
    ...
};
```

### L2 Serialization
- **파일:라인**: [AnimSequence.h:99](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:99) (AssetVersion 상수), [AnimSequence.cpp:28-83](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:28) (Serialize 구현). `FCameraShakeParams` serializer는 [AnimNotify.h:27-42](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:27), `FAnimNotifyEvent` serializer는 [AnimNotify.h:57-73](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:57).
- **존재 여부**: ✓ 완전 존재
- **누락/이상**: 없음. AssetVersion=4 상수 존재, v3 백필 경로 분기(Type=None) 존재, v4 경로는 `Ar << Notifies` (TArray<FAnimNotifyEvent>) 전체 직렬화.
- **핵심 발췌**:
```cpp
// AnimSequence.h:99
static constexpr uint32 AssetVersion = 4u;

// AnimSequence.cpp:65-82
if (Ar.IsLoading() && Header.Version == 3u)
{
    // v3 backfill: 새 v4 필드(Type/SoundId/ShakeParams)는 default 값으로 채운다.
    uint32 Count = 0;
    Ar << Count;
    Notifies.resize(Count);
    for (uint32 i = 0; i < Count; ++i)
    {
        Ar << Notifies[i].TriggerTime;
        Ar << Notifies[i].Duration;
        Ar << Notifies[i].NotifyName;
        Notifies[i].Type = EAnimNotifyType::None;
    }
}
else
{
    Ar << Notifies;
}
```

### L3 Editor UI
- **파일:라인**: [AnimSequenceEditorTab.cpp:706-725](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:706) (Add Notify 메뉴 분기), [AnimSequenceEditorTab.cpp:920-1044](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:920) (inline payload 위젯). 데이터 어댑터: [AnimSequenceDataSource.h:14-24](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceDataSource.h:14) `FAnimNotifyEntry`에 Type/SoundId/ShakeParams 필드 미러링.
- **존재 여부**: ✓ 완전 존재
- **누락/이상**: 없음. 단, `ImGui::InputText`는 사용되지 않고 SoundId는 `RadioButton` 기반 picker (`FSoundManager::Get().GetRegisteredEffectIds()` 결과 중 택1, 자유 입력 미지원). ShakeParams 위젯은 13개 필드(Pattern, Duration, BlendIn/Out, LocAmp xyz, RotAmp pyr, FOVAmp, Frequency, Roughness, bApplyInCameraLocalSpace, bSingleInstance, Seed) 모두 노출 — 좌표축 위젯이 InputFloat3 1줄로 묶인 형태라 13줄 위젯이 아닌 9줄 위젯 묶음으로 그려진다는 점만 차이.
- **핵심 발췌**:
```cpp
// AnimSequenceEditorTab.cpp:706-714 — Add Notify 분기
if (ImGui::MenuItem("Add Sound Notify at current frame"))
{
    FAnimNotifyEntry NN;
    NN.Name = "SoundNotify";
    NN.TriggerTime = CurrentTime;
    NN.Type = EAnimNotifyType::Sound;
    SelectedNotifyIndex = DataSource->AddNotify(NN);
}
if (ImGui::MenuItem("Add Camera Shake Notify at current frame"))
{
    ...
    NN.Type = EAnimNotifyType::CameraShake;
    SelectedNotifyIndex = DataSource->AddNotify(NN);
}

// AnimSequenceEditorTab.cpp:924-963 — Sound payload 위젯 (요약)
else if (Edited.Type == EAnimNotifyType::Sound)
{
    ...
    const TArray<FSoundId> RegisteredIds = FSoundManager::Get().GetRegisteredEffectIds();
    for (const FSoundId &Id : RegisteredIds)
    {
        if (ImGui::RadioButton(Id.c_str(), Id == Edited.SoundId)) { Edited.SoundId = Id; bChanged = true; }
    }
}
```

### L4 Dispatch 골격
- **파일:라인**: [AnimInstance.h:89](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:89) (`DispatchTriggeredNotifies` 선언), [AnimInstance.h:108](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:108) (`TriggeredNotifiesThisFrame` 멤버), [AnimInstance.cpp:29-99](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:29) (`Update` 구현 + dispatch 호출), [SkeletalMeshComponent.cpp:398](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:398) (`Update` 호출 진입점).
- **존재 여부**: ✓ 완전 존재
- **누락/이상**: 없음. 루프 wrap 처리(`IsTriggeredBetween`) + `LocalTriggered`(포인터 배열) 누적 후 `DispatchTriggeredNotifies` 호출 + `TriggeredNotifiesThisFrame`(이름만) 누적이 한 번에 이루어짐.
- **핵심 발췌**:
```cpp
// AnimInstance.cpp:81-98
const TArray<FAnimNotifyEvent> *Notifies = GetActiveNotifies();
TArray<const FAnimNotifyEvent *> LocalTriggered;
if (Notifies)
{
    for (const FAnimNotifyEvent &Notify : *Notifies)
    {
        if (Notify.IsTriggeredBetween(PreviousTime, CurrentTime, Length))
        {
            TriggeredNotifiesThisFrame.push_back(Notify.NotifyName);
            LocalTriggered.push_back(&Notify);
        }
    }
}
if (!LocalTriggered.empty())
{
    DispatchTriggeredNotifies(LocalTriggered);
}
```

### L5 Real Dispatch
- **파일:라인**: [AnimInstance.cpp:101-135](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:101) (`DispatchTriggeredNotifies` 본체), [AnimInstance.cpp:137-157](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:137) (`ResolveCameraManager` outer chain 5단계).
- **존재 여부**: ✓ 완전 존재
- **누락/이상**: 없음. Sound → `FSoundManager::Get().PlayEffect(Notify.SoundId)` 직접 호출, CameraShake → `ResolveCameraManager()` outer chain 통한 `CamMgr->StartCameraShake(Notify.ShakeParams)` (FCameraShakeParams 그대로 전달). nullptr 가드 5단계 (AnimInstance → SkeletalMeshComponent → AActor → UWorld → APlayerController → APlayerCameraManager). 에디터 preview처럼 PlayerController가 없는 환경은 silent no-op.
- **핵심 발췌**:
```cpp
// AnimInstance.cpp:108-128
switch (Notify.Type)
{
case EAnimNotifyType::None:
    continue; // D3: v3 백필 항목 — skip
case EAnimNotifyType::Sound:
    FSoundManager::Get().PlayEffect(Notify.SoundId);
    break;
case EAnimNotifyType::CameraShake:
{
    APlayerCameraManager *CamMgr = ResolveCameraManager();
    if (CamMgr) { CamMgr->StartCameraShake(Notify.ShakeParams); }
    break;
}
default: break;
}
```

---

## 3. 목표 B — H2 검증

### 3.1 DispatchTriggeredNotifies 함수
- **시그니처**: `void UAnimInstance::DispatchTriggeredNotifies(const TArray<const FAnimNotifyEvent *> &InTriggered)`
- **선언**: [AnimInstance.h:89](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:89) (protected)
- **정의**: [AnimInstance.cpp:101-135](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:101)

### 3.2 Sound 분기 전체 발췌
```cpp
// AnimInstance.cpp:101-135 (전체)
void UAnimInstance::DispatchTriggeredNotifies(const TArray<const FAnimNotifyEvent *> &InTriggered)
{
    for (const FAnimNotifyEvent *NotifyPtr : InTriggered)
    {
        if (!NotifyPtr) continue;
        const FAnimNotifyEvent &Notify = *NotifyPtr;

        switch (Notify.Type)
        {
        case EAnimNotifyType::None:
            // D3: v3 백필 항목 — skip
            continue;

        case EAnimNotifyType::Sound:
            // D6: 미등록/empty SoundId는 SoundManager 측에서 UE_LOG + no-op. 사전 검증 없음.
            FSoundManager::Get().PlayEffect(Notify.SoundId);
            break;

        case EAnimNotifyType::CameraShake:
        {
            APlayerCameraManager *CamMgr = ResolveCameraManager();
            if (CamMgr)
            {
                CamMgr->StartCameraShake(Notify.ShakeParams);
            }
            // CamMgr == nullptr 이면 silent no-op (R2, R5)
            break;
        }

        default:
            // 알 수 없는 type (forward-compat): 무시.
            break;
        }
    }
}
```

### 3.3 PlayEffect 호출 여부 및 인자
- **호출 존재**: ✓ 존재 ([AnimInstance.cpp:116](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:116))
- **인자**: `Notify.SoundId` (타입 `FString`, alias `FSoundId`)
- **SoundId 데이터 경로**:
  1. 에디터에서 `FAnimNotifyEntry::SoundId` 세팅 ([AnimSequenceEditorTab.cpp:956](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:956))
  2. 어댑터 `FUAnimSequenceDataSource`가 asset의 `FAnimNotifyEvent::SoundId`로 mirror
  3. `UAnimSequence::Notifies` 배열에 보관 ([AnimSequence.h:122](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:122))
  4. 직렬화 시 `Ar << Notify.SoundId` ([AnimNotify.h:70](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:70))
  5. 런타임에 `UAnimSingleNodeInstance::GetActiveNotifies()`가 `UAnimSequence::GetNotifies()` 포인터 반환 ([AnimSingleNodeInstance.cpp:75-78](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:75))
  6. `UAnimInstance::Update`가 `IsTriggeredBetween` 통과한 항목의 `&Notify`를 `LocalTriggered`에 push → `DispatchTriggeredNotifies` 진입 → Sound case에서 `Notify.SoundId` read.

### 3.4 비활성화 흔적
- 주석처리된 호출: **없음**
- `#if 0`: **없음**
- early return: dispatch 함수 내에서 `continue`는 `None` case 한 곳뿐(의도된 skip), Sound case에는 없음
- `TODO`/`FIXME`: 본 함수 내에는 **없음** (다른 의도성 주석 D3/D6/R2/R5는 동작 설명용일 뿐 skip 표시 아님)

### 3.5 판정
- **H2 통과**
- **근거**: Sound case → `FSoundManager::Get().PlayEffect(Notify.SoundId)` 무조건 호출, 인자도 정상적으로 `FAnimNotifyEvent::SoundId`에서 직접 읽음. 사이 비활성 흔적 없음. silent 이슈의 원인이 본 함수 내부일 가능성은 낮음 — 즉, "Sound branch가 끊겨 silent"는 root cause 후보가 아니다.

---

## 4. 목표 C — Lua 경유 infra 사전조사

### 4.1 SoundManager Lua binding 현황
- **파일:라인**: [LuaSoundLibrary.cpp:10-34](KraftonEngine/Source/Engine/Scripting/LuaSoundLibrary.cpp:10)
- **binding된 함수 목록** (전역 `Sound` table에 등록):
  - `Sound.PlayEffect(ID)` (1-arg)
  - `Sound.PlayEffect(ID, Volume)` (2-arg — 단, **Volume 인자 실제로는 무시** ([LuaSoundLibrary.cpp:21](KraftonEngine/Source/Engine/Scripting/LuaSoundLibrary.cpp:21)에서 `PlayEffect(ID)`만 호출). 핸드오프와 같은 관찰을 위해 명시함.)
  - `Sound.StopEffect(ID)`
  - `Sound.IsEffectPlaying(ID) → bool`
- **PlayEffect binding 존재 여부**: ✓ 존재 (overload 형태). C++ AnimNotify dispatch가 호출하는 것과 동일한 `FSoundManager::Get().PlayEffect(ID)`로 그대로 위임.
- **binding 미존재 항목** (참고용 관찰만): `LoadEffect`, `LoadMusic`, `PlayMusic`/`StopMusic`/`StopAllMusic`, `PlayBGM`/`StopBGM`, `GetEffectDuration`, `GetRegisteredEffectIds`는 Lua binding 없음.

### 4.2 CameraManager Lua binding 현황
- **파일:라인**: `APlayerCameraManager` 직접 binding은 **부재**. 우회 binding은 [LuaPlayerControllerBindings.cpp:616-648](KraftonEngine/Source/Engine/Scripting/LuaPlayerControllerBindings.cpp:616) (`PlayerController.StartCameraShake`), `LuaPlayerControllerBindings.cpp:571-720`의 Fade/Vignette 관련 항목들도 `PlayerController:GetCameraManagerPtr()` 경유로 CameraManager 메서드를 호출.
- **존재 여부**: △ **간접 노출만 존재**.
  - 직접 `APlayerCameraManager` usertype은 등록되어 있지 않음.
  - `PlayerController:StartCameraShake(Duration, LocationAmplitude, RotationAmplitude, Frequency [, FOVAmplitude [, bSingleInstance]])` overload 3개가 Lua에 노출.
  - 이 binding은 [PlayerController.cpp:336-362](KraftonEngine/Source/Engine/GameFramework/PlayerController.cpp:336)에서 primitive 인자들을 모아 내부에서 `FCameraShakeParams` 구성 후 `GetCameraManager().StartCameraShake(Params)` 호출.
  - 따라서 `FCameraShakeParams`의 13필드 중 `Pattern`, `BlendInTime`, `BlendOutTime`, `Roughness`, `bApplyInCameraLocalSpace`, `Seed`, `LocationAmplitude`의 비등방 xyz, `RotationAmplitude`의 비등방 P/Y/R는 **현재 Lua에서 지정 불가능**.
- **핸드오프 가정과의 차이**: 핸드오프 추정("현재 없음")은 "FCameraShakeParams 완전 binding은 없음" 측면에서는 일치. 다만 PlayerController 경유 primitive-arg 형태로는 이미 일부 binding이 있다는 점이 관찰됨.

### 4.3 OnAnimNotify 또는 등가 Lua callback hook
- **존재 여부**: ✗ **부재**
- **근거**:
  - `lua_pcall`, `luabridge`, `CallLuaFunction` 등 C++ → Lua 호출 패턴은 src 트리에서 검색 시 `ThirdParty/Sol/sol.hpp`(라이브러리)에만 매치, 본 프로젝트 src 코드에 매치 없음.
  - `OnAnimNotify` 식별자 매치 없음.
  - 현재 Lua infra는 **Lua가 C++를 polling 하는 단방향 모델**: `LuaSkeletalMeshBindings.cpp:787-807`의 `GetTriggeredNotifies`가 매 프레임 호출되면 Lua 측이 결과를 받는 형태. C++가 Lua 함수를 직접 invoke 하는 hook은 없음.

### 4.4 TriggeredNotifiesThisFrame 현재 형태
- **정확한 타입**: `TArray<FName>` ([AnimInstance.h:108](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:108))
- **push 위치**: [AnimInstance.cpp:89](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:89) — `TriggeredNotifiesThisFrame.push_back(Notify.NotifyName);` (이름만 push). 동일 iteration에서 `LocalTriggered`에 `&Notify` 포인터도 push되지만 이쪽은 함수-로컬 변수로 frame 끝나면 소멸.
- **clear/소비 위치**:
  - clear: [AnimInstance.cpp:24](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:24) (`InitializeAnimation`), [AnimInstance.cpp:32](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:32) (매 `Update` 시작 시).
  - 소비 (Lua): [LuaSkeletalMeshBindings.cpp:801](KraftonEngine/Source/Engine/Scripting/LuaSkeletalMeshBindings.cpp:801) — `for (const FName& NotifyName : AnimInstance->GetTriggeredNotifiesThisFrame())` 로 이름만 list로 변환.
  - 소비 (C++): 외부에서 `GetTriggeredNotifiesThisFrame()` 호출하는 위치는 위 한 곳 외에 없음 (grep 기준).
- **Lua 전달 시 정보 부족 항목**:
  - `Type`: 부재 (이름만 있어서 Lua가 type별 분기 불가)
  - `SoundId`: 부재
  - `ShakeParams`: 부재 (13필드 전체)
  - `TriggerTime`/`Duration`: 부재 (NotifyName 외에는 일체 없음)
- **추가 관찰**: `DispatchTriggeredNotifies(InTriggered)`가 받는 `TArray<const FAnimNotifyEvent*>`는 fully-populated payload를 들고 있으나 함수 인자 scope를 벗어나면 사라짐. Lua에 payload 전달하려면 `TriggeredNotifiesThisFrame`을 더 풍부한 타입으로 바꾸거나 별도 저장소가 필요하다는 사실만 관찰. (설계 제안은 본 문서 scope 밖)

### 4.5 Hook 지점 후보 표
| 위치 후보 | 파일:라인 | 현재 코드 형태 | Lua hook 삽입 시 영향 항목 |
|---|---|---|---|
| `DispatchTriggeredNotifies` 진입부 | [AnimInstance.cpp:101](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:101) | `switch (Notify.Type)` 만 분기, Lua 우선 분기 없음 | `InTriggered` payload 전체 보유. 진입부에서 Lua 우선 시도 후 fallback 분기에 진입하는 패턴 가능 (현재 hook 없음). |
| Sound case 본체 | [AnimInstance.cpp:114-117](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:114) | `FSoundManager::Get().PlayEffect(Notify.SoundId)` 직접 호출 | Lua의 `Sound.PlayEffect`도 정확히 같은 C++ 함수에 도달 — Lua 경유 시 동일 효과. 분기 추가 위치는 이 한 줄. |
| CameraShake case 본체 | [AnimInstance.cpp:119-128](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:119) | `ResolveCameraManager()` outer chain → `CamMgr->StartCameraShake(Notify.ShakeParams)` | Lua 측은 PlayerController 경유 primitive-args binding만 있어 13필드 중 일부 손실. Lua 우선 분기 시 정보 손실 없는 직접 binding 신설이 필요한 상태(관찰만). |
| `TriggeredNotifiesThisFrame` 정의 | [AnimInstance.h:108](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:108) | `TArray<FName>` 이름만 보관 | Lua callback hook에 payload 전달하려면 본 멤버를 풍부한 타입으로 바꾸거나 별도 멤버 추가 필요(관찰만). |
| `TriggeredNotifiesThisFrame` push 위치 | [AnimInstance.cpp:89](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:89) | `push_back(Notify.NotifyName)` | 같은 위치에서 풍부한 정보를 같이 push 하는 형태 가능(관찰만). |
| SoundManager Lua binding 위치 | [LuaSoundLibrary.cpp:10-34](KraftonEngine/Source/Engine/Scripting/LuaSoundLibrary.cpp:10) | `Sound.PlayEffect/StopEffect/IsEffectPlaying` 3종만 등록 | 이미 PlayEffect 존재. 추가 binding(LoadEffect, registered ID 조회 등) 신설 지점. |
| CameraManager Lua binding 위치 | (파일 없음 — 신설 지점 후보) | `APlayerCameraManager` usertype 미등록. PlayerController 경유 primitive-arg shake binding만 [LuaPlayerControllerBindings.cpp:616-648](KraftonEngine/Source/Engine/Scripting/LuaPlayerControllerBindings.cpp:616) | FCameraShakeParams 완전 binding 신설하려면 별도 파일 또는 `LuaPlayerControllerBindings.cpp`/유사 파일에 usertype 등록 지점이 필요. |
| AnimInstance → Lua callback 진입점 | (파일 없음 — hook 신설 후보) | C++에서 Lua 함수를 호출하는 인프라(`lua_pcall`/`sol::function::call`) 부재 | 본 프로젝트에 C++→Lua push 모델 전례 없음. 신설하려면 LuaInstance 보관 + 호출 위치 + 인자 직렬화가 필요(관찰만). |

---

## 5. 다음 단계로 넘어가기 전 사용자가 알아야 할 항목

1. **H2 통과의 의미**: 본 진단으로 "DispatchTriggeredNotifies 내부 Sound branch가 끊겨 silent"라는 가설은 **기각**. 에디터 preview silent 이슈의 root cause는 본 함수 내부가 아니라 **상류**에 있을 가능성이 높음.
2. **유력한 상류 후보 (관찰만, 추가 검증 prompt 필요)**:
   - `FAnimSequenceEditorTab::SyncPlaybackToComponent()` ([AnimSequenceEditorTab.cpp:338-339](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:338))가 매번 `Comp->SetBakedAnimPaused(true)` + `Comp->SetBakedAnimTime(CurrentTime)`을 호출. `SetBakedAnimPaused(true)`는 [SkeletalMeshComponent.h:88-92](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:88)에서 `AnimInstance->SetPaused(true)`로 전파.
   - `UAnimInstance::Update`는 [AnimInstance.cpp:41-47](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:41)에서 `if (bPaused) { PreviousTime = CurrentTime; return; }`로 **early return** — 이 경로에서는 Notify 판정 자체에 도달하지 않음. 즉 `DispatchTriggeredNotifies`가 호출되기 전에 차단됨.
   - 에디터 toolbar Play 버튼이 `SetBakedAnimPaused(false)`를 거는지, 그리고 그 다음 프레임 SyncPlayback이 다시 `true`로 덮어쓰지 않는지가 **별도 검증 필요**. 본 prompt scope 밖이므로 후속 prompt로 분리.
3. **L3에서 핸드오프와의 미세 불일치**: SoundId UI는 `ImGui::InputText`가 아니라 `RadioButton` picker. ShakeParams는 13필드 전체 노출이지만 위젯 줄 수로는 9개(InputFloat3 묶음 때문). 본 prompt의 "13필드" 표현과 미세 차이 있음 — 기능적으로는 모두 노출되어 있으므로 L3는 ✓로 판정.
4. **Lua infra 측면 관찰 (목표 C 결과 요약)**:
   - `Sound.PlayEffect`는 이미 Lua에서 동일 C++ 종착점(`FSoundManager::Get().PlayEffect`)에 도달. 즉 Lua 경유 Sound dispatch는 인프라가 다 갖춰져 있다.
   - CameraShake는 `PlayerController:StartCameraShake` primitive-args 우회 binding만 존재. `FCameraShakeParams` 전체 필드는 Lua에서 지정 불가능.
   - C++ → Lua callback 인프라(`lua_pcall`/`sol::function::call` 등 사용 사례)는 src 트리에 **전례 없음**.
   - `TriggeredNotifiesThisFrame`은 `TArray<FName>` 이름만 보관 — Lua에 payload(Type/SoundId/ShakeParams)를 전달할 정보가 부족.
5. **본 진단에서 확인 불가로 남은 항목**: 없음. 5개 layer + H2 분기 + Lua infra 4개 조사 항목 모두 code view로 1차 확인 완료.
