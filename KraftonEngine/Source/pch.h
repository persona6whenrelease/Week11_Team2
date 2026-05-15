#pragma once

// =============================================================================
// KraftonEngine — Precompiled Header
// -----------------------------------------------------------------------------
// Scope: KraftonEngine.vcxproj (Engine + Editor + GameClient + ObjViewer).
// CrossyGame.vcxproj is excluded by Directory.Build.props conditional logic.
//
// Maintenance rules:
//   - Add new STL/typedef content to Engine/Core/CoreTypes.h, not here.
//   - Add new math content to Engine/Math/Math.h, not here.
//   - Direct additions to this file trigger a full ~233-TU rebuild.
//   - C-category headers are commented out below — opt in only after review.
// =============================================================================

// -----------------------------------------------------------------------------
// Platform & System
// -----------------------------------------------------------------------------
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <Windows.h>               // transitively used in ~26 in-scope files
#endif

// -----------------------------------------------------------------------------
// Standard Library — via CoreTypes.h aggregator
//   Brings in: <stdint.h> <cassert> <vector> <list> <unordered_set>
//              <unordered_map> <queue> <array> <string> <utility>
//   Plus UE-style typedefs (int8..uint64, FString, TArray, TMap, ...) and
//   check()/checkf() macros.
// -----------------------------------------------------------------------------
#include "Engine/Core/CoreTypes.h"

// -----------------------------------------------------------------------------
// Math — via Math.h aggregator
//   Brings in: Vector, Matrix, MathUtils, Quat, Rotator, Transform.
//   Transitively: <cmath>, <cstring>, <DirectXMath.h>.
// -----------------------------------------------------------------------------
#include "Engine/Math/Math.h"

// -----------------------------------------------------------------------------
// Extra STL (A category)
// -----------------------------------------------------------------------------
#include <algorithm>                   // used in 62/233 TU

// -----------------------------------------------------------------------------
// C — Review required (uncomment to opt in)
//   Each line below is a candidate. Discuss build-time vs. churn trade-off
//   before enabling. See Document/categorized.md for rationale.
// -----------------------------------------------------------------------------
 #include <filesystem>               // used in 22/233 TU — heavy parse cost
 #include <fstream>                  // used in 17/233 TU — heavy parse cost
 #include "Core/Log.h"               // used in 42/233 TU — logging API may evolve
 #include "Engine/Platform/Paths.h"  // used in 29/233 TU (combined aliases)

// -----------------------------------------------------------------------------
// Engine Foundation slot (currently empty)
//   Reserved for headers promoted from C category after review.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Editor Common slot (currently empty)
//   Reserved for Editor/Packaging, Editor/Selection, Editor/UI/ContentBrowser
//   shared headers when/if identified.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// GameClient slot (currently empty — placeholder)
//   GameClient/ currently has 9 TUs. When the module solidifies, list its
//   foundation header here. Example:
//     #include "GameClient/<foundation_header>.h"
// -----------------------------------------------------------------------------
