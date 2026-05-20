# AnimNotify P-Fix5 구현 산출물 — AnimSequenceEditorTab Playback 모델 Component 소유로 전환

D2 진단([Document/animnotify_diagnosis_D2.md](animnotify_diagnosis_D2.md)) Option 5 채택 후속 구현 결과.

작업 일자 컨텍스트: 2026-05-20 working tree.

---

## 1. 배경 요약

- **D1 진단**([Document/animnotify_diagnosis_D1.md](animnotify_diagnosis_D1.md)): P1~P5 5 layer 인프라 완전 존재, H2(Sound dispatch → `FSoundManager::Get().PlayEffect`) 통과. silent 원인은 dispatch 함수 내부가 아니라 상류로 판정.
- **D2 진단**([Document/animnotify_diagnosis_D2.md](animnotify_diagnosis_D2.md)): H-Pause 확정. `SyncPlaybackToComponent`이 매 프레임 무조건 `Comp->SetBakedAnimPaused(true)`로 강제 → `UAnimInstance::Update`가 early return → dispatch 미도달.
- **채택 방안**: D2 Option 5 — tab의 시간/playback 소유를 component(`USkeletalMeshComponent` + `UAnimInstance`)로 위양.
- **scope**: `AnimSequenceEditorTab.{h,cpp}` 한 파일군. 다른 파일 미수정.

---

## 2. Stage A 결정 사항 (구현 전 사용자 합의)

| 결정 항목 | 채택안 |
|---|---|
| 부재 API `SetBakedAnimLooping`/`IsBakedAnimLooping` 처리 | (b) `Comp->GetAnimInstance()->SetLooping(b)` / `IsLooping()` 직접 호출 — scope 내, AnimInstance public API 활용 |
| `OnTickPreview` 처리 | 빈 함수로 유지 (시간 진행 로직 제거, anim binding 확인만 위해 `SyncPlaybackToComponent()` 단일 호출 보존) |
| `SyncPlaybackToComponent` 처리 | 이름 유지, `SetBakedAnimPaused`/`SetBakedAnimTime` 라인만 삭제. anim asset binding 확인 책임만 남김 |

---

## 3. 구현 내용

### 3.1 [AnimSequenceEditorTab.h](../KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.h) — 멤버 4개 삭제

**삭제된 멤버**:
```cpp
float CurrentTime = 0.0f;   // → Comp->GetBakedAnimTime() / SetBakedAnimTime()
bool  bPlaying    = false;  // → Comp->IsBakedAnimPaused() 반전 / SetBakedAnimPaused()
bool  bLooping    = true;   // → Comp->GetAnimInstance()->IsLooping() / SetLooping()
float PlayRate    = 1.0f;   // → Comp->GetBakedAnimPlaybackSpeed() / SetBakedAnimPlaybackSpeed()
```

**유지된 멤버**: `bRecording`(UI placeholder)는 유지.

### 3.2 [AnimSequenceEditorTab.cpp](../KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp) — 핸들러별 전환

#### 3.2.1 `OpenAnimSequenceAsset` 초기화
- 삭제: tab 멤버 4개 초기화 라인.
- 추가: `Comp->GetAnimInstance()->SetLooping(true)` (looping 초기값을 AnimInstance에 명시).
- 유지: `Comp->SetBakedAnimTime(0.0f)` + `SetBakedAnimPlaybackSpeed(1.0f)` + `SetBakedAnimPaused(true)` (기존부터 component-side로 초기화하던 패턴).

#### 3.2.2 `OnTickPreview` — 시간 진행 로직 제거
**변경 전**: 매 frame `CurrentTime += DeltaTime * PlayRate` + loop wrap + 비-loop 종단 처리.
**변경 후**:
```cpp
void FAnimSequenceEditorTab::OnTickPreview(float DeltaTime)
{
    (void)DeltaTime;
    if (!DataSource) return;
    // 시간 진행 책임은 USkeletalMeshComponent + UAnimInstance가 소유한다.
    // 본 hook은 anim asset binding 유지만 확인한다.
    SyncPlaybackToComponent();
}
```

#### 3.2.3 `SyncPlaybackToComponent` — pause/time 강제 라인 삭제
**변경 전**:
```cpp
Comp->SetBakedAnimPaused(true);     // ← 매 frame H-Pause silent 원인
Comp->SetBakedAnimTime(CurrentTime);
```
**변경 후**: 위 두 라인 삭제. `SetAnimation` 분기만 남음 — anim asset binding 확인 책임만 보유.

#### 3.2.4 `RenderPlaybackControls` — 8개 버튼 핸들러 전환

| 버튼 | 변경 후 동작 |
|---|---|
| GoToFront `\|<` | `Comp->SetBakedAnimTime(0)` + `SetBakedAnimPaused(true)` |
| StepBack `<\|` | `Comp->SetBakedAnimTime(max(0, T - FrameStep))` + `SetBakedAnimPaused(true)` |
| Reverse `<<` | toggle: `SetBakedAnimPlaybackSpeed(-abs(speed))` + `SetBakedAnimPaused(false)` ↔ `SetBakedAnimPaused(true)` |
| Play `▶` / Pause `\|\|` | toggle: `SetBakedAnimPlaybackSpeed(abs(speed))` + `SetBakedAnimPaused(false)` ↔ `SetBakedAnimPaused(true)` |
| StepFwd `\|>` | `Comp->SetBakedAnimTime(min(Duration, T + FrameStep))` + `SetBakedAnimPaused(true)` |
| GoToEnd `>\|` | `Comp->SetBakedAnimTime(Duration)` + `SetBakedAnimPaused(true)` |
| Loop 토글 | `Comp->GetAnimInstance()->SetLooping(!IsLooping())` (Stage A (b) 결정안) |
| Speed 슬라이더 | `Comp->SetBakedAnimPlaybackSpeed(...)` (부호는 현재 speed로 결정) |

표시값은 모두 매 frame component query: `Comp->GetBakedAnimTime()`, `IsBakedAnimPaused()`, `GetBakedAnimPlaybackSpeed()`, `GetAnimInstance()->IsLooping()`.

#### 3.2.5 `RenderTimelinePanel` — 시간 표시 + scrub drag + Add Notify
- 함수 진입부에 `const float CurrentTime = Comp ? Comp->GetBakedAnimTime() : 0.0f` 로컬 변수 도입 — 본 함수 내 기존 `CurrentTime` 참조(frame 라벨, scrub head, Add Notify의 TriggerTime)는 별도 수정 없이 새 로컬 변수로 자연스럽게 해석됨.
- Scrub drag 핸들러:
  ```cpp
  else if (bScrubbing) {
      if (Comp) {
          Comp->SetBakedAnimTime(SnapT);
          Comp->SetBakedAnimPaused(true);
      }
  }
  ```

#### 3.2.6 `RenderNotifyPropertyInline` — Notify "Go" 버튼
```cpp
if (ImGui::Button("Go")) {
    if (USkeletalMeshComponent* Comp = PreviewScene.PreviewMeshComponent) {
        Comp->SetBakedAnimTime(Edited.TriggerTime);
        Comp->SetBakedAnimPaused(true);
    }
}
```

---

## 4. Before/After — 시간 흐름

### Before (silent 원인 모델)
```
Frame N:
  PreviewScene.Tick → World.Tick → Component.TickComponent
    → AnimInstance::Update(DT)
       ├ bPaused=true (이전 frame에서 set됨)
       ├ TriggeredNotifiesThisFrame.clear()
       ├ PreviousTime = CurrentTime
       └ early return  ← dispatch 미도달
  OnTickPreview
    ├ tab.CurrentTime += DT * PlayRate
    └ SyncPlaybackToComponent
        ├ Comp->SetBakedAnimPaused(true)  ← H-Pause 강제
        └ Comp->SetBakedAnimTime(CurrentTime)  ← PrevTime=CurTime 박음
```

### After (component 소유 모델)
```
Frame N:
  PreviewScene.Tick → World.Tick → Component.TickComponent
    → AnimInstance::Update(DT)
       ├ bPaused 상태 = 직전 button 액션에 따라 결정
       ├ TriggeredNotifiesThisFrame.clear()
       ├ (!bPaused 경로) PreviousTime = CurrentTime, CurrentTime += DT * PlaybackSpeed
       ├ IsTriggeredBetween(PrevTime, CurTime, Length) 판정
       └ DispatchTriggeredNotifies → Sound case → FSoundManager::Get().PlayEffect ✓
  OnTickPreview
    └ SyncPlaybackToComponent → SetAnimation 분기만 (binding 유지 확인)
```

---

## 5. 검증 결과

### 5.1 정적 grep 검증
| 검사 | 결과 |
|---|---|
| tab 멤버 `bPlaying`/`PlayRate`/`bLooping`/`CurrentTime` 참조 | ✓ 0건 (헤더/소스 모두). 동명의 RenderTimelinePanel/RenderPlaybackControls 로컬 변수는 의미가 다름. |
| `SyncPlaybackToComponent` 본체에 `SetBakedAnimPaused`/`SetBakedAnimTime` 호출 | ✓ 0건. anim asset binding 확인만 남음. |
| `OnTickPreview` 본체에 시간 진행 로직 | ✓ 0건. SyncPlaybackToComponent 단일 호출만 남음. |
| Play 버튼에 `SetBakedAnimPaused(false)` 직접 호출 | ✓ 존재 |
| Pause 버튼에 `SetBakedAnimPaused(true)` 직접 호출 | ✓ 존재 |
| AnimSequenceEditorTab 외 파일 수정 | ✓ 0건 (scope 준수) |

### 5.2 빌드 검증
- MSBuild로 `KraftonEngine.sln` `KraftonEngine` 프로젝트 Debug|x64 빌드 성공.
- 산출물: `C:\GitDirectory11\KraftonEngine\Bin\Debug\KraftonEngine.exe`
- 컴파일 에러 0건.

---

## 6. 보존된 정책

| 정책 | 보존 방식 |
|---|---|
| **D7** — reverse/seek 시 notify 발화 정확성 | `SetBakedAnimTime` → `SetEvaluationTime`이 `PrevTime=CurTime` 동시 박는 V5 안전망 미손상. scrub drag/Go 버튼/Step 등 시간 jump 동작은 모두 `SetBakedAnimTime` 경유 → 사이 구간 notify 발화 자동 suppress. |
| **D2 부분 무효화** — 향후 Lua 우선/C++ fallback hook | AnimInstance가 시간을 직접 소유한 채로 `Update → DispatchTriggeredNotifies` 경로가 정상 작동. hook 삽입 위치(`DispatchTriggeredNotifies` 진입부)에 영향 없음. |
| **R5** — preview/game 둘 다 dispatch | `AnimSequenceEditorTab.{h,cpp}` 외 파일 미수정. game 경로([SkeletalMeshComponent.cpp:398](../KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:398) `AnimInstance->Update`)는 그대로. |

---

## 7. Smoke test 가이드

editor를 켜서 다음 5개 항목 확인:

1. **Silent 해소 (본 fix의 주 목적)**: Sound notify가 포함된 `.anm` asset을 ContentBrowser에서 더블클릭 → AnimSequenceEditorTab에서 ▶ Play. timeline scrub head가 notify marker를 지나칠 때 **소리가 들리는지** 확인.
2. **Pause / Resume**: ▶ Play 재생 중 ⏸ Pause 토글 (아이콘 변경 + 시간 정지). 다시 클릭 시 resume.
3. **Step / GoTo / Reverse**:
   - GoToFront `|<` → CurrentTime=0, paused
   - StepBack `<|` / StepFwd `|>` → 한 프레임씩 이동
   - GoToEnd `>|` → CurrentTime=Duration
   - Reverse `<<` → playback speed 음수, 시간 역행
4. **Loop 토글**: 비루프 + 끝까지 재생 → 끝에서 멈춤. 루프 + 끝까지 재생 → wrap되어 0부터 다시 시작.
5. **Timeline scrub & Notify 편집**:
   - timeline content 영역 좌클릭 drag → scrub head 따라옴 + 자동 pause
   - 빈 영역 우클릭 → "Add Sound Notify at current frame" → 현재 scrub 위치에 notify 생성
   - Notify 선택 후 inline "Go" 버튼 → scrub head가 notify TriggerTime으로 이동 + paused

---

## 8. 변경 파일 목록 (최종)

| 파일 | 변경 요약 |
|---|---|
| [AnimSequenceEditorTab.h](../KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.h) | 시간/playback 멤버 4개 삭제 (`CurrentTime`/`bPlaying`/`bLooping`/`PlayRate`) |
| [AnimSequenceEditorTab.cpp](../KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp) | init 멤버 초기화 4줄 삭제 + Looping init 4줄 추가; `OnTickPreview` 시간 진행 17줄 삭제; `SyncPlaybackToComponent` pause/time 강제 2줄 삭제; `RenderPlaybackControls` 8개 핸들러 component 호출로 재작성; `RenderTimelinePanel` 로컬 `CurrentTime` 도입 + scrub drag 핸들러 교체; `RenderNotifyPropertyInline` "Go" 버튼 교체 |

---

## 9. 후속 작업 후보 (본 fix 범위 외 — 참고)

- **D7 정책 작업**: reverse playback / seek 시 notify 발화 정확성 정밀화. 본 fix는 V5 안전망을 손상시키지 않았으므로 D7 작업의 출발점 그대로 보존.
- **Lua hook 추가**: `DispatchTriggeredNotifies` 진입부에 Lua 우선 분기 신설. 본 fix가 AnimInstance 시간 소유 구조를 정합화하여 hook 삽입 시 자연 호환.
- **`SetBakedAnimLooping`/`IsBakedAnimLooping` 신설** (선택): API 일관성을 위해 Component에 looping setter/getter 신설. 현재는 `Comp->GetAnimInstance()->SetLooping(b)` 우회로 처리됨 — 다른 caller가 늘어나면 API 신설 가치 상승.
- **SkeletalMeshEditorTab과의 playback 모델 통일성 점검**: 본 fix로 AnimSequenceEditorTab은 SkeletalMeshEditorTab 패턴에 가까워졌으나, Loop 토글 처리 등 일부 차이 잔존. 통일 여부는 별도 결정 사항.
