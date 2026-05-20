# Implementation Prompt P2 — T3: DataSource Adapter 양방향 확장

## 목적

P1에서 확장된 `FAnimNotifyEvent`(asset model)와 에디터용 `FAnimNotifyEntry`(editor model) 사이의 양방향 변환에 새 3개 필드(`Type`, `SoundId`, `ShakeParams`)를 추가한다. D9 결정에 따라 2-layer 어댑터 구조는 유지한다.

## 전제

- **P1(T1+T2) 머지 완료 상태**. 즉 `FAnimNotifyEvent`는 이미 6필드를 가지고 있고, asset 직렬화는 v4 + v3 백필이 동작한다.
- 본 prompt는 그 위에서 어댑터만 확장한다.

## 참조 문서

- 통합 설계: `Document/animnotify_integration_design.md`
  - 인용 섹션: **5.3 (c) — FAnimNotifyEntry/DataSource 확장**, **T3 (Section 7)**, **D9**

## 엄격한 제약

1. **본 PR의 범위는 T3뿐**. 에디터 UI 자체(메뉴, inline editor)는 다음 PR(P3)에서 다룬다. **UI 코드를 수정하지 않는다.**
2. `FAnimNotifyEntry` 의 기존 4필드(Name/TriggerTime/Duration/ColorPacked)는 시그니처와 의미를 유지한다.
3. `IAnimSequenceDataSource` 의 `AddNotify` / `UpdateNotify` / `GetNotifies` / `RemoveNotify` API 시그니처는 변경하지 않는다(가능한 한). 만약 시그니처가 `FAnimNotifyEntry` 를 받는 형태라면, struct 확장만으로 자동으로 전파되므로 시그니처 자체는 동일.
4. 본 PR은 빌드만 통과하면 UI 차원에서는 아직 의미 있는 동작을 하지 못한다(UI가 새 필드를 채우지 않으므로 항상 기본값으로 들어감). 그것이 정상이다.

## 작업 항목

### Step 0 — Verify Read

다음을 view하여 현재 어댑터 구조 재확인. 설계 문서와 실제 code가 다르면 STOP하고 보고.

- `Editor/UI/SkeletalEditor/AnimSequenceDataSource.h` 의 `FAnimNotifyEntry` 정의
- `Editor/UI/SkeletalEditor/AnimSequenceDataSource.cpp` 의 다음 함수들:
  - `AddNotify`
  - `UpdateNotify`
  - `RemoveNotify`
  - `RebuildNotifyCache` (또는 동등 — entry 캐시를 만드는 함수)
  - `WriteNotifyToAsset` (또는 동등 — entry → event 역방향 복사 함수)
- `IAnimSequenceDataSource` 인터페이스가 있다면 그 정의도 view.

### Step 1 — `FAnimNotifyEntry` 필드 확장

`AnimSequenceDataSource.h` 의 `FAnimNotifyEntry` 에 3개 필드 추가:

```cpp
EAnimNotifyType Type = EAnimNotifyType::None;
FString SoundId;
FCameraShakeParams ShakeParams;
```

- `AnimNotify.h` 와 `CameraShakeModifier.h` include 필요. include 순서/순환은 P1에서 이미 해결되었으므로 그대로 따라간다.

### Step 2 — Entry → Event (write to asset) 갱신

asset에 entry를 다시 쓰는 경로 (`AddNotify`, `UpdateNotify`, `WriteNotifyToAsset` 등):

- 기존 4필드 복사에 더해 새 3필드도 복사한다.
- 복사 방향: `Entry.Type → Event.Type`, `Entry.SoundId → Event.SoundId`, `Entry.ShakeParams → Event.ShakeParams`
- `FString` 과 `FCameraShakeParams` 는 모두 복사 가능한 값 타입이므로 단순 대입.

### Step 3 — Event → Entry (cache rebuild) 갱신

asset의 notify 배열을 읽어 entry 캐시를 만드는 경로 (`RebuildNotifyCache` 또는 동등):

- 기존 4필드 복사에 더해 새 3필드도 entry로 복사한다.
- v3 백필로 들어온 notify는 `Type=None`이 그대로 entry에도 들어온다 — UI는 다음 PR에서 이를 read-only로 표시할 것이므로 본 PR은 그저 값만 보존하면 됨.

### Step 4 — UpdateNotify의 부분 갱신 정책 확인

`UpdateNotify` 가 entry 전체를 받는지, 일부 필드만 갱신하는 API인지 확인:

- entry 전체를 받는 경우 (전체 덮어쓰기): 자동으로 새 필드도 갱신됨. 추가 작업 없음.
- 일부 필드만 갱신하는 oneOf-style API인 경우: 새 필드도 갱신할 수 있는 분기를 동일 패턴으로 추가.

본 prompt 작성 시점에 어떤 형태인지 확정되지 않았으므로 view 결과에 따라 결정. 다만 **API 시그니처 자체를 바꾸지 않는다** — 만약 부분 갱신 API라면, 새 필드를 갱신하는 oneOf 케이스를 같은 패턴으로 추가하는 정도까지만 허용.

### Step 5 — 기본값 (default) 정책

새 notify가 추가될 때(엔트리 생성 시점)의 기본값은 다음과 같이 둔다. **본 PR에서는 단지 default가 None/empty/default-construct로 들어오는 것까지만 보장**. UI에서 "Add Sound Notify" / "Add Camera Shake Notify" 메뉴가 `Type` 을 어떻게 채울지는 다음 PR(P3)의 책임.

| 필드 | 기본값 |
|---|---|
| `Type` | `EAnimNotifyType::None` |
| `SoundId` | empty string |
| `ShakeParams` | `FCameraShakeParams{}` (struct default constructor) |

### Step 6 — 빌드 확인

- 위 확장 후 빌드만 통과하면 본 PR은 완료.
- 에디터에서 기존 "Add Notify" 메뉴를 클릭했을 때 type=None인 entry가 추가되고, 직렬화/로드를 거쳐도 type=None이 유지되는지 mental trace로 확인.

## 변경 파일 목록 (예상)

| 파일 | 변경 |
|---|---|
| `Editor/UI/SkeletalEditor/AnimSequenceDataSource.h` | `FAnimNotifyEntry` 3필드 추가 + include |
| `Editor/UI/SkeletalEditor/AnimSequenceDataSource.cpp` | 양방향 복사 갱신 |

위 외의 파일은 건드리지 않는다.

## 절대 금지

- 에디터 UI(`AnimSequenceEditorTab.cpp`) 변경 — 다음 PR(P3)의 범위
- AnimInstance/SoundManager/CameraManager 관련 코드 변경
- `IAnimSequenceDataSource` 의 메서드 시그니처 변경
- `FAnimNotifyEntry` 의 기존 4필드 이름·타입·의미 변경
- 새 메서드를 인터페이스에 추가 (필요 시 다음 PR에서 결정)

## 보고 시 포함할 것

1. Step 0의 verify 결과 — 함수명/시그니처가 설계 문서 인용과 실제 일치하는지
2. Step 4의 `UpdateNotify` 형태(전체 덮어쓰기 / 부분 갱신) 결정과 이유
3. 새 필드 default 값 적용 확인
4. 빌드 결과
5. **다음 PR(P3 — Editor UI)에 영향을 줄 만한 발견사항**

(끝)
