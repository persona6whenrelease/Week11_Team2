# Implementation Prompt P6 (Optional) — T9: Smoke Test Checklist

## 목적

P1~P5의 누적 결과가 end-to-end로 동작하는지 **수동 시나리오 1회**를 통해 확인한다.
자동화 인프라가 없으므로 본 prompt는 **체크리스트 문서 작성**과 **수동 실행 가이드**까지만 다룬다.

## 전제

- P1~P5 머지 완료. Sound notify와 CameraShake notify가 game 모드에서 실제 효과로 이어지는 상태.

## 참조 문서

- 통합 설계: `Document/animnotify_integration_design.md`
  - 인용 섹션: **T9 (Section 7)**, **Section 6 (Sequence Diagram)**

## 엄격한 제약

1. **본 PR은 source code 변경을 포함하지 않는다**. 산출물은 checklist 문서 1개 (`Document/animnotify_smoke_test.md`) 와 (선택적으로) 테스트용 .animseq fixture 한두 개.
2. fixture 생성도 코드 변경이 아닌 **에디터에서 manual 생성 + 저장 + 커밋** 절차로 다룬다. 만약 fixture 저장이 어렵다면 fixture 없이 "에디터에서 즉석 생성 후 재생" 시나리오로 대체 가능.

## 작업 항목

### Step 1 — Checklist 문서 작성

`Document/animnotify_smoke_test.md` 에 다음 구조로 작성:

```
# AnimNotify Smoke Test — Sound + CameraShake

## 0. 사전 조건
- KraftonEngine 빌드 성공
- SoundManager에 최소 1개 효과음 ID가 LoadEffect로 등록되어 있음
  - 등록 위치(코드/스크립트): [경로 명시]
  - 등록된 ID 예시: "test_pop", "test_boom" 등 (실제 등록된 것)
- Game 모드 진입 시 PlayerController가 생성되는 시나리오가 준비되어 있음

## 1. 시나리오 A — Sound Only
1. AnimSequence 에디터 열기
2. 임의의 .animseq 로드 (또는 신규 생성)
3. timeline에서 우클릭 → "Add Sound Notify at current frame"
4. inline editor에서 SoundId를 사전 등록된 ID로 입력
5. 저장
6. Game 모드 진입 → 같은 sequence 재생
7. **기대**: 해당 frame 도달 시 효과음 1회 재생
8. 동일 sequence를 loop 재생 → 매 사이클 1회씩 재생되는지 확인

## 2. 시나리오 B — CameraShake Only
1. (시나리오 A의 1~2 반복)
3. "Add Camera Shake Notify at current frame"
4. inline editor에서 Duration=1.0, LocationAmplitude=(5,5,5), RotationAmplitude=(2,2,2), Frequency=10 등 식별 가능한 값 입력
5. 저장
6. Game 모드 진입 → 재생
7. **기대**: 해당 frame 도달 시 카메라가 1초간 흔들림

## 3. 시나리오 C — 둘 다
1. 같은 sequence에 Sound 1개 + CameraShake 1개를 다른 frame에 배치
2. Game 모드에서 재생
3. **기대**: 두 효과 모두 정확한 frame에 발화
4. 같은 frame에 두 notify를 두면 → 같은 프레임에 동시 발화

## 4. 시나리오 D — Preview vs Game (R5 검증)
1. 시나리오 A의 sequence를 에디터 preview tab에서 재생
2. **기대**: Sound는 들림 (R5 결정에 따른 의도된 동작). CameraShake는 outer chain에서 PC가 nullptr이면 silent — crash 없음.
3. log에 에러나 경고가 노이즈로 쌓이지 않는지 확인

## 5. 시나리오 E — Legacy v3 호환 (D3 검증)
1. v3로 저장된 기존 .animseq가 있다면 그 파일을 로드
   (없다면 본 시나리오 skip + 보고에 명시)
2. 에디터에서 열어 notify가 보이는지 확인
3. inline editor에서 해당 notify가 "(no type — legacy)" 로 표시되는지
4. Game 모드 재생 시 dispatch에서 skip (효과 발화 없음)
5. 해당 sequence를 다시 저장 → AssetVersion이 4로 승격되었는지 mental trace

## 6. 시나리오 F — 빈 / 미등록 SoundId (D6 검증)
1. Sound notify를 추가하고 SoundId를 비워 둠
2. Game 모드 재생 → crash 없음, SoundManager UE_LOG만 남음
3. SoundId를 "nonexistent_id_xxx" 같은 미등록 값으로 입력
4. Game 모드 재생 → 동일하게 crash 없음, log만

## 7. Regression 확인
- 본 변경 이전에 동작하던 Lua `GetTriggeredNotifies` polling이 그대로 동작하는지
- 기존 .animseq의 다른 모든 데이터(curve, additive 등)가 손상 없이 로드되는지
- 에디터 timeline의 기존 동작(drag, name 편집, color, duration 편집)이 regression 없는지

## 8. 결과 보고 템플릿
각 시나리오별로 다음 표를 채워 보고:

| Scenario | Pass / Fail | 비고 |
|---|---|---|
| A | | |
| B | | |
| C | | |
| D | | |
| E | | |
| F | | |
| Regression | | |
```

### Step 2 — (선택) Fixture 생성

테스트 자동화는 없지만, 재현 가능한 fixture를 두면 향후 유용:

1. `Test/Fixtures/AnimNotify/` 같은 디렉토리에 (또는 본 프로젝트의 테스트 자산 컨벤션 위치):
   - `sound_only.animseq`
   - `shake_only.animseq`
   - `combined.animseq`
2. 각각 위 시나리오 A~C에 대응
3. 사용된 SoundId / Shake 파라미터를 별도 README에 메모

본 단계는 선택적이며, 본 프로젝트의 asset 디렉토리 정책에 따라 생략 가능.

### Step 3 — 보고

- `Document/animnotify_smoke_test.md` 작성 완료
- 시나리오 8번 표를 직접 실행하여 채운 결과 별도 보고
- 발견된 regression이나 R5/R6/D6/D3 정책 위반 사례가 있으면 별도 issue로 분리

## 절대 금지

- 본 PR에서 source code 변경 (T1~T8의 산출이 잘못되었다고 판단되면 해당 PR을 revert하거나 별도 fix PR로 진행 — 본 PR에 섞지 않는다)
- 자동화 테스트 인프라 신설 (본 프로젝트의 기존 테스트 인프라 점검 후 별도 논의)
- 시나리오를 임의로 추가/생략 (위 A~F + Regression은 모두 수행)

## 보고 시 포함할 것

1. `Document/animnotify_smoke_test.md` 생성 여부
2. 시나리오 A~F + Regression 각각의 Pass/Fail
3. v3 fixture 존재 여부 — 없으면 시나리오 E skip 사유 명시
4. fixture 디렉토리 생성 여부와 위치
5. 후속 작업 제안 (자동화, asset picker UI, reverse/seek 정책 등 — D7, R5 등에서 보류된 항목)

(끝)
