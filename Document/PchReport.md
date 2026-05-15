# KraftonEngine PCH 적용 보고서

작업 디렉토리: `C:\GitDirectory11`
대상 프로젝트: `KraftonEngine.vcxproj` (Engine + Editor + GameClient + ObjViewer)
적용 방식: ForcedInclude `/FI` + `Directory.Build.props` + `Directory.Build.targets`

---

## 1. 개요

KraftonEngine은 ~588개의 `.cpp/.h` 파일을 가지며, 모든 TU(translation unit)가 STL/Windows/DirectX 헤더를 반복 파싱하던 상태였다. PCH를 도입해 다음을 달성한다:

- **STL/플랫폼 헤더 1회 파싱** — 233 in-scope TU × ~530ms 절감 추정 → 8코어 기준 wall-clock 약 15초/clean build 단축 추정
- **단일 진실 공급원(SSOT)** — `Engine/Core/CoreTypes.h` (STL) + 신규 `Engine/Math/Math.h` (Math) 모음집을 통해 한 줄 include로 모든 기반 헤더 노출
- **빌드 자동화 호환** — `Scripts/generateprojectfiles.py` 무수정. 재생성 후에도 PCH 설정 유지
- **CrossyGame 분리** — 동일 폴더의 `CrossyGame.vcxproj`는 PCH 비대상으로 자동 제외

---

## 2. 프로젝트 환경

| 항목 | 값 |
|------|---|
| 빌드 시스템 | Visual Studio 17 (v143), MSBuild |
| 솔루션 | `C:\GitDirectory11\KraftonEngine.sln` |
| 프로젝트 1 (PCH 대상) | `KraftonEngine\KraftonEngine.vcxproj` — Application |
| 프로젝트 2 (PCH 비대상) | `KraftonEngine\CrossyGame.vcxproj` — StaticLibrary |
| 생성 스크립트 | `Scripts\generateprojectfiles.py` (직접 XML 작성) |
| C++ 표준 | C++20 (`/std:c++20`) |
| Configurations | 7개 (Debug/Release × Win32/x64, ObjViewDebug, Demo, GameClient) |

**핵심 제약:**
- `.vcxproj`는 스크립트가 자동 생성하므로 **직접 수정 금지** (재실행 시 덮어쓰여짐)
- 두 `.vcxproj`가 동일 폴더에 공존 — 자동 import되는 `.props/.targets`는 양쪽에 모두 적용되므로 `MSBuildProjectName` 조건 필수

---

## 3. 생성된 파일 일람

### 코드/빌드 설정 (저장소 내, 빌드에 직접 참여)

| 경로 | 종류 | 역할 |
|------|------|------|
| `KraftonEngine\Source\pch.h` | 신규 | PCH 헤더 — 모음집 2개 + `<Windows.h>` + `<algorithm>` + C 카테고리 슬롯 |
| `KraftonEngine\Source\pch.cpp` | 신규 | `#include "pch.h"` 한 줄, `/Yc`로 PCH 바이너리 생성 |
| `KraftonEngine\Source\Engine\Math\Math.h` | 신규 | Math 모음집 — Vector/Matrix/MathUtils/Quat/Rotator/Transform 통합 |
| `Directory.Build.props` | 신규 | 모든 ClCompile에 `/Yu` + `/FI pch.h` 기본값 설정 (KraftonEngine만) |
| `Directory.Build.targets` | 신규 | `pch.cpp` 한 파일만 `/Yc`로 override + ForcedInclude 해제 |

### 문서 (Document/, 본 보고서 외)

| 경로 | 내용 |
|------|------|
| `project_mapping.md` | 빌드 자동화 스크립트 분석, 통합 지점 |
| `aggregator_headers.md` | CoreTypes.h + Math.h 모음집 구조 |
| `include_frequency.csv` | 233 TU × 288 헤더 빈도 표 |
| `include_frequency_summary.txt` | 상위 30개 헤더 요약 |
| `categorized.md` | A/B/C/D 카테고리 분류와 사유 |
| `vs_integration_guide.md` | 적용 방법 상세 가이드 |
| `skipped_files.md` | 분석 제외 폴더/파일 |
| `patches/Directory.Build.props.example` | props 백업본 |
| `patches/Directory.Build.targets.example` | targets 백업본 |
| `patches/generateprojectfiles_pch_patch.py.diff` | 대안(방식 1) 스크립트 패치 |

### 분석 도구 (재실행 가능)

| 경로 | 역할 |
|------|------|
| `Scripts\pch_include_scan.py` | include 빈도 스캔 — 향후 재분석 시 재실행 가능 |

---

## 4. 작업 절차 (단계별)

### 0단계: 프로젝트 구조 파악
- `Scripts\generateprojectfiles.py` 분석 (XML 직접 작성 방식, 라인 517–765의 `generate_vcxproj()`)
- 두 개의 `.vcxproj` 존재 확인 → 단일 PCH가 KraftonEngine.vcxproj만 커버
- 결과: `Document\project_mapping.md`

### 1단계: 모음집 헤더 점검 및 신설
- `Engine\Core\CoreTypes.h` 검증 — 10개 STL 헤더 포함 + UE 스타일 typedef
- Math 디렉토리에 통합 헤더 부재 확인 → **신규 `Engine\Math\Math.h` 생성**
  ```cpp
  #pragma once
  #include "Vector.h"
  #include "Matrix.h"
  #include "MathUtils.h"
  #include "Quat.h"
  #include "Rotator.h"
  #include "Transform.h"
  ```
- 결과: `Document\aggregator_headers.md`

### 2단계: include 빈도 분석
- `Scripts\pch_include_scan.py` 실행
- 스캔 범위: `Engine\` 재귀, `Editor\Packaging`, `Editor\Selection`, `Editor\UI\ContentBrowser`, `GameClient\`, `main.cpp`
- 결과: 233 TU × 288 헤더, `Document\include_frequency.csv`

### 3단계: 카테고리 분류 (보수적 기준)
- **A (PCH 직접)**: `<Windows.h>` (전이적으로 ~26 파일), `<algorithm>` (62/233 = 26.6%)
- **B (모음집 경유)**: 19개 (CoreTypes.h의 10 STL + Math.h의 6 math + `<cmath>` + `<cstring>` + `<DirectXMath.h>`)
- **C (검토 후 opt-in)**: `<filesystem>`, `<fstream>`, `Core/Log.h`, `Platform/Paths.h`, `Serialization/Archive.h`, `<d3d11.h>` — pch.h에 주석 슬롯으로
- **D (제외)**: GameFramework/Component/Material/Render/Lua/FBX SDK 헤더 (게임 로직 / 빈번 변경)
- 결과: `Document\categorized.md`

### 4단계: `pch.h` / `pch.cpp` 생성
- 위치: `KraftonEngine\Source\` (Source 루트 — Engine/Editor/GameClient 모두 인접)
- `pch.h` 섹션 구조: Platform → STL aggregator → Math aggregator → Extra STL → C 슬롯 → Engine/Editor/GameClient 슬롯
- `WIN32_LEAN_AND_MEAN` + `NOMINMAX`을 `<Windows.h>` include 이전에 정의
- `pch.cpp`: `#include "pch.h"` 한 줄만

### 5단계: 빌드 시스템 통합 (방식 2 — 권장)
- 저장소 루트(`C:\GitDirectory11\`)에 두 파일 생성:
  - `Directory.Build.props` — 기본 PCH 메타데이터 (모든 ClCompile에 `/Yu` + `/FI`)
  - `Directory.Build.targets` — `pch.cpp` 한 파일만 `/Yc`로 override
- `Scripts\generateprojectfiles.py`는 **무수정**
- `Scripts\GenerateProjectFiles.bat` 재실행하여 `pch.cpp/pch.h/Math.h`가 `.vcxproj`에 자동 등록 확인

---

## 5. 필수 설정 (실제 적용된 내용)

### 5.1 `Directory.Build.props`

저장소 루트에 위치. MSBuild가 `.vcxproj` 본문 **이전**에 자동 import.

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project>
  <ItemDefinitionGroup Condition="'$(MSBuildProjectName)' == 'KraftonEngine'">
    <ClCompile>
      <PrecompiledHeader>Use</PrecompiledHeader>
      <PrecompiledHeaderFile>pch.h</PrecompiledHeaderFile>
      <ForcedIncludeFiles>pch.h;%(ForcedIncludeFiles)</ForcedIncludeFiles>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
```

**역할:**
- 모든 ClCompile 항목에 `PrecompiledHeader=Use` 기본값 부여 (`/Yu`)
- `ForcedIncludeFiles=pch.h` → `/FI pch.h`로 모든 TU에 자동 주입 (소스 파일 수정 불필요)
- `Condition`으로 KraftonEngine.vcxproj에만 적용, CrossyGame.vcxproj는 제외

### 5.2 `Directory.Build.targets`

저장소 루트에 위치. MSBuild가 `.vcxproj` 본문 **이후**에 자동 import (이 시점에 `pch.cpp` 항목이 이미 존재).

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project>
  <ItemGroup Condition="'$(MSBuildProjectName)' == 'KraftonEngine'">
    <ClCompile Update="$(MSBuildThisFileDirectory)KraftonEngine\Source\pch.cpp">
      <PrecompiledHeader>Create</PrecompiledHeader>
      <ForcedIncludeFiles></ForcedIncludeFiles>
    </ClCompile>
  </ItemGroup>
</Project>
```

**역할:**
- `pch.cpp` 한 파일만 `PrecompiledHeader=Create` (`/Yc`)로 변경 — PCH 바이너리 생성자
- `ForcedIncludeFiles` 빈 값 → pch.cpp가 자기 자신을 `/FI`로 강제 include하는 자기참조 방지
- 절대 경로(`$(MSBuildThisFileDirectory)KraftonEngine\Source\pch.cpp`)로 Update= 매칭 — MSBuild가 정규화하여 `.vcxproj`의 상대경로 항목 `Source\pch.cpp`와 일치

### 5.3 `pch.h` 구조

```cpp
#pragma once

// Platform & System
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <Windows.h>
#endif

// Standard Library — via aggregator
#include "Engine/Core/CoreTypes.h"

// Math — via aggregator
#include "Engine/Math/Math.h"

// Extra STL
#include <algorithm>

// C category (opt-in by uncommenting)
// #include <filesystem>
// #include <fstream>
// #include "Core/Log.h"
// #include "Engine/Platform/Paths.h"
```

전체 본문: `KraftonEngine/Source/pch.h`

### 5.4 `pch.cpp`

```cpp
#include "pch.h"
```

---

## 6. 작동 원리 (왜 이 구조인가)

### MSBuild Import 순서와 PCH 적용 흐름

```
1. Microsoft.Cpp.Default.props (MSBuild 표준)
   ↓
2. Directory.Build.props 자동 import
   → ItemDefinitionGroup이 "모든 ClCompile에 /Yu+/FI 적용" 기본값 등록
   (이 시점엔 아직 ClCompile 항목이 없음 — 기본값만 등록 가능)
   ↓
3. .vcxproj 본문 평가
   → ClCompile Include="Source\pch.cpp", Include="Source\Engine\...cpp" 등 항목 등장
   → 모두 ItemDefinitionGroup의 기본값을 상속 (현재는 pch.cpp도 /Yu)
   ↓
4. Microsoft.Cpp.targets (MSBuild 표준)
   ↓
5. Directory.Build.targets 자동 import  ← pch.cpp가 이미 ClCompile 항목으로 존재
   → ItemGroup Update="...pch.cpp"가 매칭하여 pch.cpp만 /Yc로 변경
   → ForcedIncludeFiles 빈 값으로 덮어써서 pch.cpp의 자기참조 차단
   ↓
6. 빌드 시작
   → pch.cpp 컴파일 (/Yc) → KraftonEngine.pch 바이너리 생성
   → 나머지 TU 컴파일 (/Yu + /FI pch.h) → pch 바이너리 재사용
```

### 왜 두 파일로 분리해야 하나

`Update=`는 **이미 정의된** 항목만 수정한다. `Directory.Build.props`는 .vcxproj 본문 이전에 import되므로 그 안의 `<ClCompile Update="...pch.cpp">`는 silent no-op (대상 항목 부재). 결과:
- pch.cpp가 기본값 `/Yu`를 그대로 받음
- pch.cpp 자신이 만들지도 못한 PCH를 사용하려고 시도 → "PCH 파일을 열 수 없음" 에러

해결: `ItemDefinitionGroup` (기본값)은 props에, `ItemGroup Update=` (per-file override)는 targets에. 이게 PCH 두-파일 패턴의 핵심.

### 왜 `/FI` (ForcedInclude)인가

대안: 모든 `.cpp` 첫 줄에 `#include "pch.h"` 추가.
- KraftonEngine의 in-scope TU만 233개 (전체 270개) → 일괄 편집 부담 큼
- 신규 파일 추가 시 누락 위험
- `/FI`는 MSBuild 빌드 시 자동 주입 → 소스 파일 무수정, 신규 파일 자동 PCH 적용

`pch.cpp` 자신은 `/FI`를 받으면 자기참조가 되므로 `Directory.Build.targets`에서 명시적으로 ForcedIncludeFiles를 빈 값으로 덮어쓴다.

### 왜 CrossyGame.vcxproj에는 적용하지 않는가

CrossyGame은 별도 `pch.cpp/pch.h`가 없고, 작업 범위(Engine/Editor/GameClient)에도 포함되지 않는다. 두 `.vcxproj`가 동일 폴더에 있으므로 MSBuild는 둘 다에 `Directory.Build.*`를 적용하려 한다. `Condition="'$(MSBuildProjectName)' == 'KraftonEngine'"` 가드로 CrossyGame은 PCH 메타데이터를 받지 않는다.

### 왜 절대 경로 Update인가

처음 시도한 `Update="@(ClCompile)" Condition="'%(Filename)%(Extension)' == 'pch.cpp'"`는 MSBuild가 거부한다:
> "기본 제공 메타데이터 'Filename'을 참조할 수 없습니다 — 1 위치"

이 메타데이터(`%(...)`) 참조는 `<ClCompile Update="..." Condition="...">` 위치에서 허용되지 않는다. 절대 경로 직접 매칭으로 우회.

---

## 7. 검증 방법

### 7.1 정적 검증 (Visual Studio)

1. 솔루션 열기 → **Clean Solution**
2. Solution Explorer에서 `pch.cpp` 우클릭 → **Properties** → **C/C++** → **Precompiled Headers**
   - **Precompiled Header**: `Create (/Yc)` ✓
3. 임의 다른 `.cpp` (예: `Engine/Runtime/Engine.cpp`) 동일 경로
   - **Precompiled Header**: `Use (/Yu)` ✓
4. CrossyGame 프로젝트의 임의 `.cpp` 동일 경로
   - **Precompiled Header**: `Not Using Precompiled Headers` ✓ (PCH 비적용)

### 7.2 빌드 검증

```cmd
msbuild KraftonEngine.sln /p:Configuration=Debug /p:Platform=x64 /v:m
```

기대 출력:
- `pch.cpp` 컴파일 시 `KraftonEngine.pch` 생성 로그 1회
- 나머지 TU는 PCH 사용 (콘솔 verbose 출력에서 `/Yu` 플래그 확인 가능)
- CrossyGame 빌드 시 PCH 관련 로그 없음
- `KraftonEngine\Build\Debug\KraftonEngine.pch` 파일 존재 확인

### 7.3 빌드 시간 측정 (전후 비교)

```cmd
msbuild KraftonEngine.sln /p:Configuration=Debug /p:Platform=x64 /t:Rebuild /v:m /clp:Summary
```

`Time Elapsed` 값을 PCH 적용 전 측정치와 비교. 추정 단축치 약 15초/clean build (8코어 가정, 단정 아님).

### 7.4 `/FI` 작동 검증 (선택)

임의 `.cpp` 파일에서 `#include "Engine/Core/CoreTypes.h"`를 일시 제거 후 빌드 → 정상 컴파일되면 `/FI pch.h`가 작동 중인 것.

---

## 8. 트러블슈팅 (실제 경험한 문제 포함)

| 증상 | 원인 | 해결 |
|------|------|------|
| `cannot open precompiled header file: ...\KraftonEngine.pch` 다수 TU에서 발생 | `pch.cpp`가 `/Yc`를 받지 못함. 보통 `<ClCompile Update="...">` 가 `Directory.Build.props`에 있어 항목 정의 이전에 실행되어 no-op이 된 경우 | Update 항목을 **`Directory.Build.targets`**로 이동. VS Property에서 `pch.cpp`의 PrecompiledHeader가 `Create`인지 확인 |
| `built-in metadata "Filename"을 참조할 수 없습니다 — 1 위치` (Directory.Build.targets) | `<ClCompile Update="@(ClCompile)" Condition="'%(Filename)%(Extension)' == 'pch.cpp'">` 패턴 사용 — MSBuild가 이 위치의 메타데이터 참조를 거부 | Update에 **절대 경로 직접 지정**: `Update="$(MSBuildThisFileDirectory)KraftonEngine\Source\pch.cpp"` |
| `C1853: '...pch' is not a precompiled header file created with this compiler` | 이전 빌드의 stale `.pch` | Clean Solution → Rebuild |
| `C1083: Cannot open include file: 'pch.h'` | include 경로에 `Source` 부재 | 스크립트 라인 168의 `INCLUDE_PATHS`에 `"Source"`가 있는지 확인 (기본 포함됨) |
| CrossyGame이 pch.h 누락 호소 | `Condition="'$(MSBuildProjectName)' == 'KraftonEngine'"` 가드 누락 | props와 targets 양쪽 모두에 동일 조건이 있는지 확인 |
| 단일 파일만 느리게 컴파일 | 해당 파일 메타데이터가 `NotUsing`으로 override됨 | `.c` 파일 등 PCH 비호환 파일이면 `Directory.Build.targets`에 명시적 `Update=` 추가 |

---

## 9. 유지보수 가이드

### STL 헤더 추가가 필요할 때
- `pch.h`에 직접 추가하지 말 것
- `Engine/Core/CoreTypes.h`에 추가 → PCH가 자동 반영
- 이유: SSOT 유지. CoreTypes를 직접 include하는 코드와의 일관성

### Math 헤더 추가가 필요할 때
- `Engine/Math/Math.h`에 추가 → PCH가 자동 반영
- 신규 math 헤더의 의존성 순서에 주의 (Vector → Matrix → ... → Transform)

### C 카테고리 헤더 opt-in 절차
1. `pch.h`에서 해당 라인의 `//` 주석 제거
2. 빌드 시간 영향 측정 (rebuild × 3회, 평균)
3. 만족스러우면 commit, 아니면 되돌림

### 새 헤더를 A 카테고리로 승격하는 기준
모두 충족해야 안전:
- `pch_include_scan.py` 재실행 결과 TU% ≥ 15%
- `git log --since="6 months ago"` 변경 횟수 ≤ 1
- 게임 로직/렌더 컴포넌트 아님
- 모음집(`CoreTypes.h`, `Math.h`)으로 옮길 수 없는 경우만

### PCH 변경 시 비용
KraftonEngine.vcxproj의 PCH 변경 = ~233 TU 전체 재빌드. 잦은 변경은 비용이 큼.
**의심스러우면 C 또는 D 카테고리에 두라.**

### `Scripts/generateprojectfiles.py` 수정 금지
권장 적용 방식(방식 2)은 스크립트와 완전히 분리되어 있다. 스크립트는 PCH 설정을 알지 못하고 알 필요도 없다. 스크립트가 업데이트되어도 PCH 설정은 영향받지 않는다.

만약 스크립트 통합 방식(방식 1)으로 전환을 원하면 `Document/patches/generateprojectfiles_pch_patch.py.diff` 참조. 단, props/targets는 제거해야 한다 (이중 설정 방지).

---

## 10. 빌드 시간 절감 추정치

**산식:**
```
절감 ≈ (TU 수 - 1) × PCH 헤더들의 평균 단위 파싱 시간 / 병렬 작업 수
```

**구성요소 (MSVC v143, /utf-8, x64 기준 추정):**

| PCH 내용 | TU당 절감 (추정) |
|---------|------|
| `<Windows.h>` (WIN32_LEAN_AND_MEAN + NOMINMAX 적용) | ~220 ms |
| `<DirectXMath.h>` (Math 모음집 경유) | ~90 ms |
| CoreTypes 경유 STL 묶음 (`<vector> <string> <unordered_map>` 등) | ~120 ms |
| `<algorithm>` | ~60 ms |
| Math 6개 헤더 자체 | ~40 ms |
| **TU당 합계** | **~530 ms** |

**총량 추정:**
- 컴파일러 작업 절감: 233 × 530ms ≈ **123초**
- 8코어 병렬 빌드 wall-clock 단축: **약 15초 / clean build**
- 증분 빌드에서도 PCH 재사용으로 작은 이득 누적

**중요:** 위 수치는 **추정**. 실측은 디스크/RAM/안티바이러스 제외 설정에 따라 ±30~50% 변동 가능. 단정 짓지 말 것.

---

## 11. 관련 문서 참조

| 주제 | 문서 |
|------|------|
| 프로젝트 구조 및 스크립트 분석 | `Document/project_mapping.md` |
| 모음집 헤더 상세 | `Document/aggregator_headers.md` |
| 헤더 빈도 원시 데이터 | `Document/include_frequency.csv` |
| 카테고리 분류 사유 | `Document/categorized.md` |
| 적용 가이드 상세 | `Document/vs_integration_guide.md` |
| 분석 제외 파일 | `Document/skipped_files.md` |
| 백업본 (props/targets) | `Document/patches/*.example` |
| 대안 (스크립트 패치) | `Document/patches/generateprojectfiles_pch_patch.py.diff` |
