#pragma once

// Reflection annotations are consumed by the header generator script.
// The C++ compiler should treat them as no-ops.
#define UCLASS(...)
#define USTRUCT(...)
#define UENUM(...)
#define UFUNCTION(...)
#define FPROPERTY(...)

// ---------------------------------------------------------------------------
// GENERATED_BODY() dispatch
//
// Each .generated.h defines:
//   #define CURRENT_FILE_ID  <FileName>_h_LINE_
//   #define <FileName>_h_LINE_<N>  <class body declarations>
//
// GENERATED_BODY() concatenates CURRENT_FILE_ID with __LINE__ to select
// the correct per-class body for multi-class headers.
// ---------------------------------------------------------------------------

#define _REFL_PP_CAT(A, B)  _REFL_PP_CAT_(A, B)
#define _REFL_PP_CAT_(A, B) A##B

#define GENERATED_BODY() _REFL_PP_CAT(CURRENT_FILE_ID, __LINE__)
