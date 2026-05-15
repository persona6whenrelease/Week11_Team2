/**
 * ?ㅼ펷?덊깉 硫붿떆 ?먯뀑??吏곷젹??媛?ν븳 ?쒖닔 ?곗씠??援ъ“瑜??뺤쓽?쒕떎.
 *
 * ?뺤젏??bone id/weight, 蹂?怨꾩링, inverse bind pose, ?좊땲硫붿씠???몃옓怨??대┰ ?뺣낫瑜??ы븿?쒕떎.
 * ???뚯씪????낅뱾? UObject ?섎챸?대굹 GPU 由ъ냼?ㅼ뿉 ?섏〈?섏? ?딆쑝硫? FBX ?꾪룷?곌? 留뚮뱺 寃곌낵瑜? * .uasset ?먮뒗 諛붿씠?덈━ ?곗씠?곕줈 ??ν븯怨??ㅼ떆 濡쒕뱶?섍린 ?꾪븳 ?먯뀑 ?щ㎎??以묒떖???쒕떎.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Asset/Mesh/Common/MeshCommonTypes.h"
#include "Serialization/Archive.h"

#include <algorithm>

/**
 * ?ㅽ궎?앹뿉 ?꾩슂??蹂??곹뼢 ?뺣낫瑜??ы븿???뺤젏 ?뺤떇?대떎.
 *
 * ?쇰컲 硫붿떆 ?뺤젏 ?띿꽦??理쒕? 4媛쒖쓽 bone id? weight瑜?異붽???CPU/GPU skinning ?④퀎?먯꽌 蹂??됰젹?? * ?곸슜?????덇쾶 ?쒕떎. FBX ?꾪룷?곕뒗 ?щ윭 cluster weight瑜????뺤떇?쇰줈 ?뺢퇋?뷀븳??
 */
struct FSkeletalVertex
{
    FVector  pos;
    FVector  normal;
    FVector2 tex;
    FVector4 tangent;
    uint32   BoneIDs[4] = {0, 0, 0, 0};
    float    BoneWeights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

/**
 * ?ㅼ펷?덊넠???⑥씪 蹂??뺣낫瑜??쒗쁽?쒕떎.
 *
 * 蹂??대쫫, 遺紐??몃뜳?? bind pose 湲곗? ?됰젹???댁븘 怨꾩링 援ъ“? inverse bind pose 怨꾩궛??湲곗??? * ?쒕떎.
 */
struct FBoneInfo
{
    FString Name;
    int32   ParentIndex = -1;
    FMatrix LocalBindPose = FMatrix::Identity;
    FMatrix InverseBindPose = FMatrix::Identity;

    friend FArchive &operator<<(FArchive &Ar, FBoneInfo &Bone)
    {
        Ar << Bone.Name;
        Ar << Bone.ParentIndex;
        Ar.Serialize(&Bone.LocalBindPose, sizeof(FMatrix));
        Ar.Serialize(&Bone.InverseBindPose, sizeof(FMatrix));
        return Ar;
    }
};

/**
 * ?뱀젙 ?쒓컙??蹂?濡쒖뺄 transform ?섑뵆?대떎.
 *
 * ?좊땲硫붿씠??而ㅻ툕瑜??고??꾩뿉???ㅻ（湲??ъ슫 ?뺥깭濡?蹂?섑븳 寃곌낵?대ŉ, ?몃옓 蹂닿컙???낅젰 ?곗씠?곕줈
 * ?ъ슜?쒕떎.
 */
struct FBoneAnimSample
{
    FMatrix LocalMatrix = FMatrix::Identity;
};

/**
 * ?섎굹??蹂몄뿉 ????쒓컙 ?쒖꽌 ?좊땲硫붿씠???섑뵆 紐⑸줉?대떎.
 *
 * AnimationClip? 蹂몃쭏?????몃옓??媛吏怨??덉쑝硫? ?고??꾩? ?꾩옱 ?쒓컙???대떦?섎뒗 ???섑뵆??李얠븘
 * 蹂닿컙????理쒖쥌 ?ъ쫰瑜?怨꾩궛?쒕떎.
 */
struct FBoneAnimTrack
{
    int32                   BoneIndex = -1;
    TArray<FBoneAnimSample> Samples;

    friend FArchive &operator<<(FArchive &Ar, FBoneAnimTrack &Track)
    {
        Ar << Track.BoneIndex;

        uint32 SampleCount = static_cast<uint32>(Track.Samples.size());
        Ar << SampleCount;
        if (Ar.IsLoading())
            Track.Samples.resize(SampleCount);
        if (SampleCount > 0)
        {
            Ar.Serialize(Track.Samples.data(), SampleCount * sizeof(FBoneAnimSample));
        }
        return Ar;
    }
};

/**
 * ?섎굹???ъ깮 媛?ν븳 ?좊땲硫붿씠???⑥쐞?대떎.
 *
 * Walk, Idle 媛숈? ?대┰ ?대쫫, 湲몄씠, FPS, 蹂몃퀎 ?몃옓???④퍡 媛吏꾨떎. ?먮낯 FBX??AnimationStack?? * ?붿쭊?먯꽌 ?ъ슜?????덈뒗 ?뺥깭濡??뺣━??寃곌낵?대떎.
 */
struct FAnimationClip
{
    FString                Name;
    float                  Duration = 0.0f;
    float                  FrameRate = 30.0f;
    int32                  FrameCount = 0;
    TArray<FBoneAnimTrack> Tracks;

    friend FArchive &operator<<(FArchive &Ar, FAnimationClip &Clip)
    {
        Ar << Clip.Name;
        Ar << Clip.Duration;
        Ar << Clip.FrameRate;
        Ar << Clip.FrameCount;
        Ar << Clip.Tracks;
        return Ar;
    }
};

/**
 * ?ㅼ펷?덊깉 硫붿떆 ?먯뀑??????⑥쐞?대떎.
 *
 * ?ㅽ궎???뺤젏/?몃뜳?? ?뱀뀡, 癒명떚由ъ뼹 ?щ’, 蹂?怨꾩링, ?좊땲硫붿씠???대┰??紐⑤몢 ?ы븿?쒕떎. UObject媛 ?꾨땶
 * ?쒖닔 ?곗씠?곗씠誘濡?吏곷젹?붿? ?꾪룷??寃곌낵 ??μ뿉 ?ъ슜?쒕떎.
 */
struct FSkeletalMesh
{
    FString                 PathFileName;
    TArray<FSkeletalVertex> Vertices;
    TArray<uint32>          Indices;
    TArray<FMeshSection>    Sections;
    TArray<FBoneInfo>       Bones;
    TArray<FAnimationClip>  AnimationClips;

    FVector BoundsCenter = FVector(0, 0, 0);
    FVector BoundsExtent = FVector(0, 0, 0);
    bool    bBoundsValid = false;

    void CacheBounds()
    {
        bBoundsValid = false;
        if (Vertices.empty())
            return;

        FVector LocalMin = Vertices[0].pos;
        FVector LocalMax = Vertices[0].pos;
        for (const FSkeletalVertex &V : Vertices)
        {
            LocalMin.X = (std::min)(LocalMin.X, V.pos.X);
            LocalMin.Y = (std::min)(LocalMin.Y, V.pos.Y);
            LocalMin.Z = (std::min)(LocalMin.Z, V.pos.Z);
            LocalMax.X = (std::max)(LocalMax.X, V.pos.X);
            LocalMax.Y = (std::max)(LocalMax.Y, V.pos.Y);
            LocalMax.Z = (std::max)(LocalMax.Z, V.pos.Z);
        }

        BoundsCenter = (LocalMin + LocalMax) * 0.5f;
        BoundsExtent = (LocalMax - LocalMin) * 0.5f;
        bBoundsValid = true;
    }

    void Serialize(FArchive &Ar)
    {
        Ar << PathFileName;
        Ar << Vertices;
        Ar << Indices;
        Ar << Sections;
        Ar << Bones;
        Ar << AnimationClips;
    }
};
