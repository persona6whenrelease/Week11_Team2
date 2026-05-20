# Implementation Prompt P5 — T7 + T8: Sound / CameraShake 실제 호출 (Outer Chain)

## 목적

P4에서 stub log로 두었던 dispatch 분기를 실제 시스템 호출로 채운다:
- `Type=Sound` → `FSoundManager::Get().PlayEffect(SoundId)`
- `Type=CameraShake` → `ResolveCameraManager()` outer chain → `APlayerCameraManager::StartCameraShake(ShakeParams)`

## 전제

- **P1~P4 머지 완료**. dispatch 골격(`DispatchTriggeredNotifies`)이 stub log로 동작 중.
- 에디터에서 Sound/CameraShake notify를 만들고 저장/로드/재생까지 다 되는데, 효과만 안 나는 상태.

## 참조 문서

- 통합 설계: `Document/animnotify_integration_design.md`
  - 인용 섹션: **4 (Verify Result — Outer Chain)**, **5.4 (Layer 4 — ResolveCameraManager 의사 코드)**, **5.5 (Effect Dispatch Target)**, **T7, T8 (Section 7)**, **D2, D6**, **R2, R4, R5**

## 엄격한 제약

1. **본 PR 범위는 T7 + T8뿐**. 다른 layer는 손대지 않는다.
2. **D2 — 직접 호출**: AnimInstance가 outer chain을 직접 타고 올라간다. 콜백 주입 / 의존성 역전 / DI 패턴 도입 금지.
3. **R2 — nullptr 가드 필수**: outer chain의 5단계 각각에서 nullptr 체크. 어느 단계든 nullptr이면 silent no-op으로 종료. crash 절대 금지.
4. **R4 — 헤더 의존성 격리**: 새로 추가하는 include는 모두 `AnimInstance.cpp` 에만. `AnimInstance.h` 에는 `class APlayerCameraManager;` forward declaration만 추가 (helper 함수 선언에 필요한 경우).
5. **R5 — preview/game 가드 없음**: outer chain의 결과로 PlayerController가 nullptr이면 자연스럽게 no-op이 된다. world type을 명시적으로 확인하거나 분기하지 않는다.
6. **D6 — Sound 미등록 silent**: SoundManager가 미등록 ID에 대해 UE_LOG + no-op하는 기존 동작을 그대로 활용한다. AnimInstance 측에서 사전 검증 안 함.
7. **반환값 무시**: `StartCameraShake` 의 반환값(`UCameraShakeModifier*`)을 보관하지 않는다. shake 강제 종료는 본 task 범위 외.

## Verify Result (설계 문서 Section 4 인용 — 본 PR 진입 전 재확인)

| 단계 | 사용할 API | 헤더 |
|---|---|---|
| 1 | `GetTypedOuter<USkeletalMeshComponent>()` | `Object/Object.h` (이미 의존) → 결과 캐스팅용 완전 타입 필요: `Component/SkeletalMeshComponent.h` |
| 2 | `Comp->GetOwner()` → `AActor*` | `Component/ActorComponent.h` (SkeletalMeshComponent의 base — transitive로 들어옴) |
| 3 | `Owner->GetWorld()` → `UWorld*` | `GameFramework/AActor.h` |
| 4 | `World->GetPlayerController(0)` → `APlayerController*` | `GameFramework/World.h` |
| 5 | `PC->GetCameraManagerPtr()` → `APlayerCameraManager*` (nullable) | `GameFramework/PlayerController.h` (이 헤더가 `Camera/PlayerCameraManager.h` 를 transitive로 들고 있음 — 설계 5.4 인용) |
| API | `APlayerCameraManager::StartCameraShake(const FCameraShakeParams&)` | `Camera/PlayerCameraManager.h` (PC 헤더에서 transitive) |
| Sound | `FSoundManager::Get().PlayEffect(const FSoundId&)` | `Sound/SoundManager.h` |

본 PR 시작 시 위 시그니처와 헤더 위치를 view로 한 번 더 검증. 차이가 있으면 STOP 보고.

## 작업 항목

### Step 0 — Verify Read

다음 view:

- `Engine/Object/Object.h` 의 `GetTypedOuter<T>` template 시그니처 — `const` 사용 가능 여부 확인
- `Engine/Component/SkeletalMeshComponent.h` — 클래스 계층 (UActorComponent 후손인지)
- `Engine/Component/ActorComponent.h:53` — `GetOwner()` 정확한 시그니처 (const?, return type)
- `Engine/GameFramework/AActor.h` — `GetWorld()` 시그니처
- `Engine/GameFramework/World.h:111-112` — `GetPlayerController(int32)` / `GetPlayerControllers()`
- `Engine/GameFramework/PlayerController.h:61-76` — `GetCameraManager` / `GetCameraManagerPtr` / `EnsureCameraManager` 시그니처 비교
- `Engine/Camera/PlayerCameraManager.h:73` — `StartCameraShake` 정확한 시그니처와 반환형
- `Engine/Sound/SoundManager.h` — `FSoundManager::Get()` 와 `PlayEffect` 시그니처

설계 인용과 차이가 있으면 STOP 보고.

### Step 1 — Header에 forward declaration 추가 (R4)

`AnimInstance.h` 의 적절한 위치 (다른 forward decl 근처) 에:

```cpp
class APlayerCameraManager;
```

`ResolveCameraManager` private helper 선언 추가:

```cpp
private:
    APlayerCameraManager* ResolveCameraManager() const;
```

- `const` — 함수가 멤버 상태를 바꾸지 않으므로. (단 `GetTypedOuter` 가 const 컨텍스트에서 호출 가능해야 함. Step 0에서 확인.)
- return은 raw pointer. ownership 없음.
- `Sound/SoundManager.h` 와 `Component/SkeletalMeshComponent.h` 등은 헤더에 include하지 않는다 (R4).

### Step 2 — `AnimInstance.cpp` 에 include 추가

다음 include 묶음을 `AnimInstance.cpp` 의 기존 include 영역 끝에 추가:

```cpp
#include "Sound/SoundManager.h"
#include "Component/SkeletalMeshComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "GameFramework/PlayerController.h"
// Camera/PlayerCameraManager.h 는 PlayerController.h 의 transitive include 로 들어옴 (설계 5.4 인용).
// 명시적으로 추가하고 싶다면 추가해도 무방.
```

- 정확한 헤더 경로는 본 프로젝트의 include 컨벤션에 맞춘다 (`Engine/...` 접두어, 또는 모듈-relative 등).

### Step 3 — `ResolveCameraManager` 정의 (R2 — nullptr 가드)

`AnimInstance.cpp` 에 helper 정의 추가:

```cpp
APlayerCameraManager* UAnimInstance::ResolveCameraManager() const
{
    // 단계 1: AnimInstance → SkeletalMeshComponent (Outer 체인)
    USkeletalMeshComponent* Comp = GetTypedOuter<USkeletalMeshComponent>();
    if (!Comp) return nullptr;

    // 단계 2: Component → Actor
    AActor* Owner = Comp->GetOwner();
    if (!Owner) return nullptr;

    // 단계 3: Actor → World
    UWorld* World = Owner->GetWorld();
    if (!World) return nullptr;

    // 단계 4: World → PlayerController (index 0)
    APlayerController* PC = World->GetPlayerController(0);
    if (!PC) return nullptr;

    // 단계 5: PC → CameraManager (nullable)
    return PC->GetCameraManagerPtr();
}
```

- 정확한 메서드 이름은 Step 0의 view 결과로 확정.
- 5단계 모두 명시적으로 분기하고, 각 분기 결과를 디버깅 가능한 형태로 둔다 (한 줄짜리 ternary로 줄이지 말 것 — debugger 진입 가능성 보존).
- 에러 로그는 추가하지 않는다 (R5 / D6 톤). 정상 흐름에서 nullptr이 자주 나올 수 있는 경로(에디터 preview 등)이기 때문에 로그가 노이즈가 됨.

### Step 4 — `DispatchTriggeredNotifies` 의 두 분기 채우기 (T7 + T8)

P4에서 stub log로 두었던 두 분기를 실제 호출로 교체:

```cpp
case EAnimNotifyType::Sound:
    FSoundManager::Get().PlayEffect(Notify.SoundId);
    break;

case EAnimNotifyType::CameraShake:
{
    APlayerCameraManager* CamMgr = ResolveCameraManager();
    if (CamMgr)
    {
        CamMgr->StartCameraShake(Notify.ShakeParams);
    }
    // CamMgr == nullptr 이면 silent no-op (R5, R2)
    break;
}
```

- 기존 stub log 두 줄(`[AnimNotify stub] ...`)은 제거.
- `default:` 분기와 `case None:` 분기는 P4 그대로 유지.

### Step 5 — Sound: 빈 SoundId 처리 정책

`Notify.SoundId` 가 empty string인 경우 — 새 Sound notify를 추가했지만 사용자가 SoundId를 입력하지 않은 상태:

- 결정: **그대로 PlayEffect 호출**. SoundManager 측에서 미등록 ID(빈 문자열 포함)는 UE_LOG + no-op 처리한다(D6).
- AnimInstance 측에서 `if (!Notify.SoundId.IsEmpty())` 같은 사전 분기를 추가하지 않는다 — 정책 단일화 원칙.

### Step 6 — 빌드 + 수동 확인 체크리스트 (실효 검증)

P4 단계에서 stub log로 확인했던 흐름이 실제 효과로 이어지는지 검증:

- [ ] 빌드 통과
- [ ] **Sound notify**: SoundManager에 미리 LoadEffect로 등록한 ID를 가진 notify가 게임 모드에서 재생 시 **실제 소리가 들린다**
- [ ] **Sound notify (빈 ID)**: SoundId가 빈 상태로 둔 notify는 재생 시 crash 없이 silent (SoundManager의 UE_LOG만 남음, D6 확인)
- [ ] **Sound notify (미등록 ID)**: SoundManager에 등록되지 않은 임의 ID도 silent + UE_LOG (D6)
- [ ] **CameraShake notify (게임 모드, PlayerController 있음)**: 카메라가 실제로 흔들린다
- [ ] **CameraShake notify (에디터 preview 모드)**: outer chain의 어딘가가 nullptr이면 crash 없이 silent. log 노이즈도 없어야 함 (Step 3 결정 확인)
- [ ] **v3 백필 notify**: 여전히 skip 동작
- [ ] **Loop 재생**: Sound와 Shake 모두 한 사이클당 1번씩 발화
- [ ] **다중 notify 같은 frame**: 같은 프레임에 Sound 2개 또는 Sound 1 + Shake 1이 trigger되면 둘 다 발화
- [ ] **paused 상태**: 발화 없음

### Step 7 — 보고에 의존성 변화 명시

본 PR 머지로 인해 `AnimInstance.cpp` 가 새로 의존하게 되는 모듈:

- `Sound/SoundManager.h`
- `Component/SkeletalMeshComponent.h`
- `GameFramework/AActor.h`
- `GameFramework/World.h`
- `GameFramework/PlayerController.h`
- (transitive) `Camera/PlayerCameraManager.h`

이 변화가 본 프로젝트의 의존성 정책(만약 명시된 게 있다면)과 충돌하지 않는지 한 번 더 점검하고, 충돌 가능성이 있으면 보고에 명시.

## 변경 파일 목록 (예상)

| 파일 | 변경 |
|---|---|
| `Engine/Asset/Animation/Core/AnimInstance.h` | `APlayerCameraManager` forward decl + `ResolveCameraManager` 선언 |
| `Engine/Asset/Animation/Core/AnimInstance.cpp` | 5개 헤더 include 추가, `ResolveCameraManager` 정의, dispatch 분기 두 곳 실제 호출로 교체 |

## 절대 금지

- 콜백 주입 / dispatcher 인터페이스 / DI 패턴 도입 (D2 위반)
- `ResolveCameraManager` 의 nullptr 가드를 한 줄로 압축하거나 생략 (R2 위반)
- preview/game world 명시적 구분 가드 추가 (R5 위반)
- SoundId 사전 검증(empty / 등록 여부) 분기 추가 (D6, Step 5 정책 위반)
- `StartCameraShake` 반환값 보관 (범위 외)
- `AnimInstance.h` 에 외부 헤더 include 추가 (R4 위반 — forward decl만)
- 에러 로그를 ResolveCameraManager 5단계에 추가 (Step 3 톤)
- 새 enum 값 추가 또는 dispatch 분기에 default 동작 추가

## 보고 시 포함할 것

1. Step 0의 verify 결과 — 7개 API 시그니처가 설계 인용과 정확히 일치하는지
2. Step 1의 `const` 결정 (GetTypedOuter의 const 호환성 결과)
3. Step 2의 include 경로 컨벤션
4. Step 6의 수동 확인 체크리스트 결과 (각 항목 실제 결과 — 특히 preview 모드와 빈 SoundId 시나리오)
5. Step 7의 의존성 변화 점검 결과
6. **남은 작업(T9 — smoke test) 진행 시 필요한 사전 조건** (예: 등록된 SoundId 목록, 테스트용 .animseq fixture 위치 등)

(끝)
