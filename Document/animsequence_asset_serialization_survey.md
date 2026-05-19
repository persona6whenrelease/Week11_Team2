# AnimSequence Asset 직렬화 인프라 조사

> 본 문서는 **조사 및 진단**이며 코드 변경을 포함하지 않는다. 모든 항목은
> 2026-05-19 기준 현재 작업 트리(브랜치 `feature/project`)의 소스를
> 직접 읽어 확인했다. 확인하지 못한 부분은 `확인 필요`로 표시한다.

## 요약

- **범용 직렬화 프레임워크 자체는 이미 존재**한다.
  `FArchive` 베이스, `FWindowsBinReader/Writer`, `FMemoryArchive`,
  공통 헤더 `FAssetFileHeader`(Magic+Type+Version), `EAssetType` 열거형이
  갖춰져 있고 StaticMesh / SkeletalMesh / Skeleton / Material / AnimSequence /
  AnimDataModel / FBXSceneAsset 등 다수 타입이 이 위에서 `Serialize(FArchive&)`를 구현하고 있다.
- **UAnimSequence / UAnimDataModel의 직렬화 코드도 이미 완성**되어
  bone track, Notify(`FAnimNotifyEvent::operator<<`)까지 라운드트립 가능하다.
  단, 현재 이 코드가 호출되는 경로는 **단 하나뿐이며 FBX 씬 캐시
  (`Asset/FBXSceneCache/<stem>.fbxscene.bin`)** 안에 묻혀 있다. 독립적인
  `.asset` 파일로 AnimSequence 한 개를 따로 저장/로드하는 경로는 부재한다.
- **bone track 데이터는 `UAnimDataModel`이 값으로 완전 소유**한다
  (`TArray<FBoneAnimationTrack> BoneAnimationTracks`). 다만 이 데이터를
  **생성하는 경로는 FBX import 단 한 곳뿐**이며
  (`FbxAnimationParser.cpp:222`), 코드로 직접 트랙을 주입하는 별도 경로는
  발견되지 않았다. 메모리상에서는 FBX SDK 재진입 없이도 캐시 역직렬화만으로
  복원된다.

---

## 1. 범용 Asset Save/Load 시스템

엔진에 직렬화 베이스 / 백엔드 / 공통 헤더 / 타입 식별자가 모두 존재한다.
한편 `.asset` 또는 `.uasset` 단일 파일 단위로 임의 UObject를 저장/조회하는
**상위 레지스트리는 없고**, 각 매니저(`FFBXManager`, `FObjManager` 등)가
자기 캐시 디렉토리에 자기 포맷을 직접 관리한다.

### 1-1. 베이스 / 백엔드

| 위치 | 역할 |
| --- | --- |
| [Archive.h:9](KraftonEngine/Source/Engine/Serialization/Archive.h:9) `class FArchive` | 순수 가상 `Serialize(void*, size_t)`, `IsLoading/IsSaving`, 기본 `operator<<` 템플릿 |
| [Archive.h:38](KraftonEngine/Source/Engine/Serialization/Archive.h:38) `operator<<(FArchive&, std::string&)` | 문자열 길이+payload |
| [Archive.h:50](KraftonEngine/Source/Engine/Serialization/Archive.h:50) `operator<<(FArchive&, FName&)` | FName은 문자열 라운드트립 |
| [Archive.h:65](KraftonEngine/Source/Engine/Serialization/Archive.h:65) `operator<<(FArchive&, TArray<T>&)` | trivially copyable면 일괄, 아니면 요소별 |
| [WindowsArchive.h:10](KraftonEngine/Source/Engine/Serialization/WindowsArchive.h:10) `FWindowsBinWriter` / [WindowsArchive.h:46](KraftonEngine/Source/Engine/Serialization/WindowsArchive.h:46) `FWindowsBinReader` | `std::ifstream/ofstream` 기반 파일 백엔드 |
| [MemoryArchive.h:11](KraftonEngine/Source/Engine/Serialization/MemoryArchive.h:11) `FMemoryArchive` | 메모리 버퍼 백엔드(Duplicate 등 용도) |
| [ArchiveMath.h](KraftonEngine/Source/Engine/Serialization/ArchiveMath.h) | 수학 타입 `operator<<` (직접 열어 내용 미확인 — `확인 필요`) |

### 1-2. 공통 헤더 / 타입 식별자

| 위치 | 역할 |
| --- | --- |
| [AssetFileHeader.h:17](KraftonEngine/Source/Engine/Asset/AssetFileHeader.h:17) `struct FAssetFileHeader` | Magic(`0x54455341`='ASET'), AssetType, Version, PayloadSize |
| [AssetFileHeader.h:26](KraftonEngine/Source/Engine/Asset/AssetFileHeader.h:26) `operator<<` | 헤더 직렬화 |
| [AssetFileHeader.h:35](KraftonEngine/Source/Engine/Asset/AssetFileHeader.h:35) `IsValid(Type, Version)` | 로드 시 검증 |
| [AssetTypes.h:16](KraftonEngine/Source/Engine/Asset/AssetTypes.h:16) `enum class EAssetType` | `Unknown / StaticMesh / SkeletalMesh / Skeleton / AnimSequence(=4) / Material / Texture2D / FbxScene(=7)` 이미 모두 정의됨 |

### 1-3. 이 프레임워크를 실제로 쓰는 타입들 (UAnimSequence 외 사례)

`void Serialize(FArchive&)` 또는 `friend operator<<` 보유 위치를 grep으로 수집한 결과
(주요 에셋 타입만 발췌):

| 위치 | 비고 |
| --- | --- |
| [StaticMesh.h:47](KraftonEngine/Source/Engine/Asset/Mesh/StaticMesh/StaticMesh.h:47) / [StaticMesh.cpp:32](KraftonEngine/Source/Engine/Asset/Mesh/StaticMesh/StaticMesh.cpp:32) `UStaticMesh::Serialize` | 헤더+`FStaticMesh::Serialize`+`StaticMaterials` |
| [StaticMeshAsset.h:77](KraftonEngine/Source/Engine/Asset/Mesh/StaticMesh/StaticMeshAsset.h:77) `FStaticMesh::Serialize` | 정적 메시 본문 데이터 |
| [SkeletalMesh.h:33](KraftonEngine/Source/Engine/Asset/Mesh/SkeletalMesh/SkeletalMesh.h:33) `USkeletalMesh::Serialize` | 동등 패턴 |
| [SkeletalMeshAsset.h:80](KraftonEngine/Source/Engine/Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h:80) `FSkeletalMesh::Serialize` | |
| [Skeleton.h:13, 33](KraftonEngine/Source/Engine/Asset/Animation/Core/Skeleton.h:13) `FSkeleton::Serialize` / `USkeleton::Serialize` | |
| [Material.h:170](KraftonEngine/Source/Engine/Asset/Material/Material.h:170) `Material::Serialize` | |
| [FBXSceneAsset.h:40](KraftonEngine/Source/Engine/Asset/Import/FBX/Types/FBXSceneAsset.h:40) `FFBXScene::Serialize` | StaticMesh/SkeletalMesh/Skeleton/AnimSequence 배열 묶음 |
| [AnimSequence.h:34, 95](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:34) `UAnimDataModel::Serialize` / `UAnimSequence::Serialize` | 본 조사 대상 |
| [AnimNotify.h:24](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:24) `FAnimNotifyEvent::operator<<` | TriggerTime / Duration / NotifyName |
| 다수 컴포넌트(`Component/*.h`)에 `Serialize(FArchive&) override` 존재 | 씬/프리팹 JSON과 별도 경로일 가능성 — `확인 필요` |

### 1-4. 파일 백엔드를 실제로 호출하는 곳

`FWindowsBinReader/Writer`를 직접 인스턴스화하는 호출처는 3곳뿐이다.

| 위치 | 용도 |
| --- | --- |
| [ObjManager.cpp:146, 206, 234](KraftonEngine/Source/Engine/Asset/Import/OBJ/ObjManager.cpp:146) | OBJ → static mesh 바이너리 캐시 |
| [FBXManager.cpp:240, 274](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:240) | FBX 씬 캐시 read/write (`SaveSceneToCache` / `TryLoadSceneFromCache`) |
| [WindowsArchive.h](KraftonEngine/Source/Engine/Serialization/WindowsArchive.h) | 정의부 |

### 1-5. 에셋 레지스트리 / 매니저

`AssetManager`라는 범용 레지스트리는 발견되지 않았다. 대신 매니저 단위 캐시가 존재한다.

- `FFBXManager::FbxSceneCache` — `TMap<NormalizedPath, UFBXSceneAsset*>` 메모리 캐시.
  디스크 캐시 경로는 [MeshManager.cpp:99-113](KraftonEngine/Source/Engine/Asset/Import/MeshManager.cpp:99)
  `GetFbxSceneCacheFilePath` 가 `Asset/FBXSceneCache/<stem>.fbxscene.bin` 형식으로 생성.
- `.fbxscene.bin` 헤더는 `FFBXManager::FbxSceneCacheMagic` / `FbxSceneCacheVersion`
  (FBXSceneAsset이 사용하는 `FAssetFileHeader`와는 별개의 매니저 자체 헤더 —
  [FBXManager.cpp:202-208](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:202)
  `SerializeCacheHeader`).
- 씬/프리팹 저장은 별도 시스템: `FSceneSaveManager`(.Scene, JSON, [SceneSaveManager.h:55](KraftonEngine/Source/Engine/Serialization/SceneSaveManager.h:55)),
  `FPrefabSaveManager`(.Prefab, JSON, [PrefabSaveManager.h:16](KraftonEngine/Source/Engine/Serialization/PrefabSaveManager.h:16)),
  `FCurveSaveManager`. 이들은 `FArchive`가 아니라 JSON 기반이라
  binary `FArchive` 라인과는 독립 트랙이다.

**결론** — 베이스 인프라(FArchive, FAssetFileHeader, EAssetType)는 풍부하지만,
"단일 UObject를 임의 `.asset` 파일로 save/load" 하는 **범용 에셋 매니저 계층은 부재**.
현재 실제 파일 백엔드 호출은 OBJ/FBX import 캐시 두 곳에 집중되어 있고
AnimSequence는 그 캐시 안에 묶여 있다.

---

## 2. AnimSequence / AnimDataModel 직렬화 현황

### 2-1. 멤버 단위 표

| 파일:줄 | 시그니처/멤버 | 역할 | 구현 여부 |
| --- | --- | --- | --- |
| [AnimSequence.h:34](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:34) | `void UAnimDataModel::Serialize(FArchive&)` | DataModel 본문 직렬화 진입점 | ✅ |
| [AnimSequence.cpp:18-26](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:18) | 구현체 | `BoneAnimationTracks`, `PlayLength`, `FrameRate`, `NumberOfFrames`, `NumberOfKeys`, `CurveData` 6개 멤버 모두 직렬화 | ✅ |
| [AnimSequence.h:56](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:56) | `TArray<FBoneAnimationTrack> BoneAnimationTracks;` | DataModel이 값 소유 | — |
| [AnimSequence.h:93](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:93) | `static constexpr uint32 AssetVersion = 3u` | AnimSequence 헤더 버전 | ✅ |
| [AnimSequence.h:95](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:95) | `void UAnimSequence::Serialize(FArchive&)` | AnimSequence 직렬화 진입점 | ✅ |
| [AnimSequence.cpp:28-60](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:28) | 구현체 | `FAssetFileHeader(AnimSequence, v3)` 검증, `SequenceName`, `SkeletonAssetPath`, `bHasDataModel` 플래그 + lazy 생성(`UObjectManager::CreateObject<UAnimDataModel>(this)`), `DataModel->Serialize`, `Notifies` 모두 직렬화 | ✅ |
| [AnimSequence.h:116](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:116) | `TArray<FAnimNotifyEvent> Notifies;` | AnimSequence 자체가 소유 | — |
| [AnimNotify.h:24-30](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:24) | `friend FArchive& operator<<(FArchive&, FAnimNotifyEvent&)` | TriggerTime / Duration / NotifyName 라운드트립 | ✅ |
| [AnimationTypes.h:32, 50, 69, 86, 101](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimationTypes.h:32) | `FFrameRate`, `FBoneInfo`, `FRawAnimSequenceTrack`, `FBoneAnimationTrack`, `FAnimationCurveData` 모두 `operator<<` 보유 | DataModel이 의존하는 모든 하위 타입 직렬화 가능 | ✅ (단 `FAnimationCurveData`는 [101-106](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimationTypes.h:101) `(void)Ar; (void)CurveData;` — TODO 자리만 차지) |

### 2-2. 누가 이 코드를 호출하는가

`UAnimSequence::Serialize` / `UAnimDataModel::Serialize`를 직접 호출하는
유일한 경로는 FBX 씬 캐시이다.

- [FBXSceneAsset.h:73-105](KraftonEngine/Source/Engine/Asset/Import/FBX/Types/FBXSceneAsset.h:73)
  `FFBXScene::SerializeAnimSequenceArray`
  → 로드 시 `Sequences[O][I] = UObjectManager::CreateObject<UAnimSequence>()`
  → `Sequences[O][I]->Serialize(Ar)` 호출
- [FBXSceneAsset.h:40-53](KraftonEngine/Source/Engine/Asset/Import/FBX/Types/FBXSceneAsset.h:40)
  `FFBXScene::Serialize` 가 위 헬퍼를 부른다.
- [FBXManager.cpp:261, 288](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:261)
  `TryLoadSceneFromCache` / `SaveSceneToCache` 에서
  `OutScene.Serialize(Reader)` / `Scene.Serialize(Writer)` 호출.

즉 디스크 상의 표현은 **`Asset/FBXSceneCache/<stem>.fbxscene.bin`**
한 파일 안의 sub-payload이며, AnimSequence 한 개를 따로 가리키는 디스크 경로는
"가상 참조 문자열"인 `"Foo.fbx#Anim_N"` 형식 뿐이다
([FBXManager.cpp:600-648](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:600)
`ResolveAnimSequenceReference` 참고).

### 2-3. `OpenAnimSequenceAsset` 두 오버로드의 현재 상태

**헤더 주석은 "stub"이라고 적혀 있으나, 구현체는 사실상 동작한다.** 헤더 주석이
구현 진행보다 뒤처져 있는 상태로 보인다.

- 헤더: [AnimSequenceEditorTab.h:17](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.h:17)
  ```
  // 정식 entry point — UAnimSequence asset 경로용. 현재는 stub.
  bool OpenAnimSequenceAsset(const FString& AssetPath);
  ```
- 단일 인자 구현: [AnimSequenceEditorTab.cpp:152-191](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:152)
  - `FMeshManager::ResolveAnimSequenceReference(AssetPath)` 호출 → 결국
    `FFBXManager::ResolveAnimSequenceReference` → `LoadFbxScene` → FBX 씬 캐시
    역직렬화 또는 FBX SDK 재import → 결과 `UAnimSequence*` 반환.
  - PreviewMesh 결정 로직(outer로 잡힌 `UFBXSceneAsset` → fallback FBX 로드 등).
  - 끝에서 두 인자 오버로드 호출.
- 두 인자 구현: [AnimSequenceEditorTab.cpp:193-](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:193)
  - `PreviewMesh`, `AnimSequence` 멤버 세팅, `FUAnimSequenceDataSource` 생성, PreviewScene 준비.

**기대 입력 형태**는 디스크의 `.asset` 파일 경로가 아니라
**`"Path/To/Foo.fbx#Anim_N"` 형식의 가상 참조 문자열**이다. 그 문자열은 결국
FBX 원본 + 캐시(`.fbxscene.bin`)로 해석된다. 별도 `.asset` 파일을 가리키도록
바뀌려면 위 흐름과 별개의 경로가 필요하다(현재 없음).

### 2-4. Notify 영속화 흐름의 단절 지점

런타임 객체(`UAnimSequence::Notifies`)는 에디터에서
편집된다 — [AnimSequenceDataSource.cpp:49-98](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceDataSource.cpp:49)
`AddNotify` / `RemoveNotify` / `UpdateNotify` 가 모두 `Sequence->...Notifies` 를 직접 수정한다.

그러나 이 편집을 **디스크에 다시 쓰는 호출은 발견되지 않는다**.

- `SaveSceneToCache`는 [FBXManager.cpp:539](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:539)
  `LoadFbxScene` 안의 cache miss 분기에서만 호출됨 (즉 import 직후 1회).
- 에디터의 Notify 추가 UI([AnimSequenceEditorTab.cpp:642-650](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:642))도
  `DataSource->AddNotify` 만 부르고 별도 save를 트리거하지 않는다.
- 코드베이스 grep 결과 `Save.*Sequence` / `SaveAnim` 형태의 함수는 부재.

결과: Notify 편집은 **세션 메모리에만 살아 있다가 프로세스 종료 시 사라진다.**

---

## 3. Bone Track 소유 구조

### 3-1. 보유 형태

| 위치 | 멤버 / 시그니처 | 소유 형태 |
| --- | --- | --- |
| [AnimSequence.h:56](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:56) | `TArray<FBoneAnimationTrack> BoneAnimationTracks;` | UAnimDataModel이 **값으로 소유** |
| [AnimationTypes.h:81-92](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimationTypes.h:81) | `FBoneAnimationTrack { FName Name; FRawAnimSequenceTrack InternalTrack; }` | 값 포함 |
| [AnimationTypes.h:63-76](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimationTypes.h:63) | `FRawAnimSequenceTrack { TArray<FVector> PosKeys; TArray<FQuat> RotKeys; TArray<FVector> ScaleKeys; }` | 값 포함 |
| 변경 진입점 | [AnimSequence.h:36](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:36) `SetBoneAnimationTracks(TArray<FBoneAnimationTrack>&&)` (move-in) | 외부 참조를 보관하는 setter는 없음 |

즉 트랙 데이터는 **외부 버퍼를 참조하지 않고 자체 메모리에 들고 있는 진짜 소유** 모델이다.

### 3-2. 생성 출처 (코드를 끝까지 따라가서 확인)

`SetBoneAnimationTracks` 호출지를 grep한 결과는 **단 한 곳**:

- [FbxAnimationParser.cpp:222](KraftonEngine/Source/Engine/Asset/Import/FBX/Parser/FbxAnimationParser.cpp:222)
  `DataModel->SetBoneAnimationTracks(std::move(Tracks));`
  — FBX 애니메이션 스택을 본 단위로 평가해 `Tracks`를 채워 넣는다
  ([FbxAnimationParser.cpp:151-234](KraftonEngine/Source/Engine/Asset/Import/FBX/Parser/FbxAnimationParser.cpp:151) 참고).

`CreateObject<UAnimSequence>` / `<UAnimDataModel>` 호출지도 두 곳뿐:

- [FbxAnimationParser.cpp:151-152](KraftonEngine/Source/Engine/Asset/Import/FBX/Parser/FbxAnimationParser.cpp:151)
  — FBX import 경로에서 새 시퀀스+데이터모델 생성, `Sequence`가 DataModel의 Outer.
- [FBXSceneAsset.h:97](KraftonEngine/Source/Engine/Asset/Import/FBX/Types/FBXSceneAsset.h:97)
  — 로드 시 빈 시퀀스 생성 (DataModel은 [AnimSequence.cpp:52](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:52)에서 lazy 생성).

런타임에 트랙을 직접 코드로 주입하거나, 다른 포맷 import에서 채우는 경로는
발견되지 않았다. **모든 트랙 데이터는 (1) FBX SDK 직접 평가, 또는
(2) FBX 씬 캐시 역직렬화** — 둘 중 하나로 들어온다.

### 3-3. UAnimSequence ↔ UAnimDataModel 소유 관계

| 항목 | 내용 |
| --- | --- |
| 보유 형태 | [AnimSequence.h:82](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.h:82) `UAnimDataModel* DataModel = nullptr;` (raw 포인터) |
| Outer 관계 | [FbxAnimationParser.cpp:152](KraftonEngine/Source/Engine/Asset/Import/FBX/Parser/FbxAnimationParser.cpp:152) `CreateObject<UAnimDataModel>(Sequence)` — DataModel의 Outer는 Sequence |
| 로드 시 lazy 생성 | [AnimSequence.cpp:52](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:52) `DataModel = UObjectManager::Get().CreateObject<UAnimDataModel>(this);` |
| Outer 시맨틱 | [Object.h:46](KraftonEngine/Source/Engine/Object/Object.h:46) 주석 “Outer — 객체의 논리적 스코프 (**소유 의미 아님**). 직렬화 제외.” |
| 수명 관리 주체 | `UObjectManager` 단일 풀(`GUObjectArray`) — 명시적 `DestroyObject` 호출 시까지 살아 있음. UAnimSequence가 소멸하더라도 DataModel은 자동으로 해제되지 않는다 — **확인 필요(잠재적 누수 가능성)** |

논리 스코프상 “Sequence가 소유한 것처럼 보이지만”, 실제 메모리 수명 결합은 약하다.

### 3-4. FBX 재import 없이 메모리 유지 가능성

- 첫 로드: `LoadFbxScene` → 캐시 hit이면 [FBXManager.cpp:226-266](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:226)
  `TryLoadSceneFromCache`만 호출, FBX SDK 진입 없이 `FFBXScene::Serialize`로 모든 데이터 복원.
- 이후: `FFBXManager::FbxSceneCache`(메모리 맵)에 `UFBXSceneAsset*`이 살아 있으므로
  같은 경로 재요청은 메모리 hit ([FBXManager.cpp:518-523](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:518)).

따라서 **현재 구조 자체로도 “FBX 재import 없이 트랙을 메모리에서 독립 유지”는 이미 성립**한다.
다만 그 “독립”의 의미가 **하나의 FBX 씬 캐시 파일에 묶여 있다**는 것이지,
AnimSequence 한 개를 자기 파일에서 단독으로 부활시킬 수 있다는 의미는 아니다.

---

## 종합 진단

### 참조 vs 소유

현재 코드는 **소유(own) 모델에 매우 가깝다**:

- `UAnimDataModel`은 bone track을 값으로 가지고 직접 직렬화한다.
- `UAnimSequence`는 Notify를 값으로 가지고 직접 직렬화한다.
- 외부 자산을 키 단위로 참조해 lazy 페치하는 식의 코드(외부 클립 핸들, 참조 카운트, weak ref 등)는 발견되지 않았다.

다만 **현재 디스크 표현이 “FBX 씬 캐시” 하나에 통합되어 있어서**,
바깥에서 보면 마치 “AnimSequence가 FBXSceneAsset을 참조하는” 구조처럼 보인다
(에디터에서 `Sequence->GetTypedOuter<UFBXSceneAsset>()` 식으로 fallback 한 흔적이
[AnimSequenceEditorTab.cpp:167](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:167)에 있다).
코드 의미는 소유, 디스크 의미는 통합 캐시 — 라고 정리할 수 있다.

### `.asset` 직렬화를 새로 구현할 때 예상 작업 규모

**필요한 것의 대부분은 이미 만들어져 있다.** 신설보다 “기존 인프라 위에서 호출 경로/관리 계층을 한 단 추가” 수준.

이미 가지고 있는 것:
- `FArchive` + Windows 파일 백엔드.
- `FAssetFileHeader` (AnimSequence, v3까지 정의 + 검증 로직까지 완성).
- `UAnimSequence::Serialize` 완성 — 단일 `FWindowsBinWriter`를 만들어
  `Sequence->Serialize(Writer)` 한 줄 호출만 해도 라운드트립 성립.

추가가 필요한 것:
1. **저장 측 진입점** — “현재 열려 있는 AnimSequence를 `.asset`(또는 임의 확장자) 파일로 쓰기” 함수
   (FBXManager 옆에 작은 `FAnimSequenceSaveManager` 한 개 정도). 본문은
   `FAssetFileHeader` 검증 분기와 `UAnimSequence::Serialize`만 호출하면 됨.
2. **로드 측 진입점 / 경로 규약** — 현재 `OpenAnimSequenceAsset`이 받는 문자열은
   `Foo.fbx#Anim_N` 형식이라 새 경로(.asset)와 충돌 없이 어떻게 분기할지 결정 필요
   (`ResolveAnimSequenceReference` 옆에 확장자 분기 한 줄 추가).
3. **Outer/owning policy** — `.asset`에서 로드된 AnimSequence는
   기존 `UFBXSceneAsset`을 outer로 가지지 않으므로,
   PreviewMesh 결정 로직([AnimSequenceEditorTab.cpp:166-188](KraftonEngine/Source/Editor/UI/SkeletalEditor/AnimSequenceEditorTab.cpp:166))이
   `SkeletonAssetPath`만으로 mesh를 찾을 수 있어야 함. 현재는 outer가 없으면
   AssetPath의 `#` 위치를 잘라 FBX를 직접 부르는 fallback에 의존 — `.asset` 경로에서는
   `SkeletonAssetPath`(별도 fbx 참조 문자열)만 가지고 fallback을 가야 함.
4. **에디터 save 트리거** — Notify 편집 후 디스크에 다시 쓰는 호출이 현재 부재.
   메뉴/단축키 / 자동 dirty 추적 정책 결정 필요.
5. **lazy DataModel 생성 시 Outer 인자** — [AnimSequence.cpp:52](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimSequence.cpp:52)는
   이미 `this`를 outer로 지정해 두었으므로 별도 조치 불필요.
6. **수명 관리** — UAnimSequence 소멸 시 DataModel을 함께 destroy하는 정책 또는 GC 도입
   (현재 부재 — 위 3-3 표 마지막 행 참조).

전체적으로 **“직렬화 시스템 신설”이 아니라 “타입 한 개를 standalone asset으로 등록”** 수준의 작업.

### 끊긴 흐름 / 확인 필요 항목

- **끊긴 흐름**
  - Notify가 메모리에서만 살아 있고 디스크로 환원되는 경로가 부재 (위 2-4 참조).
  - `OpenAnimSequenceAsset` 헤더 주석이 stub이라고 적혀 있지만 구현은 동작 —
    헤더 주석 정리 필요(주석 수정만이라도, 본 조사 외 작업).
  - UAnimSequence 단일 파일 표현이 부재 — 항상 `.fbxscene.bin` 통째 단위.

- **확인 필요**
  - [ArchiveMath.h](KraftonEngine/Source/Engine/Serialization/ArchiveMath.h) 의 정확한 내용
    (FMatrix/FVector/FQuat operator<< 존재 여부 등은 미열람).
  - UObject 수명 관리 모델 — `UObjectManager::DestroyObject`를 호출하는 코드 흐름이 어디까지 정비되어 있는지.
    AnimSequence/DataModel이 소멸할 때 짝지어진 객체가 GC 되는지 미확인.
  - `FAnimationCurveData` 는 빈 `operator<<` 자리만 있고 본문 데이터가 없음 ([AnimationTypes.h:101-106](KraftonEngine/Source/Engine/Asset/Animation/Core/AnimationTypes.h:101)) — 향후 확장 예정이라는 TODO만 존재.
  - 컴포넌트들의 `Serialize(FArchive&) override`가 실제 어떤 파이프라인에서 사용되는지
    (Scene/Prefab은 JSON 사용 — 사용처 매핑 미완료).
  - 메모리 캐시(`FFBXManager::FbxSceneCache`)의 무효화/축출 정책 부재 여부 미확인.

### 범위 외 관찰 사항

- `FAnimNotifyEvent`에는 `IsTriggeredBetween` 로직이 있어 루프 재생 시
  notify dispatch 판정까지 준비되어 있다 ([AnimNotify.h:35-46](KraftonEngine/Source/Engine/Asset/Animation/Notify/AnimNotify.h:35)). 단,
  이 함수의 호출처(런타임 dispatch 측)는 본 조사에서 추적하지 않았다.
- `FFBXManager::ReleaseAllGPU`가 `FbxSceneCache.clear()` 한 줄로 정의되어 있어
  ([FBXManager.cpp:692](KraftonEngine/Source/Engine/Asset/Import/FBX/Core/FBXManager.cpp:692)),
  GPU 리소스/UObject 해제가 누락될 가능성 있음 — 본 조사 범위 외이나 기록.
- `FAnimationCurveData`가 사실상 placeholder인 것이 향후 “애니메이션 커브” 기능 추가 시 직렬화 호환성에 영향을 줄 가능성 — 버전 bump 시 주의.
