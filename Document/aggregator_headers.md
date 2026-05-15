# Aggregator Headers

The PCH centers on **two** aggregator headers. The PCH `#include`s these aggregators (one line each) rather than the underlying STL/system/math headers — preserving the Single Source of Truth principle.

## 1. `Engine/Core/CoreTypes.h` (existing, reused as-is)

**Path:** `KraftonEngine\Source\Engine\Core\CoreTypes.h`

**Includes (10 STL headers):**

| Header | Purpose |
|--------|---------|
| `<stdint.h>` | Fixed-width int typedefs (backs `int8`…`uint64`) |
| `<cassert>` | `assert` backing the `check`/`checkf` macros |
| `<vector>` | Backs `TArray<T>` |
| `<list>` | Backs `TDoubleLinkedList<T>` / `TLinkedList<T>` |
| `<unordered_set>` | Backs `TSet<T>` |
| `<unordered_map>` | Backs `TMap<K,V>` |
| `<queue>` | Backs `TQueue<T>` |
| `<array>` | Backs `TStaticArray<T,N>` |
| `<string>` | Backs `FString` |
| `<utility>` | Backs `TPair<T1,T2>` |

**Exposed:**
- Integer typedefs: `int8`, `int16`, `int32`, `int64`, `uint8`, `uint16`, `uint32`, `uint64`
- Container aliases: `FString`, `TArray`, `TDoubleLinkedList`, `TLinkedList`, `TSet`, `TMap`, `TPair`, `TStaticArray`, `TQueue`
- Macros: `check(expr)`, `checkf(expr, msg)` (assert-based; no-op in non-Debug)

**Decision:** Reused unchanged. PCH includes this aggregator with a single line.

## 2. `Engine/Math/Math.h` (NEW — created by this task)

**Path:** `KraftonEngine\Source\Engine\Math\Math.h`

**Rationale for creating a new aggregator:** No pre-existing aggregator covered the full Math suite. `MathUtils.h` only includes `Vector.h`. Creating a thin `Math.h` aggregator avoids expanding `MathUtils.h`'s responsibility beyond its utility focus and keeps the PCH a one-line include.

**Internal dependency chain (verified):**

```
Vector.h        →  <cmath>, <DirectXMath.h>
Matrix.h        →  <cmath>, <cstring>, Vector.h
MathUtils.h     →  <cmath>, Core/CoreTypes.h, Vector.h
Quat.h          →  <cmath>, Vector.h, MathUtils.h
Rotator.h       →  <cmath>, Vector.h, MathUtils.h
Transform.h     →  Matrix.h, Rotator.h, Quat.h
```

**Aggregator content (`Math.h`):**

```cpp
#pragma once
#include "Vector.h"      // base type — depends on DirectXMath.h
#include "Matrix.h"      // depends on Vector
#include "MathUtils.h"   // depends on Vector + CoreTypes
#include "Quat.h"        // depends on Vector + MathUtils
#include "Rotator.h"     // depends on Vector + MathUtils
#include "Transform.h"   // depends on Matrix + Rotator + Quat
```

**Side benefit:** `Vector.h` pulls `<DirectXMath.h>` — a heavyweight Windows SDK header. Centralizing this through PCH yields significant savings.

## What the PCH does NOT include from these aggregators

Both aggregators are included **as-is**. The PCH does not re-list their internal `<vector>`, `<DirectXMath.h>`, etc. This means:

- Adding/removing a header from `CoreTypes.h` or `Math.h` automatically propagates to PCH consumers
- Maintainers update only the aggregator, never the PCH, when adding STL/math content
- The PCH only adds platform/Windows-specific includes (Windows.h, etc.) and high-frequency project headers
