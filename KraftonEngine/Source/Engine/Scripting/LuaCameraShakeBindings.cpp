// LuaCameraShakeBindings.cpp

#include "LuaBindings.h"
#include "SolInclude.h"

#include "Camera/CameraShakeModifier.h"
#include "Camera/PlayerCameraManager.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"

void RegisterCameraShakeBinding(sol::state& Lua)
{
	Lua.new_enum("ECameraShakePattern",
		"Sine",   ECameraShakePattern::Sine,
		"Perlin", ECameraShakePattern::Perlin);

	Lua.new_usertype<FCameraShakeParams>(
		"FCameraShakeParams",
		sol::constructors<FCameraShakeParams()>(),

		"Pattern",                 &FCameraShakeParams::Pattern,
		"Duration",                &FCameraShakeParams::Duration,
		"BlendInTime",             &FCameraShakeParams::BlendInTime,
		"BlendOutTime",            &FCameraShakeParams::BlendOutTime,
		"LocationAmplitude",       &FCameraShakeParams::LocationAmplitude,
		"RotationAmplitude",       &FCameraShakeParams::RotationAmplitude,
		"FOVAmplitude",            &FCameraShakeParams::FOVAmplitude,
		"Frequency",               &FCameraShakeParams::Frequency,
		"Roughness",               &FCameraShakeParams::Roughness,
		"ApplyInCameraLocalSpace", &FCameraShakeParams::bApplyInCameraLocalSpace,
		"SingleInstance",          &FCameraShakeParams::bSingleInstance,
		"Seed",                    &FCameraShakeParams::Seed);

	Lua.new_usertype<APlayerCameraManager>(
		"PlayerCameraManager",
		sol::no_constructor,

		"StartCameraShake",
		[](APlayerCameraManager* Self, const FCameraShakeParams& Params)
		{
			if (Self)
			{
				Self->StartCameraShake(Params);
			}
		});
}
