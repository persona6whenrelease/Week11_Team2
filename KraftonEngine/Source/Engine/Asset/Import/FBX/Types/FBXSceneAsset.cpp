/**
 * FBX 씬 에셋 타입의 UObject 등록을 제공한다.
 *
 * 실제 직렬화 로직은 헤더에 인라인으로 정의되어 있으며, 이 파일은 리플렉션/팩토리 시스템에서
 * UFBXSceneAsset 타입을 생성할 수 있도록 클래스 등록 매크로를 배치한다.
 */

#include "Asset/Import/FBX/Types/FBXSceneAsset.h"

IMPLEMENT_CLASS(UFBXSceneAsset, UObject)
