#include "Editor/UI/SkeletalEditor/SkeletonTreeUtil.h"

#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "ImGui/imgui.h"

#include <algorithm>
#include <cstdint>

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
	bool bRequestSetOpenValue)
{
	if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
	{
		return;
	}

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow;
	if (InOutSelectedBoneIndex == BoneIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	bool bHasChild = false;
	for (int32 ChildIndex = 0; ChildIndex < static_cast<int32>(Bones.size()); ++ChildIndex)
	{
		if (Bones[ChildIndex].ParentIndex == BoneIndex)
		{
			bHasChild = true;
			break;
		}
	}
	if (!bHasChild)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf;
	}

	if (BoneIndex == RequestSetOpenBoneIndex)
	{
		ImGui::SetNextItemOpen(bRequestSetOpenValue);
	}

	const bool bOpen = ImGui::TreeNodeEx(
		reinterpret_cast<void*>(static_cast<intptr_t>(BoneIndex)),
		Flags,
		"%s",
		Bones[BoneIndex].Name.c_str());

	OutVisibleOrder.push_back(BoneIndex);
	if (bScrollToSelected && InOutSelectedBoneIndex == BoneIndex)
	{
		ImGui::SetScrollHereY(0.5f);
	}

	if (ImGui::IsItemClicked())
	{
		InOutSelectedBoneIndex = BoneIndex;
	}

	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		OutDoubleClickedBoneIndex = BoneIndex;
	}

	if (bOpen)
	{
		for (int32 ChildIndex = 0; ChildIndex < static_cast<int32>(Bones.size()); ++ChildIndex)
		{
			if (Bones[ChildIndex].ParentIndex == BoneIndex)
			{
				RenderBoneTreeNode(Bones, ChildIndex, InOutSelectedBoneIndex,
					OutDoubleClickedBoneIndex, OutVisibleOrder, bScrollToSelected,
					RequestSetOpenBoneIndex, bRequestSetOpenValue);
			}
		}
		ImGui::TreePop();
	}
}

int32 RenderSkeletonTree(
	const TArray<FBoneInfo>& Bones,
	int32& InOutSelectedBoneIndex,
	bool& InOutScrollToSelected,
	int32& InOutRequestSetOpenBoneIndex,
	bool& InOutRequestSetOpenValue)
{
	if (Bones.empty())
	{
		ImGui::TextDisabled("No bones");
		return -1;
	}

	int32 DoubleClickedBoneIndex = -1;
	TArray<int32> VisibleOrder;
	VisibleOrder.reserve(Bones.size());

	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones.size()); ++BoneIndex)
	{
		if (Bones[BoneIndex].ParentIndex < 0)
		{
			RenderBoneTreeNode(Bones, BoneIndex, InOutSelectedBoneIndex,
				DoubleClickedBoneIndex, VisibleOrder, InOutScrollToSelected,
				InOutRequestSetOpenBoneIndex, InOutRequestSetOpenValue);
		}
	}
	InOutScrollToSelected = false;
	InOutRequestSetOpenBoneIndex = -1;

	// 화살표 키 네비게이션
	if (ImGui::IsWindowFocused() && !VisibleOrder.empty())
	{
		const bool bDown  = ImGui::IsKeyPressed(ImGuiKey_DownArrow,  true);
		const bool bUp    = ImGui::IsKeyPressed(ImGuiKey_UpArrow,    true);
		const bool bLeft  = ImGui::IsKeyPressed(ImGuiKey_LeftArrow,  true);
		const bool bRight = ImGui::IsKeyPressed(ImGuiKey_RightArrow, true);

		int32 Cur = -1;
		if (bDown || bUp || bLeft || bRight)
		{
			for (int32 i = 0; i < static_cast<int32>(VisibleOrder.size()); ++i)
			{
				if (VisibleOrder[i] == InOutSelectedBoneIndex)
				{
					Cur = i;
					break;
				}
			}
		}

		if (bDown || bUp)
		{
			int32 Next = Cur;
			if (Cur < 0) Next = 0;
			else if (bDown) Next = std::min(Cur + 1, static_cast<int32>(VisibleOrder.size()) - 1);
			else Next = std::max(Cur - 1, 0);

			if (Next != Cur)
			{
				InOutSelectedBoneIndex = VisibleOrder[Next];
				InOutScrollToSelected = true;
			}
		}
		else if ((bLeft || bRight) && Cur >= 0)
		{
			const int32 SelBone = InOutSelectedBoneIndex;

			int32 FirstChild = -1;
			for (int32 j = 0; j < static_cast<int32>(Bones.size()); ++j)
			{
				if (Bones[j].ParentIndex == SelBone) { FirstChild = j; break; }
			}
			const bool bHasChildren = (FirstChild >= 0);
			const bool bIsOpen = bHasChildren
				&& Cur + 1 < static_cast<int32>(VisibleOrder.size())
				&& Bones[VisibleOrder[Cur + 1]].ParentIndex == SelBone;

			if (bRight && bHasChildren)
			{
				if (!bIsOpen)
				{
					InOutRequestSetOpenBoneIndex = SelBone;
					InOutRequestSetOpenValue = true;
				}
				InOutSelectedBoneIndex = FirstChild;
				InOutScrollToSelected = true;
			}
			else if (bLeft)
			{
				if (bHasChildren && bIsOpen)
				{
					InOutRequestSetOpenBoneIndex = SelBone;
					InOutRequestSetOpenValue = false;
				}
				else
				{
					const int32 Parent = Bones[SelBone].ParentIndex;
					if (Parent >= 0)
					{
						InOutSelectedBoneIndex = Parent;
						InOutScrollToSelected = true;
					}
				}
			}
		}
	}

	return DoubleClickedBoneIndex;
}

} // namespace SkeletonTreeUtil
