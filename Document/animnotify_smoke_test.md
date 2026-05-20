# AnimNotify Smoke Test — Sound + CameraShake

P1~P5 누적 결과의 end-to-end 수동 검증 체크리스트.
자동화 인프라가 없으므로 본 문서는 **수동 실행 가이드 + 결과 기록 템플릿**의 역할만 한다.

## 0. 사전 조건

- KraftonEngine 빌드 성공 (`KraftonEngine.exe` 산출)
- SoundManager에 효과음 ID가 LoadEffect로 등록되어 있음
  - 등록 위치: [KraftonEngine/Source/Games/Crossy/CrossyGameModule.cpp:113-118](../KraftonEngine/Source/Games/Crossy/CrossyGameModule.cpp)
  - ID 정의: [KraftonEngine/Source/Games/Crossy/Audio/CrossyAudioIds.h](../KraftonEngine/Source/Games/Crossy/Audio/CrossyAudioIds.h)
  - 등록된 ID 목록: `"Jump"`, `"Parry"`, `"Death"`, `"Dash"`, `"Crash"` (그 외에 BGM용 `"Crossy.BGM"`)
  - Smoke test 권장 ID: `"Jump"` (짧고 식별 용이)
- Game 모드 진입 시 PlayerController + PlayerCameraManager가 생성되어 있어야 CameraShake가 시각적으로 확인됨
- 테스트용 .animseq: 기존 캐릭터의 임의 sequence 사용 (또는 본 PR 산출물의 fixture 별도 디렉토리 — Step 2 참조)

## 1. 시나리오 A — Sound Only

1. KraftonEngine 실행 → AnimSequence 에디터 탭 열기
2. 임의의 .animseq 로드 (또는 신규 sequence를 baked clip에서 임시 생성)
3. timeline에서 우클릭 → **"Add Sound Notify at current frame"**
4. inline editor에서 SoundId 입력란에 `"Jump"` (등록된 ID) 입력
5. 저장
6. Game 모드 진입 → 같은 sequence 재생
7. **기대**: 해당 frame 도달 시 효과음 1회 재생
8. 동일 sequence를 loop 재생 → 매 사이클당 1회씩 재생되는지 확인

## 2. 시나리오 B — CameraShake Only

1. (시나리오 A의 1~2 반복)
2. timeline 우클릭 → **"Add Camera Shake Notify at current frame"**
3. inline editor → "Shake Params" CollapsingHeader 펼치기
4. 식별 가능한 값 입력 (예시):
   - `Duration = 1.0`
   - `LocationAmplitude = (5, 5, 5)`
   - `RotationAmplitude = (2, 2, 2)` (Pitch/Yaw/Roll)
   - `Frequency = 10`
   - 나머지는 기본값
5. 저장
6. Game 모드 진입 → 재생
7. **기대**: 해당 frame 도달 시 카메라가 1초간 흔들림

## 3. 시나리오 C — 둘 다

1. 같은 sequence에 Sound notify 1개 + CameraShake notify 1개를 **다른 frame**에 배치
2. Game 모드 재생
3. **기대**: 두 효과 모두 정확한 frame에 발화
4. 추가 검증 — 두 notify를 **같은 frame**에 배치하면 같은 프레임에 동시 발화

## 4. 시나리오 D — Preview vs Game (R5 검증)

1. 시나리오 A의 sequence를 **에디터 preview tab**에서 재생
2. **기대**:
   - Sound: 들림 (R5 정책상 의도된 동작 — preview/game 가드 없음)
   - CameraShake: outer chain의 어딘가(주로 PlayerController)에서 nullptr이면 silent. **crash 없음**
3. log에 에러/경고 노이즈가 쌓이지 않는지 확인 (`ResolveCameraManager`는 nullptr시 silent — 의도된 톤)

## 5. 시나리오 E — Legacy v3 호환 (D3 검증)

1. v3로 저장된 기존 .animseq가 있다면 그 파일을 로드
   - (없다면 본 시나리오 skip — 보고에 사유 명시)
2. 에디터에서 열어 notify가 보이는지 확인 (`Notifies.Num()` 보존)
3. inline editor에서 해당 notify가 **"(no type — legacy)"** 라고 표시되는지 확인
4. Game 모드 재생 시 dispatch에서 skip (효과 발화 없음)
5. 해당 sequence를 다시 저장 → 파일이 v4로 승격되었는지 mental trace
   - 저장 시 `Header.Version = AssetVersion(=4)` 라인이 실행되므로 v4로 승격됨
   - 재로드 시 `Header.Version == 4` 경로로 v4 operator<< 사용 → Type=None이 유지됨

## 6. 시나리오 F — 빈 / 미등록 SoundId (D6 검증)

1. Sound notify를 추가하고 SoundId를 **비워 둠**
2. Game 모드 재생 → **crash 없음**, SoundManager 측 UE_LOG만 남음
3. SoundId를 `"nonexistent_id_xxx"` 같은 **미등록 값**으로 입력
4. Game 모드 재생 → 동일하게 crash 없음, log만 남음

## 7. Regression 확인

- Lua `GetTriggeredNotifies()` polling이 그대로 동작하는지 (D8 — `TriggeredNotifiesThisFrame: TArray<FName>` 보존)
- 기존 .animseq의 다른 모든 데이터(BoneAnimationTracks, CurveData, FrameRate 등)가 손상 없이 로드되는지
- 에디터 timeline의 기존 동작 regression 없는지:
  - notify drag (DraggingNotifyIndex)
  - Name 편집 (NameBuf)
  - Trigger / Duration slider
  - Color 편집 (ColorEdit3)
  - "Go" / "Delete" 버튼
- AnimSequenceDataSource의 양방향 변환이 7필드 모두 보존 (P2 산출)

## 8. 결과 보고 템플릿

각 시나리오 실행 후 다음 표를 채워 보고:

| Scenario | Pass / Fail | 비고 |
|---|---|---|
| A — Sound Only | | |
| B — CameraShake Only | | |
| C — 둘 다 | | |
| D — Preview vs Game | | |
| E — Legacy v3 호환 | | (v3 fixture 부재 시 skip) |
| F — 빈/미등록 SoundId | | |
| Regression | | |

발견된 regression이나 R5/R6/D6/D3 정책 위반 사례가 있으면 별도 issue로 분리.

## 후속 작업 제안 (P5까지 보류된 항목)

- **자동화 테스트 인프라**: 본 프로젝트에 unit test runner가 없음. animation notify dispatch는 외부 시스템(Sound/Camera) 의존이 강해 mock 인프라가 우선 필요. 별도 RFC 권장.
- **Asset picker / 자동완성 UI** (D5 보류): SoundId 입력에 등록된 ID 드롭다운 추가. SoundManager에 ID enumeration API 추가가 선행.
- **미등록 SoundId 시각 경고** (D6 보류): inline editor에서 빨간 아이콘/외곽선으로 표시. SoundManager에 `IsEffectRegistered(ID)` 조회 API 추가 후 매 프레임 또는 dirty 시점에 검사.
- **Reverse / Seek 정책** (D7 보류): `IsTriggeredBetween`이 현재 forward 정상 재생만 정확함. 역방향 재생(PlaybackSpeed < 0) 또는 seek 시 notify 발화 정책 — 별도 RFC.
- **`Type=None` → Sound/CameraShake in-place 변환 UI** (R7 보류): 현재는 삭제 후 재추가 필요. 사용자 피드백 수집 후 결정.
- **Type별 timeline 마커 색상 구분**: 현재는 `ColorPacked` 수동 입력. Type에서 자동 derive하면 UX 향상.
- **`StartCameraShake` 반환 핸들 사용**: 현재 반환값 무시. shake 강제 종료/우선순위 같은 기능 필요 시 핸들 관리 추가.

## 변경 파일 (P1~P5 누적)

| Phase | 파일 |
|---|---|
| P1 | [AnimNotify.h](../KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h), [AnimSequence.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h), [AnimSequence.cpp](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp) |
| P2 | [AnimSequenceDataSource.h](../KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceDataSource.h), [AnimSequenceDataSource.cpp](../KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceDataSource.cpp) |
| P3 | [AnimSequenceEditorTab.cpp](../KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp) |
| P4 | [AnimInstance.h](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h), [AnimInstance.cpp](../KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.cpp) — stub log 골격 |
| P5 | 동일 파일 2개 — stub 제거, 실제 outer chain dispatch |
