/**
 * FBX 씬 에셋의 직렬화와 UObject 래핑 동작을 구현한다.
 *
 * 하나의 FBX 파일에서 생성된 여러 메시, 스켈레톤, 라이트, 컴포넌트 계층을 씬 단위로 저장하고 다시
 * 로드할 수 있게 한다. 개별 메시 에셋만 저장하는 방식으로는 잃기 쉬운 원본 씬 배치와 참조 관계를
 * 보존하는 것이 목적이다.
 */

#include "Mesh/FBX/FBXSceneAsset.h"

IMPLEMENT_CLASS(UFBXSceneAsset, UObject)
