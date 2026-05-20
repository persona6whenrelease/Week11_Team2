# Implementation Prompt P3 — T4 + T5: 에디터 UI (Add 메뉴 분기 + Inline Payload 위젯)

## 목적

`AnimSequenceEditorTab` 의 timeline UI에서:
1. notify를 "Sound" 또는 "CameraShake" 타입으로 추가하는 메뉴 분기를 만든다.
2. 인라인 property editor에 type별 payload 편집 위젯을 추가한다.

## 전제

- **P1(T1+T2) + P2(T3) 머지 완료**. `FAnimNotifyEvent`, `FAnimNotifyEntry` 모두 6필드를 보유하고, asset 직렬화도 v4 동작.
- DataSource adapter는 이미 양방향으로 새 필드를 복사한다.

## 참조 문서

- 통합 설계: `Document/animnotify_integration_design.md`
  - 인용 섹션: **5.3 (a), (b)**, **T4, T5 (Section 7)**, **D5, D6, D8**, **R3, R7**

## 엄격한 제약

1. **본 PR 범위는 T4 + T5뿐**. runtime dispatch 코드는 건드리지 않는다.
2. **R3 — 매 프레임 string 복사 정책**: `SoundId` InputText는 별도 `char[]` 버퍼를 두고(`NameBuf` 와 같은 기존 패턴 그대로), 매 프레임 `Edited.SoundId.c_str()` 를 InputText에 직접 넘기지 않는다.
3. **R7 — Type=None 처리**: v3 백필로 들어온 `Type==None` 인 notify는 inline editor에서 **read-only 안내만** 표시 (예: `"(no type — legacy)"`). "Convert to..." UI는 본 PR에 추가하지 않는다.
4. **D8 — NotifyName**: Add Sound/Add Camera Shake 메뉴가 entry를 생성할 때 `NotifyName` 의 기본값은 각각 `"SoundNotify"`, `"CameraShakeNotify"` 로 둔다. 사용자가 변경 가능.
5. **D5 — SoundId 입력**: 단순 텍스트 입력. asset picker / 등록된 ID 드롭다운 / 자동완성 등은 추가하지 않는다.
6. **D6 — 미등록 SoundId**: 입력 단계에서 등록 여부를 검사하지 않는다. 경고 아이콘 같은 UI 표시도 본 PR 범위 외.
7. 기존 UI(Name / Trigger slider / Duration slider / Color / Go / Delete) 동작은 그대로 보존한다.

## 작업 항목

### Step 0 — Verify Read

다음 view 후 작업 시작. 설계 인용과 실제 code가 다르면 STOP하고 보고.

- `Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp` 중:
  - 컨텍스트 메뉴 부근 (설계 문서 인용: `.cpp:635-643`) — 현재 "Add Notify at current frame" 메뉴 형태
  - `RenderNotifyPropertyInline` (설계 문서 인용: `.cpp:768-839`) — 기존 위젯 layout
  - `HitTestNotify` / `DraggingNotifyIndex` 등 hit-test 흐름 (변경하지 않지만 layout 영향 확인용)
- `Editor/UI/SkeletalEditor/AnimSequenceEditorTab.h` — `RenderNotifyPropertyInline` 시그니처
- `Editor/UI/SkeletalEditor/AnimSequenceDataSource.h` — P2에서 확장된 `FAnimNotifyEntry`
- `Engine/Camera/CameraShakeModifier.h` — `FCameraShakeParams` 필드 13개 (정확한 타입/이름 확인. ImGui 위젯 매핑에 사용)

### Step 1 — T4: Add 메뉴 분기

현재 "Add Notify at current frame" 단일 항목을 두 항목으로 분리:

| 메뉴 | 동작 |
|---|---|
| `"Add Sound Notify at current frame"` | `FAnimNotifyEntry` 생성: `NotifyName="SoundNotify"`, `Type=Sound`, `SoundId=""`, `ShakeParams={}` (default), `TriggerTime=<current>`, `Duration=0` |
| `"Add Camera Shake Notify at current frame"` | `FAnimNotifyEntry` 생성: `NotifyName="CameraShakeNotify"`, `Type=CameraShake`, `SoundId=""`, `ShakeParams={}` (default), `TriggerTime=<current>`, `Duration=0` |

- 생성 후 `DataSource->AddNotify(Entry)` 호출은 기존과 동일.
- 메뉴 구조는 본 프로젝트의 ImGui 컨벤션에 맞춘다 — 같은 레벨의 두 항목 / 또는 서브메뉴 ("Add Notify >") 중 어느 쪽이 자연스러운지 기존 컨텍스트 메뉴 패턴을 보고 결정. 본 prompt는 결과만 요구하고 형태는 위임.
- 기존 "Add Notify at current frame" 항목은 **제거**한다 (type 없는 notify는 v3 백필 외에는 더 이상 새로 생기지 않음. D8의 NotifyName label 정책상 type=None 항목을 새로 만들 의미가 없음).

### Step 2 — T5: `RenderNotifyPropertyInline` 확장

기존 위젯(Name/Trigger/Duration/Color/Go/Delete)을 보존한 상태에서, 그 아래에 `Edited.Type` 분기 섹션을 추가한다.

```
[기존 위젯들]
─────── (separator) ───────
[Type에 따른 payload 위젯 분기]
```

#### Step 2.1 — Type=None 분기

```cpp
ImGui::TextDisabled("(no type — legacy)");
// 편집 위젯 없음
```

읽기 전용 안내만 표시. 사용자가 type을 바꾸고 싶다면 삭제 후 재추가 (R7).

#### Step 2.2 — Type=Sound 분기 (R3)

```cpp
// 매 프레임 string 복사 회피: char buffer 보유
static char SoundIdBuf[128];  // 또는 멤버 변수 — 기존 NameBuf 패턴 따라
strncpy(SoundIdBuf, Edited.SoundId.c_str(), sizeof(SoundIdBuf) - 1);
SoundIdBuf[sizeof(SoundIdBuf) - 1] = '\0';

if (ImGui::InputText("SoundId", SoundIdBuf, sizeof(SoundIdBuf))) {
    Edited.SoundId = SoundIdBuf;
    // 변경 마킹 (기존 Name 편집과 동일 패턴)
}
```

- 정확한 버퍼 보유 방식과 변경 마킹 패턴은 기존 `NameBuf` / `bDirty` (또는 동등) 흐름을 그대로 따른다.
- 등록 여부 검사 없음 (D6).
- 버퍼 크기는 기존 NameBuf 와 동일 또는 그보다 큰 값(예: 128) 사용. SoundId 길이 제한은 SoundManager 측에 명시되어 있지 않으므로 임의 결정.

#### Step 2.3 — Type=CameraShake 분기

`FCameraShakeParams` 13개 필드를 다음 매핑으로 위젯화:

| 필드 | 타입 | 위젯 |
|---|---|---|
| `Pattern` | `ECameraShakePattern` (enum) | `Combo` — enum 값 목록 |
| `Duration` | `float` | `InputFloat` (또는 `DragFloat`, 양수만, min=0) |
| `BlendInTime` | `float` | `InputFloat` (min=0) |
| `BlendOutTime` | `float` | `InputFloat` (min=0) |
| `LocationAmplitude` | `FVector` | `InputFloat3` |
| `RotationAmplitude` | `FRotator` | `InputFloat3` (Pitch/Yaw/Roll 순서는 본 프로젝트의 FRotator 컨벤션 따라) |
| `FOVAmplitude` | `float` | `InputFloat` |
| `Frequency` | `float` | `InputFloat` (min=0) |
| `Roughness` | `float` | `InputFloat` (min=0) |
| `bApplyInCameraLocalSpace` | `bool` | `Checkbox` |
| `bSingleInstance` | `bool` | `Checkbox` |
| `Seed` | `uint32` | `InputScalar` (ImGuiDataType_U32) |

- 위젯 변경 시 기존 Name/Trigger 편집과 동일한 dirty/update 흐름을 따른다.
- 13개 위젯을 한 화면에 다 펼치면 길어지므로, ImGui `TreeNode("Shake Params")` 또는 `CollapsingHeader` 로 묶는 것이 자연스럽다. 형태는 기존 패턴 따라 결정 위임.
- `ECameraShakePattern` 의 enum 값은 `CameraShakeModifier.h` 에서 확인 후 그대로 listing. enum 값 이름과 표시 라벨이 다르면 둘 다 보여주는 식으로 (예: `"Sine"`, `"Perlin"`).

### Step 3 — 위젯 변경의 DataSource 반영

기존 Name/Trigger/Duration 편집이 어떤 경로로 `DataSource->UpdateNotify(...)` 를 호출하는지 확인하고, 새 payload 위젯들도 같은 경로를 사용한다.

- 매 프레임 무조건 update를 호출하는 패턴인지, 변경이 있을 때만 호출하는 패턴인지 확인 후 동일 패턴 적용.
- 본 PR에서 update 정책 자체를 바꾸지 않는다.

### Step 4 — 빌드 + 수동 확인 체크리스트

자동화 테스트가 없으므로, 본 PR 머지 전 수동으로 다음을 확인하고 보고:

- [ ] 빌드 통과
- [ ] timeline 우클릭 시 "Add Sound Notify" / "Add Camera Shake Notify" 두 항목이 보인다
- [ ] 각 항목 클릭 시 timeline에 해당 type의 notify가 추가된다 (시각적 마커 변화 — 색상이나 모양으로 type을 구분하지 않아도 됨, 본 PR 범위 외)
- [ ] notify를 선택하면 inline editor에 type별 payload 위젯이 보인다
- [ ] Sound notify의 SoundId 입력이 동작한다 (저장 후 다시 열면 입력값 유지)
- [ ] CameraShake notify의 13개 필드가 모두 편집 가능하고, 저장 후 다시 열면 모두 유지
- [ ] v3 백필된 notify(있다면)는 inline editor에서 "(no type — legacy)" 만 보이고 편집 위젯이 없다
- [ ] 기존 Name/Trigger/Duration/Color 편집은 그대로 동작 (regression 없음)

## 변경 파일 목록 (예상)

| 파일 | 변경 |
|---|---|
| `Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp` | Add 메뉴 분기, RenderNotifyPropertyInline 확장 |
| `Editor/UI/SkeletalEditor/AnimSequenceEditorTab.h` | (필요 시) char buffer 멤버 추가 |

## 절대 금지

- runtime dispatch / AnimInstance / SoundManager / CameraManager 코드 변경 (다음 PR P4~P5의 범위)
- SoundId 입력에 asset picker / 자동완성 / 등록 검사 (D5, D6)
- Type=None 항목에 "Convert to..." UI 추가 (R7)
- 기존 "Add Notify at current frame" 단일 메뉴를 그대로 두는 것 (Step 1에서 제거 명시)
- type별로 timeline 마커 색상을 자동으로 다르게 하는 등 본 PR 범위 외 UX 변경
- 직렬화 포맷 변경 (P1 범위)

## 보고 시 포함할 것

1. Step 0의 verify 결과
2. Step 1의 메뉴 형태 (두 항목 / 서브메뉴) 결정과 이유
3. Step 2.3의 위젯 그룹화 방식 (TreeNode / CollapsingHeader / flat) 결정
4. Step 4의 수동 확인 체크리스트 결과
5. R3에 따른 char buffer 보유 위치 (static / 멤버 / 다른 패턴)
6. **다음 PR(P4 — Runtime Dispatch 골격)에 영향을 줄 만한 발견사항**

(끝)
