# Codebase Exploration Prompt for Claude Code

## Context

너는 KraftonEngine이라는 자체제작 C++ / DX11 게임 엔진의 코드베이스를 탐색해야 한다. 이 엔진은 UObject 계열 객체 모델 + DX11 forward renderer 기반이며, FBX → SkeletalMesh + baked animation까지의 기본 파이프라인이 이미 작동한다. 이번 과제로 Animation 시스템을 Unreal Engine 스타일로 고도화하려 한다.

**알려진 사전 정보 (이미 검증된 내용 — 재확인 불필요):**

- FBX Importer (`Source/Engine/Mesh/FBX/`): Static/Skeletal/Animation 모두 구현. `FFbxAnimationParser`가 모든 `FbxAnimStack`을 순회해 본별 baked `LocalMatrix`로 저장 (T/R/S 분리 / curve / tangent **미보존**).
- Asset (`Source/Engine/Mesh/SkeletalMeshAsset.h`): `FSkeletalMesh`가 Vertices / Indices / Sections / **Bones (`FBoneInfo`)** / **AnimationClips (`FAnimationClip`)** 를 **모두 통합 보유**. Skeleton·Clip의 **독립 asset 분리는 없음**.
- Component 2계층 분리:
  - `USkinnedMeshComponent` — skinning + mesh buffer 관리 (`Source/Engine/Component/SkinnedMeshComponent.{h,cpp}`)
  - `USkeletalMeshComponent : USkinnedMeshComponent` — animation 재생 (clip index, time, speed, paused 4개 멤버) (`Source/Engine/Component/SkeletalMeshComponent.{h,cpp}`)
- Animation 재생: `ApplyBakedAnimation`에서 frame index 2개 사이 **element-wise matrix Lerp** (`SkeletalMeshComponent.cpp:43-91`). 보간 후 `RebuildMeshSpaceBoneMatrices` → `SkinVerticesToReferencePose`(virtual, 4-bone CPU skinning) → `UploadSkinnedVertices`.
- 렌더링: CPU skinning 결과를 `RuntimeMeshBuffer`(dynamic VB)로 만들어 일반 PNCTT 정점으로 흘려보내므로, Static/Skeletal이 SceneProxy 이후 통합 경로를 탄다. **GPU skinning 미구현**.
- Profiling: `Source/Engine/Profiling/Stats.h`의 `SCOPE_STAT` (CPU), `Source/Engine/Profiling/GPUProfiler.h`의 `GPU_SCOPE_STAT` (D3D11Query disjoint) 매크로 모두 구비.
- Editor: `Source/Editor/UI/EditorSkeletalMeshViewerWidget.{h,cpp}` — preview viewport + animation playback panel 존재.

**모르는 사항 (탐색해야 할 영역):**
- Reflection 시스템은 사소한 매크로만 존재한다고 알려짐 — **실제 매크로 정체와 사용처를 코드에서 확인**.
- Lua / 스크립트 시스템 존재 여부 — 알려지지 않음.
- 직렬화 (`FArchive`) 구조의 확장성 — `FAnimationClip` 분리 시 영향.
- Crash / MiniDump / Console command 인프라 존재 여부 — 알려지지 않음.
- Property / Details Panel UI 인프라 — 알려지지 않음.
- Event / Delegate 시스템 (Notify dispatch 후보) — 알려지지 않음.
- Editor 전반 UI 프레임워크 (ImGui로 추정되나 미확인).
- Asset import 시 매니저 / loader 아키텍처 (`FMeshManager` 외).

---

## Task

다음 8개 Animation 시스템 과제 항목 각각에 대해, **이미 구현되었거나 구현 연관성이 높은 기존 엔진 코드 위치**를 찾아 보고하라.

탐색 대상은 **직접 연관 + 간접 인프라**를 모두 포함한다. 예를 들면:
- "Anim Notify 데이터 저장"이라면 직접 연관(Animation 관련 직렬화 코드)뿐 아니라 간접 인프라(범용 `FArchive` 직렬화 시스템, Event / Delegate 시스템, Property metadata 매크로 등)까지 추적.
- "Timeline UI"라면 직접 연관(현재 playback panel)뿐 아니라 간접 인프라(Editor UI 프레임워크 진입점, ImGui 래퍼, Widget 등록 시스템 등)까지 추적.

### 과제 항목

1. **Animation Asset Pipeline**
   - Animation Data Model
   - FBX Animation Import
   - Animation Asset Save / Load
   - `UAnimSequence` (신규 도입 예정)
   - `UAnimDataModel` (신규 도입 예정)
   - Bone Animation Track
   - Skeleton reference (현재 `FSkeletalMesh` 내부 통합 → 분리 검토)
   - Anim Notify 데이터 저장

2. **Animation Runtime Core**
   - `UAnimInstance` (신규 도입 예정)
   - `UAnimSingleNodeInstance` (신규 도입 예정)
   - `PlayAnimation()`
   - Animation time update
   - Keyframe sampling
   - Local Pose 계산
   - Component Space Pose 계산
   - Skinning Matrix 생성
   - Reverse Play

3. **Animation Logic System**
   - Animation Blending
   - Animation State Machine
   - Lua Animation State Machine
   - Anim Notify Runtime
   - Transition 조건 처리
   - State별 animation 연결
   - Notify dispatch

4. **Animation Sequence Viewer**
   - Animation Sequence Viewer
   - Preview Viewport
   - Preview SkeletalMeshComponent
   - Animation asset 선택
   - 재생 결과 확인

5. **Timeline / Notify Editor UI**
   - Timeline UI
   - Play / Pause / Stop
   - Frame step
   - Scrubbing
   - Loop / Reverse toggle
   - Current time / frame 표시
   - Anim Notify Track UI
   - Notify marker / name 표시

6. **Skinning Rendering / Debug View**
   - GPU Skinning (현재 미구현)
   - CPU / GPU Skinning 전환
   - Bone Matrix Buffer
   - GPU Skinning Shader
   - Skinning Performance Stat
   - Bone Weight Heatmap

7. **Crash Debug System**
   - MiniDump
   - Crash Report
   - `CauseCrash` 콘솔 명령어
   - `.dmp` 저장
   - `.pdb` 기반 callstack 확인

8. **Reflection / Editor Property System**
   - Property Reflection System
   - Property metadata
   - Details Panel 노출
   - Animation asset 설정값 편집
   - State Machine / Notify 설정값 편집

---

## 탐색 방법 (이 순서대로)

1. **루트 디렉터리 구조 파악**: `Source/` 트리 전체를 한 번 훑어 어떤 시스템(Module)들이 존재하는지 확인. 예상되는 키워드: `Reflection`, `Property`, `Class`, `RTTI`, `Object`, `Serialization`, `Archive`, `Console`, `Cmd`, `Crash`, `Dump`, `Event`, `Delegate`, `Lua`, `Script`, `ImGui`, `Widget`, `Editor`, `Tick`, `Timer`.

2. **`Source/Engine/Profiling/Stats.h` 확인**: 알려진 매크로 패턴(`SCOPE_STAT`, `DECLARE_STAT_GROUP` 등)을 통해 이 엔진의 매크로 스타일을 익혀라. Reflection이 매크로 기반이라면 비슷한 스타일일 가능성이 높다.

3. **각 과제 항목에 대해 grep / 파일 탐색**: 
   - 키워드는 영문/한국어 모두 시도 (주석에 한국어가 섞여있을 수 있음).
   - **단정하지 말 것**: 못 찾았다면 "찾지 못함"이라고 명시. 비슷한 이름이지만 무관해 보이는 후보도 가능성으로 기록.

4. **확신 수준 표시**: 각 발견에 대해 "**직접 사용 가능 / 확장 필요 / 단서만 발견 / 미발견**" 4단계로 분류.

---

## Output 형식

각 과제 파트(1~8)별로 아래 마크다운 표 형식으로 작성. 표 외의 산문은 표 위에 한 문단(2~5줄) 정도의 요약만 허용.

```
## 파트 N — <파트 이름>

<2~5줄 요약: 이 파트에서 가장 중요한 발견 / 가장 큰 결손>

| 과제 세부 항목 | 관련 위치 (파일:라인) | 관련 심볼/클래스 | 연관성 분류 | 비고 |
|---|---|---|---|---|
| ... | `Source/.../X.h:42` | `FX::Method` | 직접 사용 가능 | 그대로 활용 |
| ... | `Source/.../Y.cpp:101` | `FY` | 확장 필요 | virtual 추가하면 가능 |
| ... | (없음) | (없음) | 미발견 | 신규 작성 필요 |
```

**연관성 분류 정의:**
- **직접 사용 가능**: 해당 항목을 위해 그대로 호출/사용/배치 가능.
- **확장 필요**: 기존 코드가 있으나 인터페이스 / 멤버 / 가상 함수 추가 등 부분 수정 필요.
- **단서만 발견**: 직접 관련은 아니지만, 신규 작성 시 이 코드를 참고하거나 같은 파일/디렉터리에 추가하는 게 자연스러움.
- **미발견**: 관련 코드를 못 찾았고 전부 신규 작성 필요.

---

## 제약 사항

1. **추측 금지**: 파일을 실제로 열어 확인하지 않은 사항은 보고하지 말 것. 만약 사전 정보(이미 알려진 내용)와 다른 점을 발견하면 그 점을 명시하라.
2. **재발견 금지**: 위 "알려진 사전 정보"에 이미 명시된 사항은 반복 보고하지 말 것. **단**, 그 정보가 다른 과제 항목에 어떻게 연결되는지(예: "`SkinVerticesToReferencePose`의 virtual 구조가 GPU/CPU 전환 hook으로 활용 가능")는 보고하라.
3. **단정 금지**: 사용자 선호 — "확신 없으면 그렇다고 말하라". 추정인지 확정인지 명확히 구분.
4. **출력 분량**: 과제 8개 파트 × 표 1개씩 + 각 표 위 짧은 요약. 그 외 산문은 최소화.
5. **마지막에 "탐색 중 의문점" 섹션 추가**: 코드에서 모호하거나 두 가지 이상으로 해석되는 부분을 3~5개 bullet으로 정리.

---

## Self-Check 전 보고 직전 확인

- [ ] 각 파트 표에 **모든 세부 항목**이 한 행씩 있는가? (누락 금지)
- [ ] "단서만 발견" 행은 왜 단서로 분류했는지 비고에 명시했는가?
- [ ] 파일 경로가 모두 `Source/...` 또는 `Shaders/...`로 시작하는 절대 경로인가?
- [ ] 라인 번호를 단정한 곳은 실제로 파일을 열어본 것인가?
- [ ] 사전 정보와 다른 발견이 있다면 명시했는가?
