# Animation 재생 정지 증상 진단 보고서

> 본 문서는 **진단 보고서**다. 코드는 수정하지 않는다. 마지막 절에 다음 단계 수정 방향만 권고로 적는다.
>
> 표기 규약: 확인 `[확인]` · 추측 `[추정]` · 미확인 `[미확인]`.

---

## 0. Context · 증상 정의

Step 2(SingleNode override, `1b8f766`) / Step 3(TickComponent skin trigger, `47d974c`) 가 모두 커밋 완료된 상태에서 다음 증상이 보고됨:

- **메시 렌더링은 정상** — 메시 형태, 본 구조, 머티리얼 모두 화면 표시.
- **애니메이션이 bind pose 에서 움직이지 않음** — 시퀀스를 선택해도, Play 버튼을 눌러도 변화 없음.

메시 표시 자체가 정상이므로 렌더 패스 / GPU VB 생성 / draw command 발행은 모두 동작 중. 진단 범위는 "포즈가 매 프레임 갱신되어 GPU VB 에 반영되는 경로" 로 한정. 렌더 파이프라인(P5) 은 범위 밖.

---

## 1. 진단 경로 D0~D4 결과

| 단계 | 판정 | 핵심 근거 |
|---|---|---|
| D0 — viewer 재생 경로 | **끊김** | viewer 가 `PlayAnimation`/`Play` 미호출 |
| **D1** — AnimInstance 생성 | **첫 끊김 (결정적)** | `SetSkeletalMesh`/`SetAnimation` 둘 다 생성 책임 없음. `AnimInstance` 가 viewer 경로에서 영원히 `nullptr` |
| D2 — TickComponent 호출 | 가정 정상 `[미확인]` | D1 차단으로 도달해도 즉시 return |
| D3 — 시간 누적/평가 | **별도 끊김** | `SetBakedAnimPaused`/`SetBakedAnimTime`/`SetBakedAnimPlaybackSpeed` 가 legacy mirror 만 수정. AnimInstance 에 미전파 |
| D4 — 포즈 버퍼 전달 | **정상** | Step 3 의 `SkinVerticesToReferencePose`/`EnsureRuntimeResources`/`MarkWorldBoundsDirty` 호출 머지 후 보존 (`SkeletalMeshComponent.cpp:142-146`) |

→ **D1 이 첫 끊김**. D3 는 D1 을 해결해도 별도로 막는 연쇄 끊김.

---

## 2. 첫 끊김 — D1 (AnimInstance 미생성)

### 2.1 viewer 측 호출 지점 전부

| 위치 | 호출 | 비고 |
|---|---|---|
| `Editor/UI/SkeletalEditor/SkeletalEditorPreviewScene.cpp:95` | `PreviewMeshComponent->SetSkeletalMesh(InMesh)` | 메시만 세팅. AnimInstance 생성 책임 없음 |
| `SkeletalEditorPreviewScene.cpp:98-101` | `SetBakedAnimPaused(true)` / `SetBakedAnimTime(0.0f)` / `SetBakedAnimPlaybackSpeed(1.0f)` | **legacy mirror 만**. AnimInstance 미관여 |
| `Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.cpp:252, :277` | `PreviewMeshComponent->SetAnimation(CurrentSequence)` | `USkeletalMeshComponent::SetAnimation` 진입 — 아래 2.2 참조 |
| `SkeletalMeshEditorTab.cpp:305` | `SetBakedAnimPaused(!bPaused)` — Play 버튼 | **mirror 만 토글** |
| `SkeletalMeshEditorTab.cpp:310, :320` | `SetBakedAnimTime(...)` | **mirror 만** |
| `SkeletalMeshEditorTab.cpp:328` | `SetBakedAnimPlaybackSpeed(Speed)` | **mirror 만** |
| `Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:153, :217` | `Comp->SetAnimation(InSequence)` | 위와 동일 (`USkeletalMeshComponent::SetAnimation`) |
| `Editor/UI/ContentBrowser/ContentBrowserElement.cpp:192, :196` | `SetAnimation` + `EvaluateAnimationPose` | **thumbnail 용**. 다른 경로 — 본 viewer 증상과 무관 |

`[확인]` viewer 코드 전체에서 `PlayAnimation()` / `Play()` / `Stop()` 호출 **0건**. `EvaluateAnimationPose` 는 thumbnail 외 호출 없음.

### 2.2 AnimInstance 생성 책임 코드 근거

`AnimInstance` 멤버 초기화: `Component/SkeletalMeshComponent.h:94` — `UAnimInstance *AnimInstance = nullptr;`

생성하는 함수 (전부):

```cpp
// Component/SkeletalMeshComponent.cpp:39-60 — PlayAnimation
void USkeletalMeshComponent::PlayAnimation(UAnimationAsset *NewAnimToPlay, bool bLooping)
{
    AnimToPlay = NewAnimToPlay;
    if (!AnimInstance)
    {
        AnimInstance = UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>(this);
        AnimInstance->InitializeAnimation(ResolveSkeletonFromMesh(SkeletalMesh));
    }
    SetAnimation(AnimToPlay);
    AnimInstance->SetLooping(bLooping);
    AnimInstance->ResetTime();
    AnimInstance->SetPaused(false);
    ...
}

// Component/SkeletalMeshComponent.cpp:92-126 — EvaluateAnimationPose (thumbnail 등 별 경로)
bool USkeletalMeshComponent::EvaluateAnimationPose(...) {
    if (!AnimInstance) { CreateObject<UAnimSingleNodeInstance>(...); ... }
    ...
}
```

**[확인]** `SetSkeletalMesh` (`Component/SkinnedMeshComponent.cpp:30-54`): 메시 / 본 / 버퍼만 처리. **AnimInstance 생성 코드 없음.**

**[확인]** `USkeletalMeshComponent::SetAnimation` (`Component/SkeletalMeshComponent.cpp:62-70`):
```cpp
void USkeletalMeshComponent::SetAnimation(UAnimationAsset *NewAnimToPlay)
{
    AnimToPlay = NewAnimToPlay;
    if (auto *Single = Cast<UAnimSingleNodeInstance>(AnimInstance))  // ← AnimInstance가 null이면 이 분기 진입 안 함
    {
        Single->SetAnimation(Cast<UAnimSequence>(NewAnimToPlay));
    }
}
```
→ `AnimInstance` 가 null 이면 **시퀀스 set 도 no-op**. `AnimToPlay` 멤버만 갱신될 뿐.

### 2.3 영향 체인

```
viewer 가 PlayAnimation 미호출
  └─ AnimInstance == nullptr 유지
       └─ TickComponent 진입 즉시 return
              (SkeletalMeshComponent.cpp:131-134)
            └─ LocalBonePoseMatrices 갱신 안 됨
                 └─ RebuildMeshSpaceBoneMatrices 미호출
                      └─ SkinVerticesToReferencePose 미호출
                           └─ EnsureRuntimeResources 미호출
                                └─ UploadSkinnedVertices 미호출
                                     └─ GPU VB 는 SetSkeletalMesh 시점의 bind pose
                                           → 화면이 bind pose 에 고정
```

GPU VB 자체는 `USkeletalMeshComponent::SetSkeletalMesh` → `EnsureRuntimeResources` (`SkinnedMeshComponent.cpp:51`) 경로로 최초 1회 생성되어 정상 표시. 그 뒤 갱신 경로(Tick) 가 막혀 bind pose 가 유지된다.

---

## 3. 부차 끊김 — D3 (mirror ↔ AnimInstance 불일치)

D1 차단을 해결해도 별도로 막는 두 번째 끊김.

### 3.1 setter 본문

```cpp
// Component/SkeletalMeshComponent.h:56-77 — 모두 인라인 setter
void SetBakedAnimTime(float InTime)          { BakedAnimTime = InTime; }
void SetBakedAnimPaused(bool bInPaused)      { bBakedAnimPaused = bInPaused; }
void SetBakedAnimPlaybackSpeed(float InSpeed){ BakedAnimPlaybackSpeed = InSpeed; }
```

**[확인]** 세 setter 모두 mirror 멤버만 수정. `AnimInstance->SetPaused()` / `SetEvaluationTime()` / `SetPlaybackSpeed()` 로 전파하는 코드 **없음**.

### 3.2 viewer 컨트롤이 mirror 만 건드림

| 컨트롤 | 호출 | 결과 |
|---|---|---|
| Play / Pause 버튼 | `SetBakedAnimPaused(!bPaused)` (`SkeletalMeshEditorTab.cpp:305`) | `bBakedAnimPaused` 만 토글. `AnimInstance->bPaused` 는 default `true` 유지 |
| Reset | `SetBakedAnimTime(0.0f)` (`:310`) | `BakedAnimTime` 만 0. `AnimInstance->CurrentTime` 불변 |
| Time scrub slider | `SetBakedAnimTime(Time)` + `SetBakedAnimPaused(true)` (`:320-321`) | mirror 만 |
| Speed slider | `SetBakedAnimPlaybackSpeed(Speed)` (`:328`) | mirror 만 |

→ **D1 을 해결해 `AnimInstance` 가 생성되더라도 Play 버튼이 동작하지 않음. Scrub/Speed 슬라이더도 인스턴스에 반영 안 됨.** 별도 수정 필요.

### 3.3 base 의 `bPaused` 기본값

`Asset/Animation/Core/AnimInstance.h:104` — `bool bPaused = true;` 가 **기본값**.

`UAnimInstance::Update`(`AnimInstance.cpp:37-43`)은 `bPaused` 시 시간 누적 없이 early return. 즉 D3 의 mirror 불일치가 해결되지 않으면 `bPaused` 가 영원히 true 로 남아 평가만 매 틱 동일 포즈로 반복.

---

## 4. 정상 단계 확인

### 4.1 D2 (TickComponent 호출)

`Editor/UI/SkeletalEditor/SkeletalEditorPreviewScene.cpp:126-134`:
```cpp
void FSkeletalEditorPreviewScene::Tick(float DeltaTime)
{
    if (!PreviewWorld) return;
    PreviewWorld->Tick(DeltaTime, DeltaTime, LEVELTICK_ViewportsOnly);
}
```
`PreviewActor->bTickInEditor = true` 설정(`:30`). **[추정]** 정상이라면 World → Actor → Component tick 디스패치 체인을 통해 `TickComponent` 까지 도달.

**[미확인]** `World->Tick → Actor tick → Component TickComponent` 의 실제 호출 흐름은 본 진단에서 직접 추적하지 않음. D1 이 차단하므로 D2 가 정상 동작해도 증상은 동일. **D1 해결 후 별도 검증** 필요.

### 4.2 D4 (포즈 버퍼 → GPU VB)

`Component/SkeletalMeshComponent.cpp:142-146` — Step 3 의 호출 추가가 머지 후 보존:
```cpp
RebuildMeshSpaceBoneMatrices();
SkinVerticesToReferencePose();
EnsureRuntimeResources();
MarkWorldBoundsDirty();
```
호출 순서는 `SkinnedMeshComponent.cpp:113-116` 의 `SetBoneLocalPose` 패턴과 동일. **[확인]** D4 자체는 끊김 없음. AnimInstance 가 생성되어 `OutputLocalPose` 를 채우기만 하면 그 후 GPU VB 까지 정상 전달된다.

---

## 5. 권장 진단 방법 (코드 미수정 — 임시 로그 제안만)

D1 / D3 가 코드만으로 확정됐지만, 실제 런타임 동작을 한 번 더 확인하려면 다음 4개 값을 임시 로그로 출력하는 것이 가장 빠르다. 이번 보고에서는 **제안만**:

| 위치 | 출력값 | 판별 |
|---|---|---|
| `USkeletalMeshComponent::TickComponent` 진입 (`SkeletalMeshComponent.cpp:128`) | `AnimInstance == nullptr` 여부 | true 면 D1 확정 |
| `UAnimSingleNodeInstance::EvaluateGraph` 진입 (`AnimSingleNodeInstance.cpp:30+`) | `bPaused`, `CurrentTime`, `CurrentSequence == nullptr` | 진입 자체가 안 되면 D1·D2. 진입했으나 `bPaused==true` 면 D3 |
| `EvaluateGraph` 종료 직전 | `OutputLocalPose[0]` 원소값 | bind pose `LocalBindPose[0]` 과 동일하면 평가가 bind pose 로 빠진 것 |

위 네 값을 1프레임만 잡으면 D1 vs D3 가 한 번에 갈린다. **본 진단 예상**: TickComponent 진입 첫 줄에서 `AnimInstance == nullptr` 로그가 매 프레임 찍힘 → D1 확정 → 이후 줄들은 도달하지 않음.

---

## 6. merge 영향 요약

| 커밋 | 영향 | 평가 |
|---|---|---|
| `c996372` "Fix: RawAnimation 데이터 TRS Key 기반으로 변경" | D3/D4 의 평가·스킨 로직 정상화 | 양성 — 끊김 발생 원인 아님 |
| `d88fb2a` "TRS extraction pull" | importer TRS 채움 | 양성 |
| `1b8f766` "singlenodeinstance structure fix WIP" (Step 2) | SingleNode override 도입 | 양성 |
| `47d974c` "component tick trigger fix" (Step 3) | TickComponent skin trigger | 양성 — D4 완성 |
| viewer 머지 `[추정]` (별도 작업) | `SetBakedAnimPaused`/`SetBakedAnimTime` 등 mirror 컨트롤 추가. `AnimInstance` 연결을 누락 | **음성 — 이번 증상의 직접 원인** |

`[추정]` 호출 누락이 viewer 머지 시점에 도입된 것인지 원래부터 그랬는지는 본 진단에서 git blame 으로 직접 확인하지 않음. 다만 mirror 시스템(`bBakedAnim*`) 자체가 `AnimInstance` 도입 이전의 legacy 라는 점에서 머지 시 연결을 깜빡한 가능성이 높음.

---

## 7. 다음 단계 수정 방향 (수정은 별도 작업)

### 7.A (우선) D1 해결 — AnimInstance 생성 보장

세 가지 가능한 방향. 어느 것을 채택할지는 별도 결정 사항.

| 안 | 위치 | 변경 | 트레이드오프 |
|---|---|---|---|
| **(가)** viewer 가 `PlayAnimation` 호출 | `SkeletalEditorPreviewScene.cpp:95` 직후 또는 `SetAnimation` 호출 지점 | `PreviewMeshComponent->PlayAnimation(Sequence, bLooping)` 로 교체 | viewer 측 1~2줄 수정. 의도가 명확. 단 viewer 가 `bPaused=false` 로 시작하므로 PreviewScene:98 의 일시정지 정책과 충돌 — 호출 순서 또는 즉시 `Stop()` 으로 조정 필요 |
| **(나)** `SetAnimation` 본문에서 AnimInstance lazy 생성 | `SkeletalMeshComponent.cpp:62-70` | 진입부에 `if (!AnimInstance) { CreateObject<UAnimSingleNodeInstance>(...); InitializeAnimation(...); }` 추가 | 컴포넌트 측 4~5줄 수정. viewer / 다른 호출자 모두 자동 혜택. 부작용 가능성: 게임 런타임 spawn 시 의도치 않게 AnimInstance 가 생성될 수 있음 `[미확인]` |
| **(다)** `SetSkeletalMesh` 직후 자동 생성 | `SkinnedMeshComponent.cpp:30-54` 또는 `SkeletalMeshComponent` 에서 override | 메시 set 시점에 빈 AnimInstance 생성 | 모든 컴포넌트가 항상 AnimInstance 보유 — 단순. 단 메모리 / 의도 측면에서 가장 강한 변경. 게임 런타임 영향 평가 필요 |

**[추정]** 권고: **(나) 채택**. 이유는 (1) 변경 범위가 viewer 와 무관한 컴포넌트 1곳, (2) viewer 의 정지 정책(PreviewScene:98) 과 충돌 없음, (3) `SetAnimation` 시점이라는 의도가 명확한 트리거, (4) `EvaluateAnimationPose`(`:100-104`) 도 이미 같은 lazy 패턴을 쓰고 있어 일관성. 단 최종 결정은 별도 작업.

### 7.B (후속) D3 해결 — mirror ↔ AnimInstance 동기화

세 setter 본문에서 `AnimInstance` 가 존재할 때 함께 전파:

```cpp
// 의도 예시 — 실제 구현은 별도 작업
void SetBakedAnimPaused(bool bInPaused) {
    bBakedAnimPaused = bInPaused;
    if (AnimInstance) AnimInstance->SetPaused(bInPaused);
}
void SetBakedAnimTime(float InTime) {
    BakedAnimTime = InTime;
    if (AnimInstance) AnimInstance->SetEvaluationTime(InTime);
}
void SetBakedAnimPlaybackSpeed(float InSpeed) {
    BakedAnimPlaybackSpeed = InSpeed;
    if (AnimInstance) AnimInstance->SetPlaybackSpeed(InSpeed);
}
```

또는 더 깊은 정리로 mirror 시스템 자체를 제거하고 viewer 가 `AnimInstance` 를 직접 조작 — 단 viewer 코드 변경 범위가 커진다. 트레이드오프 평가는 별도.

### 7.C 부수 정리 (선택 — 무관)

- `Component/SkeletalMeshComponent.cpp:11, :13` 의 `#include <cmath>` 중복. 컴파일 무해이나 정리.
- `ApplyBakedAnimation`(`:157-160`) 가 빈 시그니처. 호출처가 없으면 제거. 호출처가 남아 있다면 mirror 시스템 정리와 함께 검토.

---

## 8. 한계 / 추측 / 미확인 사항

| ID | 내용 | 표기 |
|---|---|---|
| L1 | `PreviewWorld->Tick → Actor tick → Component TickComponent` 디스패치 흐름을 본인 직접 추적 미수행. D1 차단으로 도달 여부 무관하나, D1 해결 후 별도 검증 필요 | `[미확인]` |
| L2 | viewer 측 호출 누락이 머지 시점 도입인지 원래부터인지 git blame 으로 추적 미수행 | `[추정]` |
| L3 | `SetSkeletalMesh` 직후 또는 `SetAnimation` lazy 로 AnimInstance 를 만들었을 때 게임 런타임 spawn 등 다른 컴포넌트 사용자에 부작용이 있는지 미평가 | `[미확인]` |
| L4 | D3 의 setter 동기화 안 vs mirror 시스템 제거 안 트레이드오프는 본 보고서에서 결정하지 않음 | 결정 별도 |
| L5 | `EvaluateAnimationPose`(`:92-126`) thumbnail 경로는 자체적으로 AnimInstance 를 lazy 생성하므로 본 증상과 독립. 단 이 경로가 viewer 와 컴포넌트 인스턴스를 공유하는지 여부 미확인 | `[미확인]` |
