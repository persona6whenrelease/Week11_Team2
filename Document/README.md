# AnimNotify Implementation Prompts — Index

`Document/animnotify_integration_design.md` 의 T1~T9 작업 분해를 의존성 기준으로 묶은 6개 prompt.

## 사용 순서

| Prompt | 포함 T | 핵심 산출 | 묶음 근거 |
|---|---|---|---|
| **P1** [P1_T1_T2_data_model_and_serialization.md](P1_T1_T2_data_model_and_serialization.md) | T1, T2 | `FAnimNotifyEvent` 6필드 / AssetVersion 4 / v3 백필 | 둘 다 `AnimNotify.h` 를 건드리고, T2 라운드트립 의미를 T1과 분리하면 빌드만 되고 의미 검증 안 된 중간 상태가 남음. R6는 옵션 A로 반영(IsValid 우회). |
| **P2** [P2_T3_datasource_adapter.md](P2_T3_datasource_adapter.md) | T3 | `FAnimNotifyEntry` 6필드 / adapter 양방향 변환 | UI(T4/T5)와는 독립적으로 빌드 검증 가능. 어댑터가 먼저 확정돼야 UI 변경의 의미가 명확. |
| **P3** [P3_T4_T5_editor_ui.md](P3_T4_T5_editor_ui.md) | T4, T5 | Add 메뉴 분기 / inline payload 위젯 | 둘 다 `AnimSequenceEditorTab.cpp` 단일 파일. 메뉴와 inline editor는 한 PR에서 UX 일관성 검토. |
| **P4** [P4_T6_dispatch_skeleton.md](P4_T6_dispatch_skeleton.md) | T6 | `DispatchTriggeredNotifies` 함수 골격 + stub log | 외부 시스템 호출 격리. T7/T8 직전 안전 베이스. stub log로 dispatch 흐름 자체를 먼저 검증. |
| **P5** [P5_T7_T8_real_dispatch.md](P5_T7_T8_real_dispatch.md) | T7, T8 | `FSoundManager::PlayEffect` 호출 + outer chain → `StartCameraShake` | dispatch switch 분기 채우기. nullptr 가드(R2)와 헤더 격리(R4)가 함께 검토되어야 하므로 묶음. R5는 가드 없이 둘 다 dispatch (사용자 결정). |
| **P6 (선택)** [P6_T9_smoke_test_checklist.md](P6_T9_smoke_test_checklist.md) | T9 | 수동 smoke test checklist 문서 + (선택) fixture | source code 변경 없음. end-to-end 시나리오 1회 수동 검증. |

## 진행 시 주의

### 1. 순서 엄수
- 각 P는 직전 P의 머지를 전제로 한다. 병렬 진행 금지.
- 특히 P4 → P5는 stub→실제 호출 교체이므로 P4 단독으로 stable한 stable point가 한 번 찍히는 것을 권장.

### 2. STOP 보고 신호
각 prompt에 명시된 다음 조건이 발생하면 즉시 작업을 멈추고 보고:
- Verify Read에서 설계 문서 인용과 실제 code가 다를 때
- 의존성 순환이 발생하는 헤더 include 상황 (P1 Step 1.3, P5 Step 1)
- 외부 API 시그니처가 설계 문서의 Section 4 Verify Result와 다를 때 (P5 Step 0)

### 3. 결정사항 재논의 금지
모든 prompt 헤더에 9개 결정사항(D1~D9)과 관련 risk 결정(R5, R6)이 박혀 있다. Claude Code가 "더 나은 대안"이라며 다른 길을 제시하면 거부하고 현재 결정을 유지한다.

### 4. Scope creep 차단
각 prompt의 "절대 금지" 섹션에 명시된 항목은 그 PR에 포함되면 안 된다. 다른 PR의 범위로 분리.

### 5. 사용자 결정 반영
- **R5 (preview/game 가드 없음)** → P4 Step 5, P5 Step 6에 명시
- **R6 (IsValid 우회 — 옵션 A)** → P1 Step 4에 명시

## 변경 파일 매트릭스

| 파일 | P1 | P2 | P3 | P4 | P5 |
|---|---|---|---|---|---|
| `Engine/Asset/Animation/Notify/AnimNotify.h` | ✏️ | | | | |
| `Engine/Camera/CameraShakeModifier.h` *(or inline in AnimNotify.h)* | ✏️ | | | | |
| `Engine/Asset/Animation/Core/AnimSequence.h` | ✏️ | | | | |
| `Engine/Asset/Animation/Core/AnimSequence.cpp` | ✏️ | | | | |
| `Editor/UI/SkeletalEditor/AnimSequenceDataSource.h` | | ✏️ | | | |
| `Editor/UI/SkeletalEditor/AnimSequenceDataSource.cpp` | | ✏️ | | | |
| `Editor/UI/SkeletalEditor/AnimSequenceEditorTab.h` | | | (?) | | |
| `Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp` | | | ✏️ | | |
| `Engine/Asset/Animation/Core/AnimInstance.h` | | | | ✏️ | ✏️ |
| `Engine/Asset/Animation/Core/AnimInstance.cpp` | | | | ✏️ | ✏️ |

(`(?)` = 필요 시 char buffer 멤버 추가, P3에서 결정)

## 후속 작업 (본 묶음 종료 후 별도 검토)

설계 문서 Section 9 Out of Scope에 명시된 항목들. 우선순위 순:

1. Reverse playback / Seek 정책 정리 (D7 보류분)
2. Sound asset picker UI (D5에서 텍스트 입력 1차 결정)
3. Type=None notify의 "Convert to..." UI (R7 보류분)
4. Particle/Hit/Event/Sync notify 타입 확장
5. UAnimNotifyState (begin/tick/end 콜백 모델)
6. Undo/Redo, Network replication

각 항목은 본 묶음 머지 후 별도 사이클(scan → integration design → 구현)로 진행한다.
