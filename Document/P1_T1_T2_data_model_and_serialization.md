# Implementation Prompt P1 — T1 + T2: Data Model 확장 + 직렬화 (AssetVersion 4)

## 목적

`FAnimNotifyEvent`에 effect type과 payload를 추가하고, `UAnimSequence`의 직렬화 포맷을 v4로 올린다.
v3로 저장된 기존 .animseq 파일은 `Type=None`으로 백필하여 로드한다.

## 참조 문서

- 통합 설계: `Document/animnotify_integration_design.md`
  - 인용 섹션: **3.1, 3.2, 3.3, 5.1, 5.2** + **T1, T2 (Section 7)** + **R1, R6 (Section 8)**
- 본 prompt에 명시되지 않은 결정사항은 위 문서의 D1~D9를 따른다.

## 엄격한 제약

1. **본 PR의 범위는 T1 + T2뿐**. 에디터 UI, DataSource, runtime dispatch는 손대지 않는다. 빌드만 깨지지 않으면 그 외 변경 금지.
2. **9개 결정사항(D1~D9)을 재논의하지 않는다**. 특히 D1(flat struct), D3(v3 백필)은 본 작업의 핵심이며 다른 모델로 대체 제안 금지.
3. **R6 결정**: `FAssetFileHeader::IsValid` 시그니처는 **변경하지 않는다**. `AnimSequence::Serialize` 안에서 직접 `Header.Type == EAssetType::AnimSequence` 와 `Header.Version <= AssetVersion` 두 조건만 검사하는 형태로 우회한다. 다른 asset class에 영향이 가지 않도록 변경을 `AnimSequence.cpp` 안에 격리.
4. **실제 code를 직접 확인할 것**. 설계 문서의 코드 인용은 reference이지만, 구현 시점에는 다음 파일을 반드시 직접 view한 뒤 작업한다:
   - `KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h`
   - `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h`
   - `KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp`
   - `KraftonEngine/Source/Engine/Camera/CameraShakeModifier.h` (`FCameraShakeParams` 구조 재확인)
   - asset header struct가 정의된 파일 (`AssetFileHeader` 또는 동등 — 본 작업 시작 전 grep으로 위치 확인)
5. **시그니처 변경 금지** (R6): `FAssetFileHeader` 자체와 `IsValid` 멤버 함수에는 절대 손대지 않는다.

## 작업 항목

### Step 0 — Verify Read

다음을 read하여 현재 상태를 재확인한다. 설계 문서와 실제 code가 다르면 **STOP하고 보고**.

- `FAnimNotifyEvent` 3개 필드 (TriggerTime/Duration/NotifyName) 그대로인지
- `FAnimNotifyEvent::operator<<` 가 3필드만 직렬화하는지
- `UAnimSequence::AssetVersion` 이 `3u` 인지
- `UAnimSequence::Serialize` 에서 `Header.IsValid(...)` 호출 방식
- `FCameraShakeParams` 의 필드 13개 순서와 타입 — 직렬화 helper 작성에 그대로 사용

### Step 1 — T1: Data Model 확장

`Engine/Asset/Animation/Notify/AnimNotify.h`:

1. 새 enum 정의 추가:
   ```cpp
   enum class EAnimNotifyType : uint8
   {
       None = 0,
       Sound = 1,
       CameraShake = 2,
   };
   ```
   - 값은 명시적으로 지정한다(직렬화 안정성).
   - 새 타입은 enum 끝에 append만 허용한다는 코멘트를 추가한다.

2. `FAnimNotifyEvent` 에 3개 필드 추가 (기존 3개 뒤에):
   - `EAnimNotifyType Type = EAnimNotifyType::None;`
   - `FString SoundId;` (default empty)
   - `FCameraShakeParams ShakeParams;` (default-constructed)

3. `FCameraShakeParams` 사용을 위해 `#include "Camera/CameraShakeModifier.h"` 를 헤더에 추가한다.
   - 이 헤더 include가 순환 의존을 만들지 않는지 빌드로 확인.
   - 만약 순환이 발생하면 forward declaration + cpp include로 대체하되, struct를 멤버로 직접 보유하기 때문에 완전 타입이 필요하므로 forward로는 해결되지 않는다. 그 경우 `Camera/CameraShakeModifier.h` 자체의 의존성을 점검하여 헤더 분리(예: `FCameraShakeParams` 만 떼어낸 별도 헤더)가 필요한지 보고하고 멈춘다.

### Step 2 — T2a: `FAnimNotifyEvent::operator<<` 갱신

같은 파일 내 `operator<<` 를 **v4 포맷 전용**으로 확장한다.

순서 (반드시 이 순서):
```
TriggerTime → Duration → NotifyName → (uint8)Type → SoundId → ShakeParams
```

- `Type`은 `uint8` 로 cast하여 직렬화한다. 로드 시에도 `uint8` 로 읽어서 enum으로 reinterpret.
- `ShakeParams` 직렬화를 위한 `operator<<(FArchive&, FCameraShakeParams&)` 가 없으면 같은 헤더(또는 별도 helper)에 신설한다. 필드 순서는 `CameraShakeModifier.h`의 선언 순서를 그대로 따른다. 직렬화 helper의 위치는:
   - 1순위: `Camera/CameraShakeModifier.h` 자체에 추가 (해당 struct의 자연스러운 위치).
   - 2순위: `Camera/CameraShakeModifier.h` 변경이 부담스러우면 `AnimNotify.h` 안에 inline free function으로 정의.
   - 위 둘 중 어느 쪽이 본 프로젝트의 기존 패턴(다른 struct들의 `operator<<` 가 어디에 있는지)에 맞는지 1분 정도 grep한 뒤 결정한다.

### Step 3 — T2b: `AssetVersion` bump

`Engine/Asset/Animation/Core/AnimSequence.h`:

- `static constexpr uint32 AssetVersion = 3u;` → `4u` 로 변경.
- 기존 v3 호환을 의도한 주석 한 줄 추가 (예: `// v3 files load via backfill (Type=None). See AnimSequence::Serialize.`)

### Step 4 — T2c: `Serialize` 함수에 v3/v4 분기 + IsValid 우회 (R6)

`Engine/Asset/Animation/Core/AnimSequence.cpp` 의 `Serialize`:

1. **IsValid 우회** (R6 옵션 A):
   - 기존: `if (!Header.IsValid(EAssetType::AnimSequence, AssetVersion)) { ... }`
   - 변경 후: `IsValid` 호출 대신 인라인으로 두 조건 검사:
     ```cpp
     const bool bTypeOk = (Header.Type == EAssetType::AnimSequence);
     const bool bVersionOk = (Header.Version <= AssetVersion);  // <= 로 backward-compat 허용
     if (!bTypeOk || !bVersionOk) { /* 기존 에러 처리 그대로 */ }
     ```
   - 정확한 필드 이름(Header.Type / Header.Version)은 헤더 struct를 보고 그대로 사용.
   - 변경은 이 함수 안에만 한정. `FAssetFileHeader` 자체에는 손대지 않는다.

2. **Notify 직렬화 분기**:
   - 저장 시 (saving): 항상 v4 포맷으로 저장. 즉 `Ar << Notifies;` 를 그대로 두면 갱신된 `operator<<` 가 v4로 직렬화한다.
   - 로드 시 (loading):
     - `Header.Version == 4`: `Ar << Notifies;` (v4 operator<<)
     - `Header.Version == 3`: 백필 로드:
       ```
       uint32 Count;
       Ar << Count;
       Notifies.SetNum(Count);  // 컨테이너의 정확한 메서드는 본 프로젝트 컨벤션 따라 (resize/SetNum)
       for (uint32 i = 0; i < Count; ++i) {
           Ar << Notifies[i].TriggerTime;
           Ar << Notifies[i].Duration;
           Ar << Notifies[i].NotifyName;
           Notifies[i].Type = EAnimNotifyType::None;
           // SoundId, ShakeParams 는 default-construct 그대로
       }
       ```
     - 그 외 버전은 기존 에러 처리.
   - 저장/로드 모드 분기는 `Ar.IsLoading()` / `Ar.IsSaving()` 등 본 프로젝트의 archive API를 사용. 정확한 API명은 archive 헤더를 grep으로 확인.

3. 저장 시 `Header.Version = AssetVersion;` 라인이 이미 있는지 확인하고, 없으면 추가한다.

### Step 5 — 라운드트립 단위 점검 (R1)

자동화 테스트 인프라가 없으면 수동 절차를 코멘트나 별도 문서로 남기되, **본 PR에서는 빌드만 확인하고 실제 라운드트립 검증은 다음 PR(또는 T9)에서 다룬다**. 단, 다음 사항만 코드 리뷰 단계에서 자기 검토:

- v3 fixture(아무 .animseq 파일)를 v4 코드로 열었을 때, `Notifies.Num()` 이 보존되는가 (mental trace)
- 저장 후 같은 코드로 다시 열었을 때 모든 필드가 보존되는가
- v3 에는 없는 v4 필드(`Type/SoundId/ShakeParams`)가 default 값으로 채워져 들어오는가

이 mental trace 결과를 PR description 또는 보고에 1단락으로 작성.

## 변경 파일 목록 (예상)

| 파일 | 변경 |
|---|---|
| `Engine/Asset/Animation/Notify/AnimNotify.h` | enum 신설, struct 3필드 추가, operator<< v4 확장 |
| `Engine/Camera/CameraShakeModifier.h` *(또는 AnimNotify.h)* | `FCameraShakeParams` 직렬화 helper 추가 (위치는 Step 2 결정) |
| `Engine/Asset/Animation/Core/AnimSequence.h` | `AssetVersion = 4u` |
| `Engine/Asset/Animation/Core/AnimSequence.cpp` | `IsValid` 우회 + v3/v4 분기 |

위 외의 파일은 건드리지 않는다.

## 절대 금지

- `FAssetFileHeader` 자체나 `IsValid` 멤버 함수 변경 (R6)
- 에디터 UI / DataSource / AnimInstance / SoundManager / CameraManager 관련 코드 변경 (다음 PR들의 범위)
- polymorphic `UAnimNotify` 베이스 클래스 도입 (D1과 충돌)
- 새 enum 값 추가 (Sound, CameraShake 외)
- `TriggeredNotifiesThisFrame` 의 타입 변경 (Lua 호환)
- T2b의 AssetVersion을 4 외의 값으로 설정

## 보고 시 포함할 것

1. Step 0의 verify 결과 — 설계 문서와 실제 code 차이 (없으면 "일치")
2. `FCameraShakeParams::operator<<` 를 어디에 두었는지와 이유
3. `Header.Type` / `Header.Version` 의 실제 필드 이름과 자료형
4. `Ar.IsLoading()` / `IsSaving()` 에 해당하는 본 프로젝트의 정확한 API 이름
5. Step 5의 mental trace 결과
6. 빌드 결과 (성공/실패)
7. **다음 PR(P2 — DataSource adapter)에 영향을 줄 만한 발견사항**이 있으면 별도 문단으로 기록

(끝)
