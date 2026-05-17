#pragma once

#include "Math/Matrix.h"
#include "Serialization/Archive.h"

inline FArchive& operator<<(FArchive& Ar, FMatrix& Matrix)
{
	// SIMD aliasing 멤버 전체가 아니라 실제 직렬화 대상인 16개 float만 저장한다.
	Ar.Serialize(Matrix.Data, sizeof(Matrix.Data));
	return Ar;
}
