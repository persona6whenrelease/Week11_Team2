#pragma once

// Math aggregator — single source of truth for math headers.
// Include order respects the internal dependency chain:
//   Vector → Matrix → MathUtils → Quat → Rotator → Transform
// Add new math headers here only if widely used and rarely modified.

#include "Vector.h"
#include "Matrix.h"
#include "MathUtils.h"
#include "Quat.h"
#include "Rotator.h"
#include "Transform.h"
