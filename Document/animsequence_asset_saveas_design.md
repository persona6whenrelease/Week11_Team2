# AnimSequence `.asset` Save As 설계 진단

> 본 문서는 **진단 및 설계 제안**이다. 코드 변경을 포함하지 않으며,
> 5번 항목의 "런타임 클립 교체"에 대한 설계 제안 역시 텍스트일 뿐 실제
> 코드 작성을 포함하지 않는다. 모든 사실은 `feature/project` 브랜치
> (2026-05-19 시점) 작업 트리의 실제 소스를 직접 열어 확인했으며 `file:line`
> 으로 근거를 댄다. 미확인 항목은 명시적으로 "확인 필요"로 표시한다.
>
> 전제: 클립을 `.asset`으로 추출하는 방식은 **에디터의 명시적 Save As**다.
> FBX 임포트 직후 자동 추출이 아니다.

---

## 요약 (TL;DR)

| 항목 | 결론 |
| --- | --- |
| 1. Skeleton/Mesh 해석 | `SkeletonAssetPath` 자체가 `"<fbx>#SkeletonAsset_<id>"` 형식의 FBX 참조 문자열이므로 outer 없이도 mesh를 찾을 수 있다. 다만 `AnimSequenceEditorTab::OpenAnimSequenceAsset` 의 fallback이 outer/AssetPath에 묶여 있어 일부 수정이 필요하다. |
| 2. 수명 결합 | `UObjectManager`에는 GC가 없고 `DestroyObject` ≡ `delete`. `UAnimSequence`는 소멸자에서 `DataModel`을 정리하지 않아 **현재 구조에서 누수 가능성 있음**. `USkeletalMeshComponent::~USkeletalMeshComponent`의 패턴이 그대로 적용 가능. |
| 3. Save As 진입점 | `FWindowsBinWriter` 호출 패턴은 OBJ/FBX 캐시에 이미 확립되어 있음. 단일 파일 writer를 `MeshManager` 옆에 두는 자유 함수, UI 진입은 모드바 / Asset Info 패널 / 컨텍스트 메뉴 세 후보. |
| 4. 로드 분기 | `FMeshManager::ResolveAnimSequenceReference` 한 지점이 가장 깨끗한 분기 후보. 확장자 / 헤더 Magic 검사로 분기 가능. |
| 5. 런타임 클립 교체 | **이미 완전히 구현되어 있음** — `USkeletalMeshComponent::SetAnimation` / `PlayAnimation` / `UAnimSingleNodeInstance::SetAnimation` / `FAnimGraphNode_SequencePlayer::SetSequence`. AnimSequenceEditorTab/SkeletalMeshEditorTab/ContentBrowser가 이미 호출 중. SingleNode 모드 한정. |

전체 의존 순서: **3 = 1 (병행) → 4 → 2 (정책 결정)**. 5는 1-4에 의존하지만,
"UAnimSequence 객체를 어떤 경로로든 메모리에 얻기만 하면" 자동 동작하므로
독립 구현 작업이 거의 없다.

---

## 1. Skeleton/Mesh 탐색 경로

### 확인된 사실

**`SkeletonAssetPath` 멤버의 실제 포맷.**
`UAnimSequence::SkeletonAssetPath` 멤버는 `FString` 1개
([AnimSequence.h:115](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:115)).
이 문자열을 채우는 코드는 두 군데뿐이다.

- [FBXImporter.cpp:195](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXImporter.cpp:195) —
  FBX 임포트 시:
  ```cpp
  SkeletalMesh.SkeletonAssetPath = InFilePath + "#SkeletonAsset_" + std::to_string(SkeletonMeta.SkeletonId);
  ```
  여기서 `InFilePath`는 FBX 파일 경로(예: `Data/Foo.fbx`),
  `SkeletonMeta.SkeletonId`는 FBX 안의 스켈레톤 ID.
  최종 형식은 `"<fbx-path>#SkeletonAsset_<int>"`
  (예: `"Data/Foo.fbx#SkeletonAsset_0"`).
- [FBXImporter.cpp:209](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXImporter.cpp:209) —
  같은 임포트 루프에서 동일 문자열을 그 mesh의 모든 `UAnimSequence`에
  복사. 이후 [FBXManager.cpp:385](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:385)
  `CreateSceneAssetFromScene` 가 cache load 경로에서 한 번 더
  `AnimSequence->SetSkeletonAssetPath(MeshAsset->SkeletonAssetPath)` 로 재설정한다.

즉 `SkeletonAssetPath`는 **별도 경로가 아니라 FBX sub-asset 참조 문자열**
이다. `#SkeletonAsset_` 마커 앞이 FBX 파일 경로, 뒤가 정수 ID.

**현재 PreviewMesh 결정 로직의 outer 의존 지점.**
[AnimSequenceEditorTab.cpp:166-188](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:166)
가 3단계로 PreviewMesh를 찾는다:

```cpp
USkeletalMesh* ResolvedPreviewMesh = nullptr;
if (UFBXSceneAsset* SceneAsset = ResolvedSequence->GetTypedOuter<UFBXSceneAsset>())   // (1) outer
{
    ResolvedPreviewMesh = FMeshManager::FindSkeletalMeshForAnimSequence(SceneAsset, ResolvedSequence);
}
if (!ResolvedPreviewMesh)
{
    const size_t HashPos = AssetPath.find('#');                                        // (2) AssetPath의 '#'
    if (HashPos != FString::npos)
    {
        const FString FbxPath = AssetPath.substr(0, HashPos);
        if (UFBXSceneAsset* FallbackScene = FMeshManager::LoadFbxScene(FbxPath))
        {
            ResolvedPreviewMesh = FMeshManager::FindSkeletalMeshForAnimSequence(FallbackScene, ResolvedSequence);
            if (!ResolvedPreviewMesh && !FallbackScene->GetSkeletalMeshes().empty())
            {
                ResolvedPreviewMesh = FallbackScene->GetSkeletalMeshes()[0];           // (3) 첫 mesh fallback
            }
        }
    }
}
```

`.asset`에서 로드한 UAnimSequence는:
- (1) outer 미설정 (FBX 임포트가 아니므로 `UFBXSceneAsset`이 outer가 아님) → 실패.
- (2) AssetPath가 `Path/To/Foo.anim.asset` 처럼 `#`을 포함하지 않으므로 → 실패.
- (3)도 도달 불가.

**재사용 가능한 해석 함수 / mesh 매칭 헬퍼.**
- [MeshManager.cpp:208-226](KraftonEngine/Source/Engine/Asset/Import/MeshManager.cpp:208)
  `FMeshManager::FindSkeletalMeshForAnimSequence(SceneAsset, Sequence)` —
  주어진 SceneAsset 안에서 `SkeletonAssetPath` 가 일치하는 SkeletalMesh를
  찾는다. 이게 핵심 매칭 헬퍼다.
- [MeshManager.cpp:151-154](KraftonEngine/Source/Engine/Asset/Import/MeshManager.cpp:151)
  `FMeshManager::LoadFbxScene` — FBX 파일 경로로부터 (또는 메모리/디스크
  캐시로부터) `UFBXSceneAsset*`을 얻는다.
- [FBXManager.cpp:121-125](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:121)
  `IsFbxPath` — 확장자 비교 유틸.
- [FBXManager.cpp:133-158](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:133)
  `ParseFbxSceneSubAssetReference(Path, "#SkeletonAsset_", ...)` —
  `"<fbx>#SkeletonAsset_<id>"` 분해.

### 설계 선택지

`.asset` 경로에서도 PreviewMesh를 찾을 수 있게 하려면, **`SkeletonAssetPath`
자체를 ‘mesh 찾기 입력’으로 쓰면 된다.** 그 문자열이 이미
`"<fbx>#SkeletonAsset_<id>"` 형식이므로:

1. `SkeletonAssetPath` 안에서 `#SkeletonAsset_` 마커를 찾는다.
2. 앞 부분을 fbx 경로로 떼어 `FMeshManager::LoadFbxScene(FbxPath)` 호출.
3. 결과 `UFBXSceneAsset*`에 대해 `FindSkeletalMeshForAnimSequence` 호출.

이때 옵션:

| 옵션 | 트레이드오프 |
| --- | --- |
| **A.** `OpenAnimSequenceAsset`의 (2)번 fallback을 `AssetPath`의 `#`이 아니라 `ResolvedSequence->GetSkeletonAssetPath()`의 `#`을 보는 식으로 일반화. | 변경 최소(한 군데), 기존 FBX 가상 경로 케이스도 동작 유지(어차피 같은 문자열). 호출 흐름 영향 작음. |
| **B.** 별도 헬퍼(`FMeshManager::ResolvePreviewMeshForSequence(UAnimSequence*)`) 신설. | 재사용성 향상이지만, 호출처가 사실상 한 군데뿐(`AnimSequenceEditorTab`)이라 과한 추상화 위험. |
| **C.** `.asset` 로드 시점에 outer로 `UFBXSceneAsset`을 강제로 끼워 둔다. | 데이터 모델이 더 더러워진다. 게다가 `.asset`은 “FBX 없이도 단독 로드 가능”이 목적이므로 outer를 늘 끼우면 의미가 약해진다. **비추천.** |

A안 기준으로의 변경 지점은 `AnimSequenceEditorTab.cpp:175` 한 줄
(`AssetPath.find('#')` → `ResolvedSequence->GetSkeletonAssetPath().find('#')`).
이 변경은 본 진단의 범위 외이므로 실제 코드는 본 문서에서 작성하지 않는다.

### 미확인 항목

- `SkeletonAssetPath`가 비어 있는 `UAnimSequence`가 발생할 수 있는 경로
  (예: 손으로 만든 시퀀스, 일부 FBX 파싱 실패 등)는 검사하지 않았다. Save As
  시점에 비어 있으면 저장 자체는 가능하지만 다시 로드했을 때 PreviewMesh
  매칭이 불가능해진다 — Save As UI에서 사전 검증 정책 필요(설계 정책 결정).

---

## 2. UAnimSequence ↔ UAnimDataModel 수명 결합

### 확인된 사실

**UObjectManager 수명 모델.**
- [Object.cpp:9-14](KraftonEngine/Source/Engine/Object/Object.cpp:9) — `UObject` 기본
  생성자가 `GUObjectArray.push_back(this)` 와 `InternalIndex` 부여를 수행.
- [Object.cpp:16-28](KraftonEngine/Source/Engine/Object/Object.cpp:16) — 소멸자가
  swap-pop 방식으로 `GUObjectArray`에서 자기 슬롯을 제거. 부분 갱신 없이
  전체 풀에서 사라진다.
- [Object.h:135-142](KraftonEngine/Source/Engine/Object/Object.h:135) —
  `UObjectManager::DestroyObject(Obj)` 본문이 `delete Obj;` 하나뿐. **GC 없음.**
  `delete`가 소멸자를 호출 → 위 swap-pop이 일어남.
- [Object.h:46](KraftonEngine/Source/Engine/Object/Object.h:46) 주석 — “Outer는
  논리적 스코프이며 **소유 의미가 아니다**. 직렬화에서도 제외.”
- [Object.h:110-113](KraftonEngine/Source/Engine/Object/Object.h:110)
  `IsAliveObject(Object)` — `GUObjectArray`에 해당 포인터가 있는지 선형 검색.
  ref 유효성 폴링용으로만 의미가 있다.

**`UAnimSequence`/`UAnimDataModel`의 소유 관계 (확인됨).**
- [AnimSequence.h:82](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:82)
  `UAnimDataModel* DataModel = nullptr;` — raw 포인터, 직접 소유 의미는
  코드상 명시되어 있지 않다.
- [AnimSequence.h:88-117](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:88)
  `UAnimSequence`에 사용자 정의 소멸자가 **없다** (`DECLARE_CLASS`의 기본
  vtbl만 사용). 즉 `delete UAnimSequence`가 일어나도 `DataModel`을
  `DestroyObject` 해 주는 코드 경로는 없다.
- DataModel 생성 위치는 두 곳:
  - [FbxAnimationParser.cpp:152](KraftonEngine/Source/Engine/Asset/Import/FBX/Parser/FbxAnimationParser.cpp:152)
    임포트 경로 — outer 인자로 `Sequence`를 줌.
  - [AnimSequence.cpp:52](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:52)
    역직렬화 lazy 생성 — outer 인자로 `this`를 줌.

**짝 객체 자동 정리의 선례.**
[SkeletalMeshComponent.cpp:27-34](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:27)
이 코드베이스에서 발견된 거의 유일한 “짝 UObject를 destroy하는” 패턴:

```cpp
USkeletalMeshComponent::~USkeletalMeshComponent()
{
    if (AnimInstance)
    {
        UObjectManager::Get().DestroyObject(AnimInstance);
        AnimInstance = nullptr;
    }
}
```

`DestroyObject` 호출처 grep
([Editor/.../*, Engine/.../*, Games/.../* 분포 — Grep "DestroyObject" 결과 참조])
를 보면 대부분 World/Actor/Component/Material 등 명백한 owning context에서만
정리되고, 에셋류는 거의 정리되지 않는다.

### 누수 가능성 판정

현재 코드 흐름상 `UAnimSequence`가 실제로 `DestroyObject`되는 경로는
**전체 코드베이스에 존재하지 않는다** (`DestroyObject(...AnimSequence...)`
grep 결과 0건). 따라서 “지금 당장 누수 중”이라고 단정할 수는 없다.
다만 `.asset` 도입 후 “Save As 후 새 시퀀스 열기”와 같은 시퀀스 교체
시나리오가 늘어나면 **곧 누수 노출**된다.

`.asset`을 반복 로드할 때의 시나리오:
1. 같은 `.asset`을 두 번 `OpenAnimSequenceAsset` → `ResolveAnimSequenceReference`
   가 매번 새 `UAnimSequence` + 새 `UAnimDataModel`을 만들지 (메모리 캐시
   적중하지 않는 한) `GUObjectArray`가 무한 증가.
2. (1)을 막으려면 `.asset` 측에도 메모리 캐시(예: `TMap<NormalizedPath,
   UAnimSequence*>`)가 필요. 이건 FBX 측이 이미
   [FBXManager.cpp:31](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:31)
   `FbxSceneCache`로 하고 있는 것과 같은 패턴.

### Destroy 정책 옵션 (어느 하나로 단정하지 않음)

| 옵션 | 의미 | 트레이드오프 |
| --- | --- | --- |
| **D1.** `UAnimSequence` 소멸자에서 `DataModel`을 destroy. | `SkeletalMeshComponent` 패턴 그대로 적용. | 단순/일관. 단 외부에서 `DataModel`을 share-소유 패턴으로 쓸 가능성은 코드상 발견되지 않음 → 안전. |
| **D2.** 명시적 `UAnimSequence::Destroy()` 호출자 책임 정책. | 소유권을 호출자에 위임. | 호출자 누락 시 누수. 현재 엔진 관습과 어긋남. |
| **D3.** `.asset` 단위 메모리 캐시 + 캐시 라이프타임이 곧 객체 라이프타임. | FBX scene cache와 동일 패턴 — 거의 영구. | 누수 회피라기보다 “생애 끝까지 안 지움” 전략. ReleaseAllGPU 등 종료 경로에서 일괄 처리 필요. FBX 측 [FBXManager.cpp:692](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:692) `ReleaseAllGPU()`가 `FbxSceneCache.clear()` 한 줄로 처리 — UObject 자체는 해제 안 됨. **본 진단 범위 외이지만, 동일한 문제가 이미 FBX 쪽에도 존재한다.** |
| **D4.** 단계적: 우선 D3로 가고, GC 도입 시 D1으로 전환. | 가장 점진적. | 단기적 비용이 작지만 누수 경향이 누적될 수 있음. |

본 진단은 정책을 단정하지 않는다. 다만 D1은 외부 의존이 거의 없어 가장
저비용이고, `SkeletalMeshComponent`의 선례가 있어 자연스럽다.

### 미확인 항목

- `UAnimSequence`를 share-소유 형태로 보관하는 호출처 (`std::shared_ptr`,
  ref-count, weak_ptr 등) 존재 여부 — grep상으로는 발견되지 않았으나
  포괄적 verification은 하지 않았다.
- `UObjectManager`의 미래 GC 도입 계획은 본 진단 범위 외.

---

## 3. Save As 진입점 설계

### 확인된 사실

**`FWindowsBinWriter` 호출 패턴 (3 곳, 모두 거의 동일).**
- [ObjManager.cpp:146](KraftonEngine/Source/Engine/Asset/Import/OBJ/ObjManager.cpp:146)
  / [ObjManager.cpp:234](KraftonEngine/Source/Engine/Asset/Import/OBJ/ObjManager.cpp:234)
  — 스택에 인스턴스 생성, `Writer.IsValid()` 분기, `StaticMesh->Serialize(Writer)`
  호출. 함수 본문 안에 직접 inline.
- [FBXManager.cpp:271-289](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:271)
  `SaveSceneToCache` — 익명 namespace의 자유 함수. `FAssetFileHeader`가
  아닌 자체 `FFBXSceneCacheHeader`를 먼저 쓰고 본문 Serialize.
- 공통 패턴:
  ```cpp
  FWindowsBinWriter Writer(CachePath);
  if (!Writer.IsValid()) { /* 로그 + 조기 종료 */ }
  // (optional) header serialize
  Obj->Serialize(Writer);
  ```

`FWindowsBinWriter` 자체:
[WindowsArchive.h:10-44](KraftonEngine/Source/Engine/Serialization/WindowsArchive.h:10) —
ctor에서 `FPaths::TryResolvePackagePath`로 경로 해석, `std::ofstream`을
`std::ios::binary`로 연다. 실패 시 stream이 비어 있고 `IsValid()` 가
false.

**AnimSequence 단일 파일 writer를 두기 적합한 위치.**

`SaveSceneToCache`가 FBXManager 내부에서 익명 namespace의 자유 함수로 살아
있는 패턴이 가장 가깝다. AnimSequence는 FBXManager에 종속될 필요가 없으므로
다음 두 후보가 자연스럽다.

| 후보 | 위치 / 형태 | 장점 | 단점 |
| --- | --- | --- | --- |
| **3-A.** `FMeshManager`에 `SaveAnimSequenceToFile / LoadAnimSequenceFromFile` 정적 자유 함수 추가. | `MeshManager.cpp`. 시그니처: `bool SaveAnimSequenceToFile(const UAnimSequence*, const FString& Path)`. | 기존 `LoadSkeletalMesh / ResolveAnimSequenceReference` 등과 같은 facade에 묶여 호출처에서 일관됨. 추가 헤더/매니저 신설 없이 끝남. | `FMeshManager` 가 점점 무거워짐. |
| **3-B.** `FAnimSequenceSaveManager` (또는 `FAnimSequenceFileIO`) 신설. | `Engine/Asset/Animation/Core/AnimSequenceFileIO.{h,cpp}` 신설. | 책임 분리. AnimSequence 전용 캐시(메모리 맵)까지 함께 둘 수 있음. | 신설 비용. 메모리 캐시까지 두려면 별도 정책 결정 필요. |

본문은 어느 쪽이든 거의 동일하다 (의사 코드, **실제 작성하지 않음**):

```
bool SaveAnimSequenceToFile(const UAnimSequence* Seq, const FString& Path)
{
    if (!Seq) return false;
    FWindowsBinWriter Writer(Path);
    if (!Writer.IsValid()) { UE_LOG(...); return false; }
    const_cast<UAnimSequence*>(Seq)->Serialize(Writer);  // FAssetFileHeader는 UAnimSequence::Serialize가 직접 씀
    return true;
}
```

`UAnimSequence::Serialize`는 헤더 작성 + 본문 작성을 모두 자체 처리하므로
([AnimSequence.cpp:28-60](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:28))
바깥에서 별도의 magic 작성이 필요하지 않다. `FObjectFileHeader.IsValid`
검증도 `UAnimSequence::Serialize` 안에서 처리됨 (load 분기 시).

**에디터 Save As UI 진입 후보.**

`FAnimSequenceEditorTab`이 다음 패널을 보유한다:
- [AnimSequenceEditorTab.cpp:298-328](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:298)
  `RenderTabContent` — 3-column 레이아웃. 최상단에 `RenderTabModeBar()`.
- [SkeletalEditorTab.cpp:399-441](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorTab.cpp:399)
  `FSkeletalEditorTab::RenderTabModeBar` — 우측 정렬로 두 개 아이콘
  (SkeletalMesh / AnimSequence) 만 있는 작은 토글바. 슬롯 여유 있음.
- [AnimSequenceEditorTab.cpp:852-913](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:852)
  `RenderLeftPanel` — “Asset / Skeleton Tree” 좌측 패널. 시퀀스 정보(Name,
  Duration, FrameRate 등)를 텍스트로 표시. 버튼을 두기에 자연스러운 위치.
- [AnimSequenceEditorTab.cpp:629-652](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:629)
  timeline 컨텍스트 메뉴 — 현재 “Delete Notify / Add Notify at current frame”.
  `Save As...` 항목을 노티파이 메뉴와 섞기에는 의미가 어긋남 — **비추천.**

| 후보 | 위치 | 장점 | 단점 |
| --- | --- | --- | --- |
| **3-C.** 모드바([SkeletalEditorTab.cpp:399-441](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalEditorTab.cpp:399))에 작은 “Save” 아이콘 추가. | 우상단 toggle bar. | 항상 보임. 한 클릭. UE Persona 패턴. | 모드바가 “모드 전환”의미였는데 액션이 섞임. |
| **3-D.** 좌측 Asset Info 패널 ([AnimSequenceEditorTab.cpp:867-873](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:867))에 “Save As...” 버튼. | 시퀀스 메타정보 옆. | 의미적으로 가장 잘 맞음. 변경 영향 좁음. | 클릭 한 번 더 필요(좌패널 활성 상태일 때). |
| **3-E.** ImGui 메인 메뉴바(파일 메뉴 등)에 “Save Anim Sequence As…” 추가. | 별도 (현재 코드 범위 외). | 글로벌 단축키 가능. | UI 위치가 분산됨. |

본 진단은 3-D를 가장 자연스러운 후보로 본다. 다만 정책 결정의 영역이므로
단정하지 않는다.

**Notify 편집 상태와 Save As의 연결 지점.**

Notify 추가/삭제/수정은 `AnimSequenceDataSource`를 통해
`UAnimSequence::Notifies`를 직접 수정한다 (서베이 문서
[2-4 절](Document/animsequence_asset_serialization_survey.md) 인용에 따르면
`AnimSequenceDataSource.cpp:49-98`에 위치 — 본 진단에서는 행 번호만 재확인하지
않았으나 grep으로 `AddNotify/RemoveNotify/UpdateNotify` 호출이
[AnimSequenceEditorTab.cpp:637, 649, 841](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:637)
에 있음을 확인했다 — DataSource 인터페이스를 통해 UAnimSequence 객체를 직접
수정). 즉 “Save As” 시점에는 **이미 메모리상의 UAnimSequence가 최신
편집 상태**이므로 그대로 `UAnimSequence::Serialize`에 흘리면 된다.

별도의 “더티 추적”은 필수가 아니지만, 정책으로 둘 수 있다 (예: dirty flag
true일 때만 모드바 아이콘 색을 바꾸는 식). 본 진단은 그 정책을 단정하지
않는다.

### 미확인 항목

- 파일 경로 선택 UI (네이티브 파일 다이얼로그) — 현재 엔진의 file picker
  유틸 부재 여부 미확인. 단순 “Save Anim Sequence as <stem>.asset” 식의
  자동 경로 결정으로 시작하는 것도 옵션.
- 같은 경로에 이미 파일이 있을 때 덮어쓰기 / 백업 정책 — 정책 결정 필요.

---

## 4. 로드 경로 분기

### 확인된 사실

**현재 입력 형식.**
[FBXManager.cpp:160-200](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:160)
`ParseFbxSceneAnimSequenceReference` — `<fbx>#Anim_<SkeletonId>_<AnimIndex>` 분해.
[FBXManager.cpp:600-648](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:600)
`ResolveAnimSequenceReference` — 이 가상 참조 문자열을 받아 SceneAsset
로드 → `SkeletonAssetPath` 매칭 → `MatchIndex == AnimIndex` 순서로 반환.

**호출 체인 (위에서 아래로).**
1. ContentBrowser 더블클릭:
   [ContentBrowserElement.cpp:573-581](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:573)
   `ImportedAnimSequenceElement::OnDoubleLeftClicked` →
   `EditorEngine->OpenAnimSequenceAsset(<path>)`.
2. [EditorEngine.h:59](KraftonEngine/Source/Editor/EditorEngine.h:59) →
   `MainPanel.OpenAnimSequenceAsset`.
3. [EditorMainPanel.cpp:125-128](KraftonEngine/Source/Editor/UI/EditorMainPanel.cpp:125) →
   `SkeletalMeshViewerWidget.OpenAnimSequenceAsset`.
4. [EditorSkeletalMeshViewerWidget.cpp:103-141](KraftonEngine/Source/Editor/UI/EditorSkeletalMeshViewerWidget.cpp:103) —
   탭 생성 → `NewTab->OpenAnimSequenceAsset(AssetPath)`.
5. [AnimSequenceEditorTab.cpp:152-191](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:152) —
   `FMeshManager::ResolveAnimSequenceReference(AssetPath)` 호출.
6. [MeshManager.cpp:146-149](KraftonEngine/Source/Engine/Asset/Import/MeshManager.cpp:146) —
   `FFBXManager::ResolveAnimSequenceReference`로 위임.

`.asset` 경로는 5단계에서 `ResolveAnimSequenceReference` 가 nullptr을 반환하므로
6단계까지 도달하지 못하고 탭이 열리지 않는다 (현재 상태).

**확장자 / 형식 판별 유틸의 현재 위치.**
- [FBXManager.cpp:121-131](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:121)
  `IsFbxPath`, `IsBinPath` — 익명 namespace의 자유 함수, `std::filesystem`
  + `ToLower` 기반 확장자 검사.
- [MeshManager.cpp:84-92](KraftonEngine/Source/Engine/Asset/Import/MeshManager.cpp:84)
  `FMeshManager::IsFbxStaticMeshReference / IsFbxSkeletalMeshReference` —
  `#Mesh_` / `#Skeleton_` 마커 검사. 가상 참조 문자열용.
- [AssetFileHeader.h:17-39](KraftonEngine/Source/Engine/Asset/AssetFileHeader.h:17)
  `FAssetFileHeader` — Magic / AssetType / Version. 검증은
  `IsValid(EAssetType, Version)`.

### 분기 후보 지점

| 후보 | 위치 | 분기 방식 | 트레이드오프 |
| --- | --- | --- | --- |
| **4-A.** `FMeshManager::ResolveAnimSequenceReference` ([MeshManager.cpp:146-149](KraftonEngine/Source/Engine/Asset/Import/MeshManager.cpp:146)) 에 분기 한 줄 추가. | 1차 facade. | 입력 path에 `#Anim_`이 있으면 기존 경로, 아니면 새 `.asset` 로더로. | 호출처는 그대로. 모든 callers (ContentBrowser/EditorTab 등)가 자동으로 새 경로 지원. **가장 깨끗.** |
| **4-B.** `FAnimSequenceEditorTab::OpenAnimSequenceAsset` ([AnimSequenceEditorTab.cpp:152](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:152)) 진입에서 분기. | UI 진입점. | `ResolveAnimSequenceReference` 호출 전에 확장자 검사. | 분기 로직이 UI 계층에 새는 점이 아쉬움. 한 곳에만 분기를 추가하지만 다른 호출처(EvaluateAnimationPose 등)에서 같은 분기 필요해지면 중복 발생. |
| **4-C.** ContentBrowser 측에 `.asset` 전용 새 Element 타입 추가. | [ContentBrowserElement.cpp](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp) `ImportedAnimSequenceElement` 옆에 `AnimSequenceAssetElement` 신설. | 더블클릭 핸들러에서 바로 새 로더 호출. | UI 계층의 책임이 늘지만 ContentBrowser는 이미 다수 Element 타입을 보유 ([ContentBrowserElement.cpp:566, 583](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:566) 등). 의미적으로 무난. |

**분기 키:**
- (i) 확장자 검사: `IsFbxPath`와 같은 패턴으로 `IsAnimSequenceAssetPath`
  (예: `.asset` 또는 `.anm` 또는 협의된 확장자). 가장 단순.
- (ii) 마커 검사: `#Anim_` 포함 여부. FBX 가상 참조와 명확히 다른 키.
- (iii) 파일 매직 검사: 파일을 열어 `FAssetFileHeader.Magic == 'ASET'` 검사.
  가장 견고하지만 I/O 비용. UI에서 단순 분기에 쓰기엔 과함.

(i)+(ii)의 조합 (FBX 가상 참조면 기존 경로, 아니면 새 경로)을 권장한다.
파일 마법수 검사(iii)는 로드 단계에서 `UAnimSequence::Serialize`가 이미
검증하므로 추가 검사가 불필요.

### 미확인 항목

- `.asset` 확장자 vs 별도 확장자(`.anim`, `.anm` 등) 선택 — 정책. 단,
  `EAssetType` ([AssetTypes.h:16-26](KraftonEngine/Source/Engine/Asset/AssetTypes.h:16))이
  여러 타입을 한 enum으로 묶고 있어, 같은 `.asset` 확장자를 공유한 채
  본문 헤더의 `AssetType`으로 구분하는 설계도 자연스럽다. (현재 OBJ 캐시는
  `.bin`, FBX 캐시는 `.fbxscene.bin`을 쓰는 등 확장자가 통일되어 있지 않다 —
  본 진단 범위 외이나 기록.)

---

## 5. 런타임 클립 교체 경로 (조사 결과: 이미 구현됨)

### 확인된 사실 — 이미 존재하는 API

**컴포넌트 레벨 진입점 (2종).**
- [SkeletalMeshComponent.h:30](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:30) /
  [SkeletalMeshComponent.cpp:52-68](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:52)
  ```cpp
  void USkeletalMeshComponent::PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping = true)
  ```
  본문: `AnimToPlay = ...; EnsureAnimInstance(); SetAnimation(AnimToPlay);
  AnimInstance->SetLooping(bLooping); AnimInstance->ResetTime();
  AnimInstance->SetPaused(false);` + `bBakedAnimLooping/BakedAnimTime/bBakedAnimPaused`
  미러 동기화.
- [SkeletalMeshComponent.h:31](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h:31) /
  [SkeletalMeshComponent.cpp:70-80](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:70)
  ```cpp
  void USkeletalMeshComponent::SetAnimation(UAnimationAsset* NewAnimToPlay)
  ```
  본문: `AnimToPlay = ...; EnsureAnimInstance();` → SingleNode 분기에서
  `Single->SetAnimation(Cast<UAnimSequence>(NewAnimToPlay))`. 시간/일시정지는
  **건드리지 않음** — 순수 클립 교체.

**AnimInstance 레벨 진입점 (SingleNode 한정).**
- [AnimSingleNodeInstance.h:23](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.h:23) /
  [AnimSingleNodeInstance.cpp:12-17](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:12)
  ```cpp
  void UAnimSingleNodeInstance::SetAnimation(UAnimSequence* InSequence)
  {
      CurrentSequence = InSequence;
      SequencePlayer.SetSequence(Skeleton, CurrentSequence);
      ResetTime();
  }
  ```
  **호출 전제 / 부작용:**
  - `CurrentSequence`는 **ref 보관, 소유 아님** ([AnimSingleNodeInstance.h:34](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.h:34) 주석).
  - `ResetTime()` 이 항상 호출되어 `PreviousTime = CurrentTime = 0.0f`
    ([AnimInstance.h:52](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimInstance.h:52)). 즉 클립 교체 시 시간 보존은 불가.
  - `bPaused` 는 변경되지 않음 (이전 상태 유지).
  - `bLooping`도 변경되지 않음.
  - `Skeleton`이 nullptr이면 SequencePlayer 캐시는 비고 평가 시 bind pose
    ([AnimGraph.cpp:30-33](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp:30)).
    이 경우를 위해 [AnimSingleNodeInstance.cpp:19-27](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:19)
    `InitializeAnimation`이 늦게 들어온 스켈레톤으로 다시 `SetSequence` 호출.

**노드 레벨 캐시 빌드.**
- [AnimGraph.h:56](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.h:56) /
  [AnimGraph.cpp:23-42](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimGraph.cpp:23)
  ```cpp
  void FAnimGraphNode_SequencePlayer::SetSequence(const USkeleton* InSkeleton, const UAnimSequence* InSequence)
  ```
  본문: `Sequence/DataModel` 캐시, `TrackToBoneIndex` 재빌드. Skeleton의
  본 이름과 시퀀스 트랙 이름 매칭이 매번 일어남.

### 현재 호출처 (이미 사용 중인 곳)

| 위치 | 호출 형태 | 맥락 |
| --- | --- | --- |
| [AnimSequenceEditorTab.cpp:221-223](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:221) | `if (Comp->GetAnimation() != InSequence) Comp->SetAnimation(InSequence);` | Anim 탭 열기/시퀀스 변경. 시퀀스가 같으면 호출 회피 (시간 리셋 방지). |
| [AnimSequenceEditorTab.cpp:286-289](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:286) | `if (AnimSequence && Comp->GetAnimation() != AnimSequence) Comp->SetAnimation(AnimSequence);` | `SyncPlaybackToComponent` (매 프레임). |
| [SkeletalMeshEditorTab.cpp:254](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.cpp:254) | `PreviewMeshComponent->SetAnimation(CurrentSequence);` | SkeletalMesh 탭에서 현재 선택 시퀀스 적용. |
| [SkeletalMeshEditorTab.cpp:279](KraftonEngine/Source/Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.cpp:279) | `PreviewMeshComponent->SetAnimation(Sequence);` | 시퀀스 콤보박스 선택 변경. |
| [ContentBrowserElement.cpp:192](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:192) | `MeshComponent->SetAnimation(Sequence);` | (확인 필요: 정확한 액션) — drag-drop/적용 추정. |
| [SkeletalMeshComponent.cpp:122](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp:122) | `Single->SetAnimation(const_cast<UAnimSequence*>(Sequence));` | `EvaluateAnimationPose` 내부 — 외부 임의 시점 평가용. |

즉 **에디터에서 mesh를 열어 둔 채 재생할 클립을 런타임에 갈아끼우는 동작은
현재도 동작한다** — Anim 탭의 우하단 Asset Browser에서 시퀀스를 더블클릭하면
같은 탭이 `OpenAnimSequenceAsset` 재호출로 시퀀스만 교체하며, 그 안에서
`Comp->SetAnimation`이 일어난다.

### 새 “.asset 클립으로 교체” 시나리오와의 관계

런타임 클립 교체 자체는 `UAnimSequence*` 한 개만 있으면 동작한다. 즉:

- 1, 4번이 먼저 구현되어 “`.asset`을 로드해 `UAnimSequence*` 를 얻는 경로”가
  완성되면, 5번은 **추가 구현 없이** 자동으로 따라온다.
- 5번을 위해 별도 코드는 필요하지 않다.

### 제약 / 미확인 항목 / 설계 제안 (코드 없음)

**제약 (확인됨).**
- 위 경로는 **AnimationSingleNode 모드 전용**. `UAnimStateMachineInstance`
  ([AnimStateMachineInstance.h:18-39](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.h:18))는
  `SetAnimation` / `SetSequence` 류 API를 노출하지 않는다. 상태머신은
  `SetStateMachineGraph` ([AnimStateMachineInstance.cpp:11-36](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimStateMachineInstance.cpp:11))
  로 그래프 전체를 통째 교체한다 — 클립 단위 교체와는 다른 단위.
- 클립 교체 시 시간이 0으로 리셋된다 ([AnimSingleNodeInstance.cpp:16](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp:16)).
  Anim 탭이 “같은 시퀀스면 호출하지 않는” 가드를 둔 이유가 이것이다
  ([AnimSequenceEditorTab.cpp:218-224 주석](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:218)).

**제약 (확인 필요).**
- `CurrentSequence`가 ref-only 보관이므로, 교체 후 이전 `UAnimSequence*`
  를 해제했을 때 dangling 가능성 — 위 2번 진단의 destroy 정책에 의존.

**설계 제안 (코드 작성하지 않음, 텍스트뿐).**

만약 클립 교체 시 “시간 보존” 또는 “페이드” 가 필요해진다면 — 현재 코드엔
없음 — 다음과 같은 형태의 확장이 자연스럽다. 어디까지나 제안이며 본 진단은
실제 구현을 하지 않는다.

- **위치:** [SkeletalMeshComponent.cpp](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp)
  의 `SetAnimation` 옆에 형제 함수 1개. 또는
  [AnimSingleNodeInstance.cpp](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSingleNodeInstance.cpp)
  의 `SetAnimation`에 옵션 인자 추가.
- **시그니처 안:**
  - `void SwapAnimationPreservingTime(UAnimationAsset* NewAnim);`
    (`UAnimSingleNodeInstance::SetAnimation`에 `bResetTime` 인자를 추가하는
    오버로드/확장)
  - `void SetAnimationWithOptions(UAnimationAsset* NewAnim, bool bResetTime, bool bClearPaused);`
- **기존 멤버와의 상호작용:**
  - `CurrentSequence`만 갈아 끼우고 `SequencePlayer.SetSequence(Skeleton, CurrentSequence)`
    호출은 그대로 (TrackToBoneIndex 캐시는 새 스켈레톤 매칭이 필요하므로
    반드시 재빌드해야 함). 시간만 보존.
  - `bPaused`는 호출자 정책에 위임. 현재 `PlayAnimation`은 false로 강제하고,
    `SetAnimation`은 손대지 않는다. 새 API에서 “옵션 인자”로 노출하는 것이
    가장 호환적.
- **`bLooping` / `bPaused` 초기화 정책:** 본 진단에서 단정하지 않는다. 단,
  현재 `SetAnimation`이 이 둘을 건드리지 않는다는 시맨틱은 보존하는 것이
  Anim 탭의 “Loop:On/Off” 토글 ([AnimSequenceEditorTab.cpp:754-757](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:754))
  과 충돌하지 않는다.

**다시 강조: 본 진단은 위 “설계 제안” 항목에 대한 실제 코드 작성을 포함하지
않는다.** 현재 5번 항목의 결론은 **“추가 구현 없이도 동작한다”** 이며,
시간 보존 등의 확장은 본 작업 범위 외다.

### 1-4번과의 의존 순서

- 5번은 **`UAnimSequence*` 한 개를 얻는 것** 외엔 1-4번에 의존하지 않는다.
- `.asset` 클립으로 mesh를 런타임 교체하려면:
  1. 4번이 먼저 — `.asset`을 메모리에 `UAnimSequence*` 로 로드할 수 있어야 한다.
  2. 1번이 함께 — 로드된 시퀀스에 맞는 PreviewMesh를 찾을 수 있어야 한다
     (탭이 mesh를 못 잡으면 비주얼적으로 결과가 안 보임).
  3. 2번은 “반복 교체로 누수가 나지 않는가”의 측면에서만 5번에 영향. 한 번만
     하는 시연이라면 2번 없이도 동작.

---

## 종합: 5개 항목의 의존 순서와 다음 단계 작업 분할

### 의존 그래프

```
[3 Save As writer]  ─┐
                     ├─→  [.asset 파일이 디스크에 존재]
[1 Skeleton 해석]   ─┘                ↓
                                      [4 Load 분기]  ──→  [메모리상 UAnimSequence* 확보]
                                                            ↓
                                                       [5 런타임 교체]  (추가 코드 없이 동작)
                                                            ↓
                                                       반복 사용 시 누수
                                                            ↓
                                                       [2 수명 정책]
```

### 권장 구현 순서 (구현은 본 작업 범위 외 — 분할만 제시)

| 순서 | 항목 | 작업 | 비고 |
| --- | --- | --- | --- |
| (a) | 3 | 단일 파일 writer 자유 함수 (Save side). | `UAnimSequence::Serialize`는 이미 완성 — writer 한 함수만 추가. |
| (b) | 1 | `AnimSequenceEditorTab::OpenAnimSequenceAsset`의 fallback이 `SkeletonAssetPath`의 `#`를 기준으로 mesh 찾도록 일반화. | `.asset` 로드 후 PreviewMesh가 잡힘. |
| (c) | 4 | `FMeshManager::ResolveAnimSequenceReference`에 확장자 분기 + 새 reader. | 새 reader는 (a)의 writer 거울. `UAnimSequence::Serialize` 재사용. |
| (d) | 3 (UI 측) | 에디터에 Save As 진입 UI (3-C/3-D/3-E 중 선택). | (a)가 끝났다면 호출만 하면 됨. |
| (e) | 2 | UAnimSequence 소멸 시 DataModel 정리 정책 (D1 권장) + `.asset` 메모리 캐시 정책. | 누수 방지. |
| (f) | 5 | **추가 구현 없음.** 위 (a)-(e)가 끝나면 자동 동작. 시간 보존 등의 확장이 필요하면 별도 작업. | |

### 확인 필요 / 정책 결정이 필요한 항목

- `.asset` 파일 확장자 (`.asset` / `.anim` / `.anm` 등) — 본 진단에서 단정하지
  않음.
- Save As 시 PreviewMesh 의존성 검증 정책 (SkeletonAssetPath가 비어 있으면
  로드 실패 가능 — 사전 차단 vs 경고 후 진행).
- `.asset` 메모리 캐시 정책 (FBX scene cache처럼 영구 보관 vs LRU vs 없음).
- UAnimSequence 소멸 시 DataModel 정리 (D1/D2/D3 중 선택).

### 범위 외 관찰 사항 (진단하지 않음, 기록만)

- [FBXManager.cpp:692](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:692)
  `ReleaseAllGPU` 가 `FbxSceneCache.clear()` 한 줄로, 안에 들어있던
  `UFBXSceneAsset*` / 자식 mesh/skeleton/anim 들이 `DestroyObject`되지 않음.
  본 문서의 진단 2번과 같은 패턴의 누수가 FBX 측에도 있다 — 본 작업 범위 외.
- `FAnimNotifyEvent::IsTriggeredBetween` 호출처 (런타임 dispatch 측) 미추적 —
  본 작업 범위 외.
- `FAnimationCurveData::operator<<` 가 빈 placeholder
  ([AnimationTypes.h](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimationTypes.h)
  — 본 진단에서 행 번호 재확인 안 함, 서베이 문서 참고) — 향후 확장 시
  `.asset` 버전 bump 필요.
