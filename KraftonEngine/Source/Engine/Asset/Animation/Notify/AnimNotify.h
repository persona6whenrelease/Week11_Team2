/**
 * 애니메이션 재생 중 특정 시간에 발생하는 Notify 이벤트를 정의한다.
 *
 * Notify는 단순히 이름과 트리거 시간만 저장하는 것이 아니라, 루프 재생 중 시간이 시퀀스 끝에서
 * 처음으로 감기는 상황까지 고려해 이벤트 발생 여부를 판정한다. 런타임 로직이나 에디터 타임라인은 이
 * 구조를 통해 시퀀스 데이터와 게임 이벤트를 느슨하게 연결한다.
 */

#pragma once

#include "Camera/CameraShakeModifier.h"
#include "Core/CoreTypes.h"
#include "Object/FName.h"
#include "Serialization/Archive.h"

/**
 * Notify가 트리거 시 발생시킬 효과의 종류를 식별하는 enum.
 * 새 타입은 enum 끝에 append만 허용 (직렬화 안정성).
 */
enum class EAnimNotifyType : uint8
{
    None = 0,
    Sound = 1,
    CameraShake = 2,
};

inline FArchive &operator<<(FArchive &Ar, FCameraShakeParams &Params)
{
    Ar << Params.Pattern;
    Ar << Params.Duration;
    Ar << Params.BlendInTime;
    Ar << Params.BlendOutTime;
    Ar << Params.LocationAmplitude;
    Ar << Params.RotationAmplitude;
    Ar << Params.FOVAmplitude;
    Ar << Params.Frequency;
    Ar << Params.Roughness;
    Ar << Params.bApplyInCameraLocalSpace;
    Ar << Params.bSingleInstance;
    Ar << Params.Seed;
    return Ar;
}

/**
 * 애니메이션 재생 시간에 맞춰 발생하는 이벤트 정보를 저장한다.
 */
struct FAnimNotifyEvent
{
    float TriggerTime = 0.0f;
    float Duration = 0.0f;
    FName NotifyName;

    EAnimNotifyType    Type = EAnimNotifyType::None;
    FString            SoundId;
    FCameraShakeParams ShakeParams;

    friend FArchive &operator<<(FArchive &Ar, FAnimNotifyEvent &Notify)
    {
        Ar << Notify.TriggerTime;
        Ar << Notify.Duration;
        Ar << Notify.NotifyName;

        uint8 TypeByte = static_cast<uint8>(Notify.Type);
        Ar << TypeByte;
        if (Ar.IsLoading())
        {
            Notify.Type = static_cast<EAnimNotifyType>(TypeByte);
        }

        Ar << Notify.SoundId;
        Ar << Notify.ShakeParams;
        return Ar;
    }

    /**
     * 이전 시간과 현재 시간 사이에 Notify가 지나갔는지 루프 상황까지 포함해 판정한다.
     */
    bool IsTriggeredBetween(float PreviousTime, float CurrentTime, float SequenceLength) const
    {
        if (SequenceLength <= 0.0f)
        {
            return false;
        }
        if (PreviousTime <= CurrentTime)
        {
            return PreviousTime < TriggerTime && TriggerTime <= CurrentTime;
        }
        return TriggerTime > PreviousTime || TriggerTime <= CurrentTime;
    }
};
