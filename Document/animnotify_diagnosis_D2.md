# AnimNotify 진단 D2 — bPaused 가설 검증 + Fix 설계

[D1 진단(`Document/animnotify_diagnosis_D1.md`)](Document/animnotify_diagnosis_D1.md) Section 5.2 후속.

조사 방법: code view + grep only. 코드 수정 금지. D1 결론(5 layer 존재, H2 통과)은 신뢰하고 그 위에 쌓는다.

조사 일자 컨텍스트: 2026-05-20 working tree.

---

## 1. 요약

- **Stage 1 판정**: **H-Pause 확정**. H-PrevTime은 음성(가설 자체가 silent 원인은 아니나, scrub-suppress-notifies 라는 다른 정상 동작 기제로 작동 — 자세한 내용은 V5 분석 참조).
- **근거**: V1(SyncPlayback의 무조건 pause), V2(Play 버튼이 component `bPaused`를 풀지 않음), V3(매 프레임 SyncPlayback이 마지막에 pause 강제), V4(early return 시 `TriggeredNotifiesThisFrame` clear만 남고 dispatch 미도달).
- **Stage 2 진행 여부**: 진행함. H-Pause 대응 옵션만 발굴 (H-PrevTime은 음성이라 옵션 불필요).
- **발굴된 fix 옵션 수**: 5개 (Option 1~5).
- **부수 발견**: `AnimSequenceEditorTab`은 "tab이 시간을 소유 + 매 프레임 component에 push" 패턴인 반면, 같은 SkeletalEditor의 `SkeletalMeshEditorTab`은 "component가 시간을 소유 + 버튼이 component bPaused/Speed 직접 변경" 패턴 — 두 tab의 playback 모델이 다름. 본 차이는 Option 5 설계 근거.

---

## 2. Stage 1 — 가설 검증

### V1: SyncPlaybackToComponent 동작

- **위치**: [AnimSequenceEditorTab.cpp:330-340](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:330)
- **전체 발췌**:
```cpp
void FAnimSequenceEditorTab::SyncPlaybackToComponent()
{
    USkeletalMeshComponent* Comp = PreviewScene.PreviewMeshComponent;
    if (!Comp || !DataSource) return;
    if (AnimSequence && Comp->GetAnimation() != AnimSequence)
    {
        Comp->SetAnimation(AnimSequence);
    }
    Comp->SetBakedAnimPaused(true);
    Comp->SetBakedAnimTime(CurrentTime);
}
```
- **무조건 pause인가**: ✓ 무조건. 분기 없음. `bPlaying`/`PlayRate` 등 tab 상태 일체 확인하지 않고 line 338에서 항상 `true`.
- **false 분기 존재 여부**: 같은 함수 내에 `SetBakedAnimPaused(false)` 호출 없음. AnimSequenceEditorTab.cpp 파일 전체로 grep해도 `SetBakedAnimPaused(false)` 매치 0건 (`SetBakedAnimPaused(false)`는 `SkeletalMeshEditorTab.cpp:843,864`에만 존재 — 다른 tab).
- **caller 목록** (grep `SyncPlaybackToComponent`):
  - 선언: [AnimSequenceEditorTab.h:46](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.h:46)
  - 정의: [AnimSequenceEditorTab.cpp:330](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:330)
  - 호출: [AnimSequenceEditorTab.cpp:327](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:327) — `OnTickPreview` 끝부분에서 단 한 곳에서 호출. caller가 단일.

### V2: Play/Pause/Stop 버튼 경로

- **Play / Pause 버튼 처리** ([AnimSequenceEditorTab.cpp:797-806](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:797)):
```cpp
// ▶ / ⏸
{
    const bool bForwardPlaying = bPlaying && PlayRate >= 0.0f;
    const EAnimToolIcon PlayIcon = bForwardPlaying ? EAnimToolIcon::Pause : EAnimToolIcon::Play;
    if (DrawAnimIconButton("##PlayPause", PlayIcon, true, bForwardPlaying ? "Pause" : "Play"))
    {
        if (bForwardPlaying) bPlaying = false;
        else { PlayRate = std::abs(PlayRate); bPlaying = true; }
    }
}
```
  - **관찰**: tab 멤버 `bPlaying`만 토글. **component의 `SetBakedAnimPaused(false)` 호출 없음**.
- **Reverse 버튼** ([AnimSequenceEditorTab.cpp:778-787](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:778)): 동일하게 `bPlaying` + `PlayRate` 부호만 변경. component side 직접 호출 없음.
- **Stop 버튼**: 별도 Stop 버튼 미존재. `bPlaying=false`는 다음 위치에서도 발생 — Step back/Step forward([AnimSequenceEditorTab.cpp:773,815](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:773)), 그리고 OnTickPreview 비-loop 종단([AnimSequenceEditorTab.cpp:323-324](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:323)). 어느 위치도 component bPaused 직접 변경 없음.
- **Play 상태 per-frame tick 존재 여부**: ✓ 존재 — [AnimSequenceEditorTab.cpp:308-328](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:308) `OnTickPreview`:
```cpp
void FAnimSequenceEditorTab::OnTickPreview(float DeltaTime)
{
    if (!DataSource) return;

    const float Duration = DataSource->GetDuration();
    if (bPlaying && Duration > 0.0f)
    {
        CurrentTime += DeltaTime * PlayRate;
        if (bLooping)
        {
            CurrentTime = std::fmod(CurrentTime, Duration);
            if (CurrentTime < 0.0f) CurrentTime += Duration;
        }
        else
        {
            if (CurrentTime <= 0.0f)      { CurrentTime = 0.0f;     bPlaying = false; }
            else if (CurrentTime >= Duration) { CurrentTime = Duration; bPlaying = false; }
        }
    }
    SyncPlaybackToComponent();
}
```
  - **관찰**: tab의 **로컬 `CurrentTime`만 `DeltaTime * PlayRate` 만큼 진행**. 그 후 `SyncPlaybackToComponent()`를 무조건 호출하여 component에 push.

### V3: 호출 순서

- **Tick/Draw 호출 순서** ([SkeletalEditorTab.cpp:623-625](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorTab.cpp:623)):
```cpp
PreviewScene.Tick(DeltaTime);
UpdateBoneDebugLines();
OnTickPreview(DeltaTime);
```
  - `PreviewScene.Tick` → [SkeletalEditorPreviewScene.cpp:126-134](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorPreviewScene.cpp:126) → `PreviewWorld->Tick` → [World.cpp:479](KraftonEngine/Source/Engine/GameFramework/World.cpp:479) `TickManager.Tick` → 결국 `USkeletalMeshComponent::TickComponent` → `AnimInstance->Update(DeltaTime)`.
  - 그 다음 `OnTickPreview` → 위 V2 발췌 → `SyncPlaybackToComponent`로 `SetBakedAnimPaused(true)` + `SetBakedAnimTime(CurrentTime)`.

- **SyncPlaybackToComponent의 상대 위치**: Frame N의 **끝부분**에서 호출 → component의 bPaused/time 상태를 frame N+1을 위해 setup.

- **분석**:
  - Frame N: `AnimInstance->Update` 실행 시점에는 **frame N-1의 OnTickPreview가 설정한 `bPaused=true` 상태**. 따라서 항상 [AnimInstance.cpp:41-47](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:41) early return으로 진입.
  - **결론**: tab의 `bPlaying` 토글은 component의 `bPaused`에 영향 없음. tab은 자신의 로컬 `CurrentTime`만 진행시키고 component에 push 하므로, 본질적으로 component를 "passive pose evaluator" 로 사용. Notify dispatch 경로는 절대 도달하지 않음.

### V4: bPaused early return 동작

- **함수 전체 발췌** (앞부분만 — H2 dispatch 분기는 D1에서 확인 완료):
```cpp
// AnimInstance.cpp:29-47
void UAnimInstance::Update(float DeltaTime)
{
    LastDeltaTime = bPaused ? 0.0f : DeltaTime;
    TriggeredNotifiesThisFrame.clear();          // line 32 — early return 이전

    const float Length = GetEffectivePlayLength();
    if (Length <= 0.0f)
    {
        PreviousTime = CurrentTime = 0.0f;
        return;
    }

    if (bPaused)
    {
        // Paused 상태에서도 PreviousTime을 정렬해 두면 외부에서 시간을 강제 변경한 직후
        // Notify 판정에 잘못된 prev/curr 간격이 잡히지 않는다.
        PreviousTime = CurrentTime;              // line 45
        return;                                  // line 46
    }
    // ... (이후 시간 진행 + Notify 판정 — pause 시 도달 불가)
```
- **pause 시 작업**: ① `LastDeltaTime=0` ② `TriggeredNotifiesThisFrame.clear()` ③ `PreviousTime = CurrentTime` ④ early return. 다른 부수 작업 없음.
- **TriggeredNotifiesThisFrame clear 위치**: **line 32, early return 이전**. 즉 pause된 frame에서는 clear 후 dispatch path에 진입하지 못함.
- **pause된 frame의 결과 상태**: `TriggeredNotifiesThisFrame`은 **빈 상태**(clear됨)로 끝남. `DispatchTriggeredNotifies`는 호출 안 됨 (line 95-98의 `if (!LocalTriggered.empty()) DispatchTriggeredNotifies(...)`에 도달 불가).
- **부수 관찰**: line 45의 `PreviousTime = CurrentTime`은 pause 동안 외부 SetEvaluationTime 호출 직후 `IsTriggeredBetween`이 (구) PreviousTime 범위로 잘못 fire 되는 것을 막는 정상 의도. V5의 H-PrevTime 가설과 직접 연결되는 안전망.

### V5: SetBakedAnimTime 동작

- **위치**: [SkeletalMeshComponent.h:74-82](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:74) (inline 정의)
- **발췌**:
```cpp
void SetBakedAnimTime(float InTime)
{
    BakedAnimTime = InTime;
    if (AnimInstance)
    {
        AnimInstance->SetEvaluationTime(InTime);
        RefreshAnimationPose();
    }
}
```
  - `SetEvaluationTime` 정의 ([AnimInstance.h:63](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:63)):
```cpp
void SetEvaluationTime(float InTime) { PreviousTime = CurrentTime = InTime; }
```
- **CurrentTime만 갱신하는가**: 아니오. **PreviousTime도 같이 InTime으로 덮어씀** (한 줄 두 대입).
- **PreviousTime도 같이 갱신하는가**: ✓ 갱신. `PreviousTime = CurrentTime = InTime;` 형태로 양자 동시.
- **IsTriggeredBetween 판정에 미치는 영향**:
  - `IsTriggeredBetween(PreviousTime, CurrentTime, Length)`는 [AnimNotify.h:78-89](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:78)에서 `PreviousTime < TriggerTime && TriggerTime <= CurrentTime` (포워드 케이스) 또는 루프 wrap 케이스를 판정. **PrevTime == CurTime 이면 어떤 TriggerTime도 통과 못함** (closed-open interval 폭이 0).
  - 그러나 **이 자체가 silent root cause는 아님** — 이유:
    - 매 프레임 `AnimInstance::Update`가 `PreviousTime = CurrentTime` (line 49)를 먼저 백업한 다음 `CurrentTime`을 `DeltaTime * PlaybackSpeed`만큼 진행시킴. 즉 만약 bPaused=false 라면 Update 본체가 PrevTime↔CurTime 간격을 정상적으로 만들어줌.
    - SetEvaluationTime이 둘을 같이 박는 것은 **외부 jump/scrub 직후 한 frame 동안만** 영향. 그 다음 Update가 정상 interval로 회복.
  - 단, **bPaused=true 인 경우** Update 본체가 line 41-47에서 early return하므로 interval 회복이 없음. 따라서 **H-PrevTime은 H-Pause와 독립적으로는 silent를 만들지 않으나, H-Pause 하에서는 두 메커니즘이 결합되어 PrevTime=CurTime인 채로 dispatch path 자체가 차단됨**.
- **추가 관찰**: `SetEvaluationTime`이 PrevTime을 같이 박는 동작은 사용자가 timeline을 drag/seek 했을 때 그 사이의 모든 notify가 한꺼번에 재생되는 것을 막는 **의도된 scrub-suppress-notify 메커니즘**으로 해석 가능 — 이는 D7(reverse/seek policy) 범위에서 다룰 사안. 본 prompt scope에서는 silent 의 원인이 아닌 것으로 판정.

### V6: SkeletalMeshComponent → AnimInstance 진입점

- **위치/발췌** ([SkeletalMeshComponent.cpp:390-410](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:390)):
```cpp
void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                           FActorComponentTickFunction &ThisTickFunction)
{
    if (!AnimInstance)
    {
        return;
    }

    AnimInstance->Update(DeltaTime);
    const uint64 PoseSamplingStartCycles = FPlatformTime::Cycles64();
    AnimInstance->EvaluateGraph();
#if STATS
    FSkinningStats::AddPoseSamplingTime(FPlatformTime::ToMilliseconds(FPlatformTime::Cycles64() - PoseSamplingStartCycles) * 0.001);
#endif

    ApplyEvaluatedPose(AnimInstance->GetOutputLocalPose());
    BakedAnimTime = AnimInstance->GetCurrentTime();  // line 409 — mirror back
}
```
- **preview/runtime 분기 여부**: ✗ 분기 없음. 같은 함수, 같은 `AnimInstance->Update(DeltaTime)` 호출. R5(preview/game 둘 다 dispatch) 정책과 정합.
- **부수 관찰**: line 409에서 `BakedAnimTime = AnimInstance->GetCurrentTime()` — component가 AnimInstance의 CurrentTime을 mirror back. 즉 AnimInstance가 시간을 주체적으로 진행할 수 있는 인프라는 갖춰져 있다(Option 5 근거).

### 가설 판정

- **판정**: **H-Pause 확정**. H-PrevTime은 음성(별도 silent 원인이 아님 — V5 분석 참조).
- **근거**:
  - V1: `SyncPlaybackToComponent`이 매 호출 무조건 `SetBakedAnimPaused(true)` 강제.
  - V2: Play 버튼은 tab 로컬 `bPlaying`만 토글, component `bPaused`는 절대 false로 풀지 않음.
  - V3: 매 프레임 마지막에 `SyncPlaybackToComponent`가 호출되어 다음 프레임 `Update`가 항상 bPaused=true로 시작.
  - V4: bPaused=true 분기에서 `TriggeredNotifiesThisFrame.clear()` 후 early return. dispatch path 도달 불가.
  - V6: TickComponent에 분기 없으므로 preview/runtime 차이로 인한 silent가 아님 — 순전히 bPaused 상태 차이.
- **추가 관찰**:
  - `SkeletalMeshEditorTab.cpp:838-866`은 다른 패턴: Play 버튼이 `PreviewMeshComponent->SetBakedAnimPaused(false/true)` 직접 호출, tab은 로컬 `bPlaying`을 별도로 들고 있지 않음. 즉 SkeletalMesh 뷰어에서는 같은 issue가 없을 가능성 (별도 검증 외).
  - `SetBakedAnimPaused(false)` grep 결과 AnimSequenceEditorTab.cpp에 0건, SkeletalMeshEditorTab.cpp에 2건([line 843](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.cpp:843), [line 864](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.cpp:864)). 두 tab 간 playback 모델 불일치가 root cause의 구조적 원인.

---

## 3. Stage 2 — Fix 옵션 비교

H-PrevTime은 음성이므로 H-Pause 대응 옵션만 발굴.

### 3.1 H-Pause 대응 옵션

| # | 수정 위치 (파일:라인) | 수정 방향 | 장점 | 단점 / Side effect | Lua hook 정책 충돌 | 라벨 |
|---|---|---|---|---|---|---|
| 1 | [AnimSequenceEditorTab.cpp:338](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:338) | 무조건 `SetBakedAnimPaused(true)` → `SetBakedAnimPaused(!bPlaying)`. 한 줄 변경. | 단일 함수, 1줄, tab의 의도를 정확히 표현. 다른 viewer/runtime 영향 없음 (이 함수는 AnimSequenceEditorTab 한 곳에서만 호출). | 매 프레임 `Update`가 bPaused=false로 본체 진입 → `CurrentTime`이 `DeltaTime * PlaybackSpeed`(=1 기본)으로 진행. 그 후 OnTickPreview가 tab의 `CurrentTime` (=`DeltaTime * PlayRate`로 진행)을 push해서 다시 덮어씀. PlayRate≠1 일 때 한 frame 내 시간이 두 경로로 진행 → AnimInstance 본체가 진행시킨 작은 interval에서 notify 1번 fire, 그 후 tab이 더 큰 interval로 jump-set. PlayRate=1 일 때는 두 경로가 같은 양만큼 진행 → 잠재적으로 같은 notify가 두 번 잡힐 위험 (tab push 후 다음 frame Update 본체에서 다시). **추가 검증 필요**. | 없음. dispatch 함수 내부는 미수정, Lua 우선 분기 추가 위치(`DispatchTriggeredNotifies` 진입부)와 독립. | **가장 작은 변경** (근거: 1줄, 1함수, 1책임만 수정) |
| 2 | [AnimSequenceEditorTab.cpp:797-806](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:797) + line 338 | Play 버튼 분기에서 `Comp->SetBakedAnimPaused(false)` 직접 호출, Pause 분기에서 `true` 호출. **추가로** SyncPlayback에서 `SetBakedAnimPaused` 라인 삭제. | SkeletalMeshEditorTab 패턴과 일치. bPaused는 명시적 액션으로만 변경. | tab의 `bPlaying`과 component의 `bPaused`가 별도 진실 (예: tab 진입 시 둘이 어긋날 수 있음 — 초기화 코드도 같이 손봐야 함). Step/GoTo/click 같이 시간만 변경하는 액션도 bPaused 명시 변경 필요한지 검토 필요. | 없음. | (라벨 없음 — Option 5와 영역 중복) |
| 3 | [AnimInstance.cpp:41-47](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:41) | bPaused early return을 notify 판정 **뒤로** 이동. 시간 진행만 skip, 판정은 수행. | 엔진 측 변경 — 모든 viewer/runtime이 한 번에 혜택. | 본 시나리오에서는 무효: bPaused=true 인 동안 SetEvaluationTime이 PrevTime=CurTime로 박아두므로 IsTriggeredBetween의 interval이 0이 되어 어차피 fire 불가. V5 분석 결과로 옵션 자체가 silent 해소에 부족. 단독 채택 시 효과 없음. | 없음. | (탈락 — 단독으로 효과 없음) |
| 4 | [AnimInstance.h:63](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:63) + [AnimInstance.cpp:41-47](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp:41) | `SetEvaluationTime`을 CurrentTime만 갱신하도록 분리(`PreviousTime` 보존) + bPaused early return을 notify 판정 후로 이동. 외부 push가 PrevTime을 보존하면 그 사이 interval에서 notify가 발화. | bPaused와 무관하게 notify 동작. 가장 깊은 수준의 일관성. | scrub-suppress-notify (V5 의도) 가 깨짐 — timeline drag 시 사이의 모든 notify가 한꺼번에 발화. **D7 정책과 직접 충돌**. 다른 caller([EditorConsoleWidget.cpp:1443](KraftonEngine/Source/Editor/UI/EditorConsoleWidget.cpp:1443), [ContentBrowserElement.cpp:192](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:192), [LuaSkeletalMeshBindings.cpp:604](KraftonEngine/Source/Engine/Scripting/LuaSkeletalMeshBindings.cpp:604) 등)에도 시맨틱 변화 발생. | 없음 (dispatch 함수 미수정). 단 SetEvaluationTime 시맨틱 변화로 Lua 측 caller의 기대 동작이 바뀔 수 있음. | (탈락 — D7 정책 충돌, scope 초과) |
| 5 | [AnimSequenceEditorTab.cpp:308-340](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:308) 전체 패턴 변경 | tab의 로컬 `CurrentTime`/`bPlaying`/`PlayRate` 시간 진행 책임을 component로 위양. Play 버튼은 `SetBakedAnimPaused(false)` + `SetBakedAnimPlaybackSpeed(PlayRate)` 직접 호출. Step/GoTo 버튼은 `SetBakedAnimTime(target)` 직접 호출. `OnTickPreview`는 시간 진행 로직 제거하고 component의 `BakedAnimTime`을 query해서 UI 표시만. `SyncPlaybackToComponent`는 anim asset binding 확인 책임만 남기고 bPaused/time 라인 삭제. | 단일 진실원천 (component의 AnimInstance가 시간 소유). `SkeletalMeshEditorTab` 패턴과 일치하여 코드베이스 일관성. 매 프레임 push가 사라져 시간 진행이 1중. AnimInstance가 자신의 PlaybackSpeed로 직접 진행 → notify dispatch 정상 동작. | 변경 범위 큼 — `OnTickPreview`, `SyncPlaybackToComponent`, Play/Pause/Reverse 버튼, Step 버튼, GoTo 버튼, Loop 토글, Speed 슬라이더 등 다수 핸들러 손봐야 함. timeline drag/scrub 핸들러도 `SetBakedAnimTime`로 일원화 필요. **단, 모든 핸들러가 같은 파일 내에 있고 다른 viewer/runtime은 영향 없음**. | 없음. AnimInstance가 시간을 주체적으로 진행하는 구조는 향후 Lua hook이 `DispatchTriggeredNotifies` 진입부에 들어갈 때 자연스럽게 작동. | **가장 미래 정책 호환** (근거: AnimInstance가 시간 소유 → Lua hook이 AnimInstance 내부에서 작동할 때 시간 흐름이 정합); **가장 일관된 아키텍처** (근거: SkeletalMeshEditorTab과 모델 통일) |

### 3.2 H-PrevTime 대응 옵션

해당 없음 (Stage 1에서 음성 판정).

### 3.3 옵션 간 trade-off 요약

- **Option 1 (가장 작은 변경)**: 1줄 수정으로 silent 해소 가능성 있음. 단 PlayRate≠1 시 시간 진행이 두 경로(AnimInstance Update + OnTickPreview push)로 중복되어 같은 notify가 두 번 잡히거나 시간이 어긋날 위험. 본 옵션을 채택하면 **추가 검증**(중복 발화 + 시간 drift) 필요.
- **Option 2**: Option 1+버튼-측 명시화. 두 진실원천(`bPlaying` vs component `bPaused`) 문제가 남음. 일관성을 위해서는 Option 5 쪽으로 가는 게 자연스러움.
- **Option 3**: 단독 채택 시 silent 해소 안 됨 (V5 분석 결과). 탈락.
- **Option 4**: D7 정책 충돌. scope 초과. 탈락.
- **Option 5**: 변경 범위는 가장 크나 범위가 tab 한 파일에 국한되고 아키텍처가 가장 정합. 시간 중복 진행 문제 자체가 사라짐. 미래 Lua hook과도 자연 호환.
- **요약**: 사용자 선택은 "최소 변경 → Option 1(+추가 검증)" 과 "구조적 정합 → Option 5" 사이의 trade-off. Option 3/4는 객관적으로 탈락.

---

## 4. 본 진단에서 확인 불가로 남은 항목

- **Option 1 채택 시 중복 발화 검증**: AnimInstance::Update(DT) 본체와 OnTickPreview의 tab `CurrentTime += DT*PlayRate` 두 경로가 동시에 활성일 때, 동일 frame에 같은 notify가 두 번 처리되는지 / 시간이 drift하는지는 본 prompt에서는 검증하지 않았음. Option 1 채택 시 구현 prompt에서 별도 확인 필요.
- **D7 정책 외 reverse/seek 시 notify 동작**: V5 분석에서 SetEvaluationTime이 scrub-suppress-notify 안전망으로 작동한다고 관찰했으나, 본 prompt scope 외이므로 reverse playback / scrub 시 notify 동작 자체의 정확성은 검토하지 않음.
- **SkeletalMeshEditorTab의 notify 동작**: 같은 viewer 그룹의 다른 tab은 `SetBakedAnimPaused(false)` 패턴이라 본 issue 없을 가능성이 높으나, 실제 game-mode (R5 정책 상 양쪽 모두 dispatch) 와의 차이는 본 prompt에서 검증 외.

---

## 5. 사용자 후속 결정 항목

1. **fix 옵션 선택**: 본 문서 3.1 표에서 Option 1 또는 Option 5 중 채택할 옵션 선택. (Option 2~4는 위 분석에 따라 비추천 / 탈락이라는 객관적 평가가 있음).
2. **Option 1 채택 시**: 후속 구현 prompt에 "중복 발화 / 시간 drift 검증" 항목 명시 필요.
3. **Option 5 채택 시**: 후속 구현 prompt scope를 `AnimSequenceEditorTab.cpp` 한 파일로 한정하되, 손봐야 할 핸들러 목록(Play/Pause/Reverse/Step/GoTo/Loop/Speed/timeline drag)을 명시 — 본 문서 3.1 Option 5 열 참고.
4. **부차적 issue (Stage 1에서 발견)**:
   - `SetEvaluationTime`이 PrevTime=CurTime 으로 박는 동작은 scrub-suppress-notify 안전망 — D7 reverse/seek 정책 작업 시 본 동작의 의도성을 명시 보존해야 함.
   - `SkeletalMeshEditorTab`과 `AnimSequenceEditorTab`의 playback 모델 불일치 — 코드베이스 차원에서 일관성을 잡으려면 Option 5 방향이 자연스러우나, 본 prompt scope 외이므로 별도 결정 사항.
