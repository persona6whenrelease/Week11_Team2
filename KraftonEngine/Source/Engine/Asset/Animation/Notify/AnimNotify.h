/**
 * 애니메이션 재생 중 특정 시간에 발생하는 Notify 이벤트를 정의한다.
 *
 * Notify는 단순히 이름과 트리거 시간만 저장하는 것이 아니라, 루프 재생 중 시간이 시퀀스 끝에서 처음으로
 * 감기는 상황까지 고려해 이벤트 발생 여부를 판정한다. 런타임 로직이나 에디터 타임라인은 이 구조를 통해
 * 시퀀스 데이터와 게임 이벤트를 느슨하게 연결한다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Object/FName.h"
#include "Serialization/Archive.h"

/**
 * 애니메이션 재생 시간에 맞춰 발생하는 이벤트 정보를 저장한다.
 */
struct FAnimNotifyEvent
{
    float TriggerTime = 0.0f;
    float Duration = 0.0f;
    FName NotifyName;

    friend FArchive& operator<<(FArchive& Ar, FAnimNotifyEvent& Notify)
    {
        Ar << Notify.TriggerTime;
        Ar << Notify.Duration;
        Ar << Notify.NotifyName;
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
