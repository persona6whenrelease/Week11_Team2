# ContentBrowser `.asset` 파일 인식 경로 진단

> 본 문서는 **진단 및 설계 제안**이다. 코드 변경을 포함하지 않는다. 모든
> `file:line`은 `feature/project` 브랜치 (2026-05-19 시점) 작업 트리의 실제
> 소스를 직접 열어 확인한 결과이다. 미확인 항목은 명시적으로 "확인 필요"로
> 표시한다.

---

## TL;DR — 근본 원인

`.asset` 파일을 더블클릭하면 윈도우 OS의 "이 파일을 어떤 앱으로 열지"
다이얼로그가 뜨는 이유는 다음 한 문장이다:

> ContentBrowser의 Element 분류 if-else 체인
> ([ContentBrowser.cpp:262-301](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowser.cpp:262))에
> `.asset` 분기가 없어 기본 `ContentBrowserElement`로 분류되고, 그 베이스
> 클래스의 `OnDoubleLeftClicked`가 `ShellExecuteW(..., L"open", ...)`을 호출하기
> 때문이다
> ([ContentBrowserElement.h:34](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:34)).

세 가지가 동시에 성립한다:

1. `ReadDirectory`는 확장자 화이트리스트 없이 디렉토리 안의 모든 파일을
   `FContentItem`으로 만든다 — 즉 `.asset`은 **목록에는 표시된다**.
2. 분류 if-else에 `.asset` 분기가 **없다**.
3. 기본 `ContentBrowserElement`의 더블클릭 핸들러는 OS 위임(`ShellExecuteW`)
   하나뿐이다.

---

## 1. ContentBrowser의 파일 스캔 경로

### 확인된 사실

**디렉토리 스캔 진입점.**
[ContentBrowser.cpp:404-438](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowser.cpp:404)
`FEditorContentBrowserWidget::ReadDirectory(std::wstring Path)` 가 현재
디렉토리를 한 단계만 훑어 `FContentItem` 배열을 만든다. 그 본문(요약):

```cpp
for (const auto& Entry : std::filesystem::directory_iterator(Path))
{
    if (Entry.is_directory() && (Name == L"Bin" || Name == L"Build" ||
                                  Name == L".git"  || Name == L".vs"))
        continue;

    FContentItem Item;
    Item.Path = Entry.path();
    Item.Name = Name;
    Item.bIsDirectory = Entry.is_directory();
    Items.push_back(Item);
}
```

**핵심: 확장자 필터가 없다.** `Bin / Build / .git / .vs` 디렉토리만 제외하고,
파일은 종류 불문 전부 수집된다. 결론: **`.asset` 파일은 목록에 정상 표시된다.**

**`FContentItem` 구조** —
[ContentItem.h:5-10](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentItem.h:5)
`Path`(절대 fs 경로), `Name`, `bIsDirectory` 세 필드뿐. 확장자/타입 메타 없음.

**스캔 후 Element 변환.**
[ContentBrowser.cpp:254-306](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowser.cpp:254)
`RefreshContent()`가 위 `ReadDirectory` 결과를 받아 if-else 체인으로 Element
서브타입을 만든다. **확장자만 본다.** (자세한 내용은 2번 절에서 다룬다.)

### 판정

- `.asset` 파일이 ContentBrowser 목록에 **나타난다** — 확장자 필터가 없으므로.
  현상상 윈도우 다이얼로그가 뜨는 시점이 더블클릭 직후라는 것은, 클릭할
  엔트리는 이미 화면에 있다는 뜻이고, `ReadDirectory`/`RefreshContent` 자체는
  통과했음을 의미한다.
- 단, 이 fact는 사용자 보고("`.asset` 더블클릭 시 윈도우 다이얼로그가 뜬다")와
  정합한다 — 보이지 않는 파일은 더블클릭할 수 없으므로 보이긴 보인다는 뜻.
- 즉 문제는 "스캔 단계"가 아니라 "분류 단계 + 더블클릭 핸들러"에 있다.

### 미확인 항목

- 사용자의 `.asset` 파일이 실제로 어느 디렉토리에 저장되었는지는 본 진단
  범위 외다. 다만 (a) `AnimSequenceEditorTab` Save As 기본 시작 폴더는
  `FPaths::AssetDir()` (`Asset/`,
  [AnimSequenceEditorTab.cpp:898](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:898))
  이므로, 사용자가 별도 변경 없이 저장했다면 `Asset/<stem>.asset`에 있을 것이고,
  ContentBrowser는 `RootDir = exe 옆`이 기본이므로
  ([ContentBrowser.cpp:231](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowser.cpp:231)
  `Refresh`가 `RootDir`을 루트로 잡음), 트리에서 `Asset/`로 내려가면 보일 것이다.

---

## 2. Element 타입 분류 로직

### 확인된 사실

**분류 코드 전체.**
[ContentBrowser.cpp:260-301](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowser.cpp:260):

```cpp
if (Content.bIsDirectory)               element = std::make_shared<DirectoryElement>();
else if (Content.Path.extension() == ".Scene")              element = std::make_shared<SceneElement>();
else if (Content.Path.extension() == ".Prefab")             element = std::make_shared<PrefabElement>();
else if (Content.Path.extension() == ".obj")                element = std::make_shared<ObjectElement>();
else if (Content.Path.extension() == ".mat")                element = std::make_shared<MaterialElement>();
else if (Content.Path.extension() == ".lua")                element = std::make_shared<LuaScriptElement>();
else if (Content.Path.extension() == ".curve")              element = std::make_shared<CurveElement>();
else if (Content.Path.extension() == ".fbx"
      || Content.Path.extension() == ".FBX")                element = std::make_shared<FBXElement>();
else if (Content.Path.extension() == ".png"
      || Content.Path.extension() == ".PNG")                element = std::make_shared<PNGElement>();
else                                                         element = std::make_shared<ContentBrowserElement>();
```

**분류 키:** 확장자만 본다 (`std::filesystem::path::extension()`). 파일 내용,
헤더(`FAssetFileHeader.AssetType`), 별도 메타데이터는 일절 참조하지 않는다.
대소문자 처리도 `.fbx`/`.FBX`, `.png`/`.PNG` 두 분기에서만 수기로 처리되어 있고
나머지(.Scene/.Prefab/.mat/.lua/.curve/.obj 등)는 케이스 매칭이 정확해야만
잡힌다.

**현재 존재하는 Element 타입들.**
[ContentBrowserElement.h](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h)
기반:

| 클래스 | 정의 위치 | 어디서 생성되는가 |
| --- | --- | --- |
| `ContentBrowserElement` (base) | [.h:10](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:10) | 분류 if-else의 **최종 fallback**. [ContentBrowser.cpp:300](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowser.cpp:300) |
| `ExpandableElement` | [.h:50](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:50) | 자체 인스턴스 없음. `ImportableElement` 부모. |
| `DirectoryElement` | [.h:66](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:66) | 디렉토리. [ContentBrowser.cpp:264](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowser.cpp:264) |
| `SceneElement` | [.h:73](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:73) | `.Scene` |
| `ObjectElement` | [.h:80](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:80) | `.obj` |
| `ImportedStaticMeshElement` | [.h:87](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:87) | 분류 if-else에 **없음.** `FBXElement` 전개 자식. [ContentBrowserElement.cpp:655](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:655) |
| `ImportedSkeletalMeshElement` | [.h:95](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:95) | 분류 if-else에 **없음.** `FBXElement` 전개 자식. 같은 라인. |
| `ImportedAnimSequenceElement` | [.h:103](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:103) | 분류 if-else에 **없음.** `FBXElement` 전개 자식. [ContentBrowserElement.cpp:325](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:325) (헬퍼 `AddAnimSequenceElement` 통해) → 호출 지점 [ContentBrowserElement.cpp:713](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:713) |
| `ImportableElement` | [.h:111](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:111) | 자체 인스턴스 없음. `FBXElement` 부모. |
| `FBXElement` | [.h:126](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:126) | `.fbx` / `.FBX` |
| `PNGElement` | [.h:138](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:138) | `.png` / `.PNG` |
| `MaterialElement` | [.h:146](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:146) | `.mat` |
| `PrefabElement` | [.h:158](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:158) | `.Prefab` |
| `LuaScriptElement` | [.h:164](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:164) | `.lua` |
| `CurveElement` | [.h:170](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:170) | `.curve` |

**주의 — `ImportedAnimSequenceElement`는 디스크의 실제 파일을 표현하지 않는다.**
이 Element는 오직 `FBXElement`가 펼쳐졌을 때 그 FBX 안의 가상 sub-asset을
표현한다.
[ContentBrowserElement.cpp:319-320](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:319):

```cpp
AnimItem.Path = FPaths::ToWide(AnimSequencePath);  // "<fbx>#Anim_<sid>_<aid>"
AnimItem.Name = FPaths::ToWide(DisplayName);
```

즉 `ContentItem.Path`에 들어가는 값이 **가상 참조 문자열**이며 실제 디스크
파일 경로가 아니다. 더블클릭 핸들러
([ContentBrowserElement.cpp:573-581](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:573))
가 이 가상 참조를 그대로
`Context.EditorEngine->OpenAnimSequenceAsset(...)`에 넘기고,
[MeshManager.cpp:151-170](KraftonEngine/Source/Engine/Asset/Import/MeshManager.cpp:151)
`ResolveAnimSequenceReference`가 `#Anim_` 분기 또는 `.asset` 분기로 보내준다.

따라서 **현재 코드에서 `ImportedAnimSequenceElement`는 디스크의 `.asset` 파일에
대응하는 경로가 아니다.** 그 클래스를 그대로 재사용하려면 `ContentItem.Path`에
들어가는 값이 가상 참조이든 실제 디스크 경로이든 동일하게 동작하는지
확인해야 한다.

`OpenAnimSequenceAsset(FPaths::ToUtf8(ContentItem.Path.wstring()))` →
`FMeshManager::ResolveAnimSequenceReference`에 도달했을 때:
- 가상 참조(`#Anim_` 마커): FBX 경로 처리.
- `.asset` 확장자: `LoadAnimSequenceFromFile`에서
  [MeshManager.cpp](KraftonEngine/Source/Engine/Asset/Import/MeshManager.cpp)
  의 `FWindowsBinReader`에 그대로 넘어가고, 이 reader는 절대/상대 모두 받는다
  ([Paths.cpp:182-185](KraftonEngine/Source/Engine/Platform/Paths.cpp:182)
  `TryResolvePackagePath`가 `Input.is_absolute()`을 그대로 통과시킨다). 따라서
  ContentBrowser의 절대 경로(`C:\...\Asset\Foo.asset`) 자체로 동작 가능.

### 판정

- 분류 키는 확장자뿐. 본문 헤더(`FAssetFileHeader.AssetType`)는 보지 않는다.
- `.asset` 확장자에 대한 분기가 **없다**. → fallback `ContentBrowserElement`로
  떨어진다.
- 같은 `.asset` 확장자를 여러 `AssetType`(AnimSequence, SkeletalMesh, …)이
  공유하기로 한 설계 결정
  ([animsequence_asset_saveas_design.md](Document/animsequence_asset_saveas_design.md)
  결정 1)을 분류 단계에서 반영하려면, 본 단계에서 헤더의 `AssetType`까지
  들여다보는 추가 검사가 필요하다 — 즉 단순 "확장자 → 한 Element 타입" 매핑은
  부족하다.

### 미확인 항목

- 헤더 검사(`FAssetFileHeader`)를 ContentBrowser 분류 단계에서 수행할 때의
  I/O 비용 영향 — 디렉토리 한 번 표시에 그 디렉토리 내 모든 `.asset` 파일을
  파일 헤드 16바이트씩 읽어야 한다. 현재 `FBXElement`가 expand 시점에 캐시
  binary를 디스크 확인하는 정도
  ([ContentBrowserElement.cpp:608-613](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:608)
  `HasImportedBinary`)는 한 파일당 한 번 `std::filesystem::exists`이므로
  더 가볍다. 본 진단에서는 비용 측정을 하지 않았다.

---

## 3. 더블클릭 동작과 OS 위임 폴백

### 확인된 사실

**더블클릭 디스패치 지점.**
[ContentBrowserElement.cpp:426-454](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:426)
`ContentBrowserElement::Render`가 hover + `IsMouseDoubleClicked` 조건에서
`OnDoubleLeftClicked(Context)`를 호출한다
([:436-439](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:436)).

**기본 (base) 더블클릭 핸들러 — OS 위임 폴백의 실제 지점.**
[ContentBrowserElement.h:34](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:34):

```cpp
virtual void OnDoubleLeftClicked(ContentBrowserContext& Context) {
    ShellExecuteW(nullptr, L"open", ContentItem.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
};
```

`ShellExecuteW(nullptr, L"open", <Path>, ...)`는 Windows 표준 동작으로 다음 순서를
탄다:
1. 해당 확장자에 등록된 핸들러가 있으면 그것을 호출.
2. 없으면 OS의 "이 파일을 어떤 앱으로 열지" 다이얼로그를 표시.

`.asset` 확장자는 일반적으로 OS 레지스트리에 핸들러가 없으므로 (2)번 다이얼로그가
뜬다. **사용자가 보고한 현상의 정확한 원인.**

**기타 검색 — 본 코드베이스에서 OS 위임 또 다른 진입점이 있는가?**
[Grep "ShellExecute" 전체 Source/](전체 검색)
결과는 위 한 곳뿐이다. 즉 본 베이스 클래스의 한 줄이 유일한 OS 위임 진입.

**각 Element의 더블클릭 동작 비교.**

| 클래스 | OnDoubleLeftClicked | 설명 |
| --- | --- | --- |
| `ContentBrowserElement` (base) | `ShellExecuteW(..., L"open", Path, ...)` | **OS 위임 (윈도우 다이얼로그)** |
| `DirectoryElement` | `Context.CurrentPath = Path; PendingRevealPath = ...; bIsNeedRefresh = true;` ([:502-507](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:502)) | 디렉토리 진입 |
| `SceneElement` | `EditorEngine->LoadSceneFromPath(...)` ([:537-543](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:537)) | 씬 로드 |
| `ImportedStaticMeshElement` | `(void)Context;` (no-op) ([.h:92](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:92)) | 무동작 |
| `ImportedSkeletalMeshElement` | `(void)Context;` (no-op) ([.h:100](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:100)) | 무동작 |
| `ImportedAnimSequenceElement` | `EditorEngine->OpenAnimSequenceAsset(ToUtf8(Path))` ([:573-581](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:573)) | **여기로 흘려야 한다** |
| `FBXElement` | `EditorEngine->OpenSkeletalMeshViewerAsset(...)` ([:583-591](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:583)) | SkeletalMesh 뷰어 |
| `CurveElement` | `ContentBrowserElement::OnDoubleLeftClicked(Context);` ([:738-740](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:738)) | 베이스 호출 — 결과적으로 OS 위임 |
| `PNGElement` / `MaterialElement` / `PrefabElement` / `LuaScriptElement` / `ObjectElement` | 미오버라이드 → 베이스 (OS 위임) | 윈도우 다이얼로그가 뜨거나 핸들러가 등록되어 있으면 그 앱이 뜸 |

즉 **이 코드베이스에는 "알 수 없는 확장자 → 정중하게 무시" 같은 분리된 분기가
없다.** "오버라이드된 Element 타입이 아닌 한, 모든 더블클릭은 ShellExecuteW로
간다."

### 윈도우 다이얼로그가 뜬 근본 원인 — 1줄 결론

**`.asset` 확장자가 [ContentBrowser.cpp:262-301](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowser.cpp:262)
의 분류 if-else에 없어 fallback `ContentBrowserElement`로 분류되고, 그 베이스의
[OnDoubleLeftClicked](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:34)
가 `ShellExecuteW(L"open", ...)`을 호출하기 때문이다.**

### 미확인 항목

- `CurveElement::OnDoubleLeftClicked`이 일부러 베이스를 호출해 OS 위임을 받는
  것이 의도된 동작인지 — 추측이지만 *.curve를 외부 에디터로 열기 위해서로
  보인다. 본 진단 범위 외.

---

## 4. `.asset`을 우리 경로로 연결하기 위한 변경 지점

`.asset` 파일(헤더 `AssetType == AnimSequence`)을 더블클릭했을 때
`EditorEngine->OpenAnimSequenceAsset`으로 흐르게 하는 방법을 두 가지 모두 적는다.
**어느 하나로 단정하지 않는다.**

### 공통 전제

- 분류 단계에서 헤더를 읽는 코드는 본 진단 범위에서 작성하지 않는다. 의사
  코드만 적는다.
- `FAssetFileHeader` 구조는
  [AssetFileHeader.h:17-39](KraftonEngine/Source/Engine/Asset/AssetFileHeader.h:17),
  처음 16바이트만 읽으면 `Magic / AssetType / Version / PayloadSize`를 모두
  얻는다. 검증은 `IsValid(EAssetType, uint32)`.
- `EAssetType` ([AssetTypes.h:16-26](KraftonEngine/Source/Engine/Asset/AssetTypes.h:16))
  은 AnimSequence 외에도 StaticMesh / SkeletalMesh / Material / Texture2D / FbxScene
  까지 여러 타입을 한 enum으로 묶고 있다. 즉 `.asset` 확장자는 향후 다중 타입
  컨테이너가 될 가능성이 크다.

### 방식 X — 새 Element 타입 신설 (`AnimSequenceAssetElement`)

**필요 변경.**

1. 신설 클래스:
   [ContentBrowserElement.h](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h)
   에 `class AnimSequenceAssetElement final : public ContentBrowserElement` 추가.
   `OnDoubleLeftClicked`을
   `Context.EditorEngine->OpenAnimSequenceAsset(FPaths::ToUtf8(ContentItem.Path.wstring()))`
   로 오버라이드. 아이콘은 `BuildAnimSequenceSnapshot`을 절대 디스크 경로
   기준으로 호출 가능
   ([ContentBrowserElement.cpp:169-201](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:169)
   가 이미 `ResolveAnimSequenceReference`를 통해 동작 — 동일하게 재사용 가능).

2. 분류 분기 추가:
   [ContentBrowser.cpp:298](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowser.cpp:298)
   의 마지막 `else` 직전에:
   ```cpp
   else if (Content.Path.extension() == ".asset" /* && 헤더에서 AnimSequence 확인 */)
       element = std::make_shared<AnimSequenceAssetElement>();
   ```
   - 헤더 검사가 없으면 모든 `.asset`이 AnimSequence Element로 분류되어
     SkeletalMesh `.asset` 등이 잘못된 핸들러로 갈 위험.
   - 헤더 검사가 있으면, 16바이트만 읽어 `EAssetType`별로 다른 Element 타입을
     반환할 수 있다.

3. 헤더 검사 헬퍼: 본 진단 범위 외이나, 동선상 `FAssetFileHeader`를 한 번 읽는
   자유 함수를 `Asset/AssetFileHeader.h` 옆 또는 `MeshManager.cpp` 인근에
   두면 자연스럽다. 함수 시그니처 예: `bool TryReadAssetType(const FString&
   Path, EAssetType& OutType)`.

**트레이드오프.**

| 항목 | 평가 |
| --- | --- |
| 변경 범위 | 클래스 추가 1, 분기 추가 1, 헤더 헬퍼 추가 1. 중간. |
| 확장성 | 우수. 각 `EAssetType`마다 전용 Element 타입을 만들면 더블클릭 시 자연스럽게 분기. 아이콘/Drag&Drop 타입 ID(`GetDragItemType`)도 타입별 분리. |
| 향후 다른 `AssetType` `.asset` 지원 | `else if (.asset && AssetType == SkeletalMesh)` 형태로 분기만 추가하면 끝 — 같은 패턴 반복. |
| 부담 | 분류 단계에서 파일을 살짝 열어 헤더를 읽어야 함. 디렉토리당 `.asset` 파일 개수에 비례하는 I/O 증가. (대안: 1차 분류는 확장자만으로 하고 2차로 `OnDoubleLeftClicked` 시점에 헤더 검사 후 분기 — 그러면 분류 단계 비용 0이지만 코드 흐름이 두 단계로 나뉨.) |

### 방식 Y — 기존 핸들러 재사용 (`ImportedAnimSequenceElement` 재사용 또는 fallback에 분기 삽입)

**Y-1. `ImportedAnimSequenceElement`를 그대로 분류에 추가.**

[ContentBrowser.cpp:298](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowser.cpp:298)
의 마지막 `else` 직전에:
```cpp
else if (Content.Path.extension() == ".asset")
    element = std::make_shared<ImportedAnimSequenceElement>();
```

- `ImportedAnimSequenceElement::OnDoubleLeftClicked`은
  ([ContentBrowserElement.cpp:573-581](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:573))
  `Context.EditorEngine->OpenAnimSequenceAsset(FPaths::ToUtf8(ContentItem.Path.wstring()))`
  이고, 이는 가상 참조든 실제 디스크 경로든 `ResolveAnimSequenceReference`가
  분기 처리할 수 있다. 이 부분은 이미
  [MeshManager.cpp](KraftonEngine/Source/Engine/Asset/Import/MeshManager.cpp)
  의 `.asset` 분기가 구현되어 있다.
- 단, **확장자 분류만으로는 부족**한 위험이 존재: 같은 `.asset`을
  AnimSequence가 아닌 다른 타입(향후 SkeletalMesh `.asset` 등)이 가져갈
  여지가 있다. 잘못된 타입이면 `LoadAnimSequenceFromFile`이
  `IsValidSequence()` 체크에 막혀 nullptr 반환 → 탭이 안 열리고 로그 한 줄로
  끝남
  ([MeshManager.cpp](KraftonEngine/Source/Engine/Asset/Import/MeshManager.cpp)
  의 `LoadAnimSequenceFromFile` 본문). **윈도우 다이얼로그는 안 뜨지만 "조용히
  실패"가 된다.**

**Y-2. fallback의 더블클릭에 헤더 검사 추가.**

베이스 `ContentBrowserElement::OnDoubleLeftClicked`을
([ContentBrowserElement.h:34](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.h:34))
변경: `.asset` 확장자면 헤더의 `AssetType`을 보고 적절한 `EditorEngine->Open*`을
호출하고, 그 외에는 기존 `ShellExecuteW` 폴백을 유지. 즉:

```cpp
// 의사 코드, 본 진단은 실제 작성하지 않음.
virtual void OnDoubleLeftClicked(ContentBrowserContext& Context) {
    if (ContentItem.Path.extension() == L".asset") {
        EAssetType Type; FString Path = FPaths::ToUtf8(ContentItem.Path.wstring());
        if (TryReadAssetType(Path, Type) && Type == EAssetType::AnimSequence) {
            Context.EditorEngine->OpenAnimSequenceAsset(Path);
            return;
        }
        // 다른 AssetType 분기들…
    }
    ShellExecuteW(nullptr, L"open", ContentItem.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
```

**트레이드오프 비교.**

| 항목 | Y-1 (분류에서 ImportedAnimSequenceElement 재사용) | Y-2 (fallback에 헤더 분기) |
| --- | --- | --- |
| 변경 범위 | 분류에 한 줄 분기 추가만. **가장 작다.** | 베이스 클래스 한 메서드 본문 변경. 모든 fallback Element에 영향. |
| 확장성 | 좋지 않음 — 다른 `AssetType`이 `.asset`을 쓰면 또 다른 Element 타입을 만들고 분류에도 분기를 또 추가해야 함. 결국 방식 X와 같은 구조로 수렴. | 모든 `EAssetType` 추가가 베이스 한 곳의 분기 확장으로 끝남. |
| 위험 | `.asset` AnimSequence가 아닌 파일을 더블클릭하면 "조용히 실패". | `ShellExecuteW` 폴백이 그대로 살아 있어, `.asset`이지만 헤더 미일치 또는 알 수 없는 타입은 다시 윈도우 다이얼로그가 뜬다 (현 동작과 동일 — 회귀 없음). |
| 의미상 일관성 | `ImportedAnimSequenceElement`는 본래 "FBX 안 가상 sub-asset"이므로 디스크 파일을 같은 타입으로 분류하면 의미가 약간 어긋남. 클래스명 재고려 가치 있음. | 베이스 클래스가 "기본 무동작"이 아니라 "기본 분기 처리"로 의미가 바뀐다. 다른 Element 타입과의 책임 분리에 약간의 변화. |
| 아이콘 / Drag&Drop | `GetElementIcon`은 `BuildAnimSequenceSnapshot`을 호출 ([ContentBrowserElement.cpp:566-571](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:566)). 이미 `.asset` 절대 경로로도 동작. **그대로 작동.** | fallback Element의 아이콘은 `FallBackIconPath` 그대로. AnimSequence 전용 아이콘 없음. UI 품질이 살짝 떨어진다. |

### 방식 X vs 방식 Y — 종합 비교

| 기준 | 방식 X (새 Element 타입) | 방식 Y-1 (재사용) | 방식 Y-2 (fallback 분기) |
| --- | --- | --- | --- |
| 변경 범위 | 중간 | **최소** | 작음 |
| 확장성 (다른 AssetType) | 우수 (타입별 클래스) | 나쁨 (반복 추가) | 우수 (한 곳에서 모두 분기) |
| 분류 단계 I/O 비용 | 있음(헤더 16B) — 또는 1차 확장자만 보고 2차에서 처리 | 없음 | 없음 (더블클릭 시점에 1회) |
| 아이콘 / Drag&Drop 일관성 | 우수 | 우수(`ImportedAnimSequence` 자산 재사용) | 약함(fallback 아이콘) |
| 조용한 실패 위험 | 낮음(헤더 검사 시) | **있음** (확장자만으로 분류) | 낮음(헤더 본 뒤 분기) |
| 의미상 일관성 | 우수 | 약함(원래 가상 sub-asset 클래스) | 베이스 책임 변경 |

본 진단은 **방식 X**(또는 변형: 분류는 확장자만 1차, 더블클릭 시 헤더 검사 후
분기하는 X-Y 하이브리드)을 **가장 깨끗**하다고 본다. 단, 단정하지는 않는다 —
"빠르게 한 줄로" 처리할 거면 Y-1이 비용 최소, 다중 `AssetType` 미래를 고려할
거면 X가 자연스럽다.

---

## 5. 범위 외 관찰 사항 (진단하지 않음, 기록만)

- 분류 if-else의 확장자 비교가 케이스 sensitive(`.Scene`, `.Prefab`, `.mat`,
  `.lua`, `.curve`, `.obj` 등 — `.fbx`/`.FBX`만 따로 처리되어 있음). 사용자가
  `.ASSET`처럼 대문자로 저장하면 또 한 단계 실패가 생긴다. 본 작업의 직접
  범위는 아니나, 변경 시 일관성을 위해 케이스 비교 헬퍼를 도입하는 편이 좋다.
- `ImportedStaticMeshElement` / `ImportedSkeletalMeshElement`의 더블클릭이
  무동작(`(void)Context;`)이다. 의도된 것인지, 추후 동작이 필요한지는 본 진단
  범위 외.
- 베이스 `ContentBrowserElement::OnDoubleLeftClicked`이 `ShellExecuteW`로
  **무조건** OS에 위임하는 동작은, `.asset` 외에도 `.obj` 등 OS가 인식 못 할
  수 있는 임의 확장자에서 같은 다이얼로그를 띄울 수 있다 — 즉 본 작업이 끝나도
  유사 회귀가 다른 확장자에서 나타날 여지가 남는다.

---

## 6. 다음 단계 구현 작업의 개략 분할

> 본 진단은 구현하지 않는다. 작업 분할만 제시한다.

| 순서 | 작업 | 비고 |
| --- | --- | --- |
| (a) | 방식 선택 (X / Y-1 / Y-2). | 정책 결정. 본 진단은 단정하지 않음. |
| (b) | (방식 X / Y-2 선택 시) 16바이트 헤더 검사 헬퍼 1개 추가. | `bool TryReadAssetType(const FString&, EAssetType&)`. 위치 후보: `Engine/Asset/AssetFileHeader.h` 옆 자유 함수 또는 `MeshManager`. |
| (c) | (방식 X) `AnimSequenceAssetElement` 클래스 추가 + 분류 분기 추가. <br/> (방식 Y-1) 분류에 `ImportedAnimSequenceElement` 분기 한 줄 추가. <br/> (방식 Y-2) 베이스 `OnDoubleLeftClicked` 본문 분기 확장. | 핵심 변경. |
| (d) | (방식 X) 아이콘은 `BuildAnimSequenceSnapshot`을 절대 경로로 호출(이미 동작). Drag&Drop type ID는 신설. | 부수 작업. |
| (e) | 검증: FBX에서 클립을 연다 → Notify 추가 → Save As → ContentBrowser에서 그 `.asset` 더블클릭 → 탭이 열리는지, 윈도우 다이얼로그가 더 이상 안 뜨는지 확인. | 본 진단의 검증 시나리오와 일치. |

### 확인 필요 / 정책 결정 필요 항목

- 분류 단계에서 헤더를 읽는 비용 — 측정 필요. 디렉토리당 `.asset` 개수가
  많지 않으면 무시 가능.
- 헤더 검사가 실패한 `.asset`(스튭이거나 손상)의 UX — 분류에서 fallback으로
  떨어뜨릴지, 별도 "Invalid asset" Element를 줄지.
- 확장자 비교의 대소문자 정책 — 본 작업과 함께 정리할지 별도 작업으로 미룰지.
- `OpenAnimSequenceAsset`이 절대 경로를 받았을 때
  `FAnimSequenceEditorTab::OpenAnimSequenceAsset`의 PreviewMesh 해석이 정상
  동작하는지의 실측. 코드 흐름상으로는 결정 4(A안,
  [animsequence_asset_saveas_design.md](Document/animsequence_asset_saveas_design.md))에
  따라 `SkeletonAssetPath`의 `#`를 기준으로 mesh를 찾으므로 절대/상대 무관하지만,
  실제 사용자 환경에서 한 번 확인 필요.
