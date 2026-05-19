/**
 * AnimGraph 노드와 인스턴스가 공유하는 포즈 유틸리티.
 *
 * 평가 출력 형식은 TArray<FTransform> 이며, USkinnedMeshComponent::ApplyEvaluatedPose에서만
 * FMatrix로 변환된다. 본 헤더는 bind pose fallback 변환 헬퍼만 두며, Blend 산술은 별도(후속 단계).
 */

#pragma once

#include "Asset/Animation/Core/Skeleton.h"
#include "Core/CoreTypes.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/Transform.h"
#include "Math/Vector.h"

namespace AnimPoseUtils
{
    /**
     * USkeleton 본의 local bind pose(FMatrix)를 FTransform으로 분해한다.
     * 노드가 트랙이 없는 본을 만나거나 전체 fallback 시 사용.
     */
    inline FTransform BindPoseToTransform(const FMatrix &M)
    {
        return FTransform(M.GetLocation(), FQuat::FromMatrix(M), M.GetScale());
    }

    /**
     * 두 FTransform을 alpha (0..1)로 본별 TRS 보간한다.
     * - 위치/스케일: FVector::Lerp
     * - 회전: FQuat::Slerp (최단경로·정규화 포함, Quat.h)
     * Alpha는 호출자가 [0,1]로 클램프했다고 가정 — 노드 측에서 미리 처리.
     */
    inline FTransform BlendTransform(const FTransform &A, const FTransform &B, float Alpha)
    {
        return FTransform(
            FVector::Lerp(A.Location, B.Location, Alpha),
            FQuat::Slerp (A.Rotation, B.Rotation, Alpha),
            FVector::Lerp(A.Scale,    B.Scale,    Alpha)
        );
    }

    /**
     * Skeleton 본 수만큼 OutLocalPose를 bind pose FTransform으로 채운다.
     * Skeleton이 nullptr이면 OutLocalPose를 비운다.
     */
    inline void FillBindPoseTransforms(const USkeleton *Skeleton, TArray<FTransform> &OutLocalPose)
    {
        if (!Skeleton)
        {
            OutLocalPose.clear();
            return;
        }
        const TArray<FBoneInfo> &Bones = Skeleton->GetBones();
        OutLocalPose.resize(Bones.size());
        for (size_t i = 0; i < Bones.size(); ++i)
        {
            OutLocalPose[i] = BindPoseToTransform(Bones[i].LocalBindPose);
        }
    }
}
