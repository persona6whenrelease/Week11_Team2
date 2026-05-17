#pragma once

#include "Core/CoreTypes.h"

struct FBoneInfo;

// SkeletalMesh tab과 AnimSequence tab이 모두 사용하는 본 계층 트리 위젯.
// 자체 ImGui 상태 없이 호출자가 selection / scroll / open 상태를 들고 다닌다.
namespace SkeletonTreeUtil
{
	void RenderBoneTreeNode(
		const TArray<FBoneInfo>& Bones,
		int32 BoneIndex,
		int32& InOutSelectedBoneIndex,
		int32& OutDoubleClickedBoneIndex,
		TArray<int32>& OutVisibleOrder,
		bool bScrollToSelected,
		int32 RequestSetOpenBoneIndex,
		bool bRequestSetOpenValue);

	// 위 함수를 모든 root bone에 대해 호출하고, 화살표 키 네비게이션까지 처리하는 헬퍼.
	// 반환: 사용자가 더블클릭한 본 인덱스 (-1 이면 없음)
	int32 RenderSkeletonTree(
		const TArray<FBoneInfo>& Bones,
		int32& InOutSelectedBoneIndex,
		bool& InOutScrollToSelected,
		int32& InOutRequestSetOpenBoneIndex,
		bool& InOutRequestSetOpenValue);
}
