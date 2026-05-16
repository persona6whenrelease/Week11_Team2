"""
pch_include_scan.py — Scan all in-scope source/header files for #include directives,
tally frequencies per TU, detect conditional inclusion, and emit a CSV for PCH planning.

Run from repo root:
    Scripts\python\python.exe Scripts\pch_include_scan.py

Output: Document\include_frequency.csv
"""

from __future__ import annotations
import csv
import os
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "KraftonEngine" / "Source"

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
CONDITIONAL_OPEN_RE = re.compile(r'^\s*#\s*(if|ifdef|ifndef)\b')
CONDITIONAL_ELIF_RE = re.compile(r'^\s*#\s*(elif|else)\b')
CONDITIONAL_CLOSE_RE = re.compile(r'^\s*#\s*endif\b')

# Scope folders. Tuple of (relative path under Source, recursive bool)
SCOPE = [
    ("Engine", True),
    ("Editor/Packaging", True),
    ("Editor/Selection", True),
    ("Editor/UI/ContentBrowser", True),
    ("GameClient", True),
]

# Also include main.cpp at the project root (KraftonEngine/main.cpp) — sole root TU.
EXTRA_TU = [ROOT / "KraftonEngine" / "main.cpp"]

# Headers excluded from candidate selection regardless of count.
NEVER_PCH = {
    # auto-generated / unstable
    "pch.h",
}

# Stale files excluded by the build script
EXCLUDED_PATHS = {
    str((SRC / "Engine" / "Runtime" / "ObjectPoolSystem.cpp").resolve()).lower(),
    str((SRC / "Engine" / "Runtime" / "ObjectPoolSystem.h").resolve()).lower(),
    str((SRC / "Engine" / "Runtime" / "RowManager.cpp").resolve()).lower(),
    str((SRC / "Engine" / "Runtime" / "RowManager.h").resolve()).lower(),
    str((SRC / "Engine" / "Component" / "Movement" / "HopMovementComponent.cpp").resolve()).lower(),
    str((SRC / "Engine" / "Component" / "Movement" / "HopMovementComponent.h").resolve()).lower(),
    str((SRC / "Engine" / "Component" / "ParryComponent.cpp").resolve()).lower(),
    str((SRC / "Engine" / "Component" / "ParryComponent.h").resolve()).lower(),
    str((SRC / "Engine" / "Scripting" / "LuaParryComponentBindings.cpp").resolve()).lower(),
    str((SRC / "Engine" / "Scripting" / "LuaRowManagerBindings.cpp").resolve()).lower(),
    str((SRC / "Engine" / "Scripting" / "LuaUiBindings.cpp").resolve()).lower(),
    str((SRC / "Engine" / "UI" / "Game" / "GameUiSystem.cpp").resolve()).lower(),
    str((SRC / "Engine" / "UI" / "Game" / "GameUiSystem.h").resolve()).lower(),
}

SOURCE_EXTS = {".cpp", ".c", ".cc", ".cxx"}
HEADER_EXTS = {".h", ".hpp", ".hxx", ".inl"}


def collect_files():
    """Return (tu_files, all_files). tu_files = TU root .cpp files (denominator).
    all_files = every .cpp/.h/.hpp/.hxx/.inl in scope (we read each to gather #include lines)."""
    tu, allf = [], []
    for rel, _recursive in SCOPE:
        base = SRC / rel
        if not base.exists():
            continue
        for p in base.rglob("*"):
            if not p.is_file():
                continue
            rp = str(p.resolve()).lower()
            if rp in EXCLUDED_PATHS:
                continue
            ext = p.suffix.lower()
            if ext in SOURCE_EXTS:
                tu.append(p)
                allf.append(p)
            elif ext in HEADER_EXTS:
                allf.append(p)
    for extra in EXTRA_TU:
        if extra.exists():
            tu.append(extra)
            allf.append(extra)
    return tu, allf


def scan_file_includes(path: Path):
    """Return list of (include_str, is_system, in_conditional) for one file."""
    out = []
    depth = 0
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return out
    for line in text.splitlines():
        if CONDITIONAL_OPEN_RE.match(line):
            depth += 1
            continue
        if CONDITIONAL_CLOSE_RE.match(line):
            depth = max(0, depth - 1)
            continue
        m = INCLUDE_RE.match(line)
        if m:
            quote, inc = m.group(1), m.group(2)
            inc_norm = inc.replace("\\", "/").strip()
            out.append((inc_norm, quote == "<", depth > 0))
    return out


def git_recent_changes(months: int = 6):
    """Return dict {relpath_lower: change_count} for files changed in last N months.
    Falls back to empty dict if git fails."""
    out = defaultdict(int)
    try:
        # Windows-friendly: -- pathspec relative to repo root
        res = subprocess.run(
            ["git", "log", f"--since={months} months ago", "--name-only", "--pretty=format:"],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            check=False,
            timeout=30,
        )
        if res.returncode != 0:
            return out
        for line in res.stdout.splitlines():
            line = line.strip()
            if not line:
                continue
            out[line.replace("/", "\\").lower()] += 1
    except Exception:
        pass
    return out


def main():
    tu_files, all_files = collect_files()
    total_tu = len(tu_files)
    if total_tu == 0:
        print("No TU files found.", file=sys.stderr)
        sys.exit(1)

    # header -> {tu_path: (count, has_unconditional)}
    per_tu_data = defaultdict(dict)
    # header -> (is_system_dominant, in_cond_count, total_count)
    type_info = defaultdict(lambda: {"system": 0, "project": 0, "cond": 0, "total": 0})

    # Build a map from each scanned file to its #includes
    file_to_includes = {}
    for f in all_files:
        file_to_includes[f] = scan_file_includes(f)

    # For TU frequency: count once per TU, but also propagate transitive headers? No — we want direct includes only at .cpp level for raw stats. Cleaner.
    # However, many .cpp use only project-local headers that themselves include STL. To capture this, also include 1 level of transitive .h includes if both .cpp and the .h are in scope.
    # Per task spec, direct frequency is the primary metric. We'll add a second pass that includes transitive impact for system headers behind project aggregators.
    # KEEP IT SIMPLE: count direct includes per .cpp only. Aggregator effects are handled in pch.h via the aggregators.

    for tu in tu_files:
        seen_unique = set()
        for inc, is_sys, in_cond in file_to_includes.get(tu, []):
            key = inc.lower()
            if key in seen_unique and not in_cond:
                continue
            seen_unique.add(key)
            ti = type_info[inc]
            ti["total"] += 1
            if in_cond:
                ti["cond"] += 1
            if is_sys:
                ti["system"] += 1
            else:
                ti["project"] += 1
            per_tu_data[inc][str(tu)] = (1, not in_cond)

    # GameClient TU subset for separate ratio
    gameclient_tus = [t for t in tu_files if "\\gameclient\\" in str(t).lower()]
    total_gameclient_tu = len(gameclient_tus)
    gc_count = defaultdict(int)
    for tu in gameclient_tus:
        seen = set()
        for inc, is_sys, in_cond in file_to_includes.get(tu, []):
            key = inc.lower()
            if key in seen:
                continue
            seen.add(key)
            if in_cond:
                continue
            gc_count[inc] += 1

    recent = git_recent_changes(months=6)

    # Coverage by aggregators
    coretypes_stl = {
        "stdint.h", "cassert", "vector", "list", "unordered_set",
        "unordered_map", "queue", "array", "string", "utility",
    }
    math_aggregator_set = {
        "Vector.h", "Matrix.h", "MathUtils.h", "Quat.h", "Rotator.h", "Transform.h",
        "Engine/Math/Vector.h", "Engine/Math/Matrix.h", "Engine/Math/MathUtils.h",
        "Engine/Math/Quat.h", "Engine/Math/Rotator.h", "Engine/Math/Transform.h",
    }

    rows = []
    for inc, info in type_info.items():
        total = info["total"]
        cond = info["cond"]
        unconditional = total - cond
        type_str = "system" if info["system"] >= info["project"] else "project"
        ratio = (unconditional / total_tu) if total_tu else 0.0
        gc_ratio = (gc_count.get(inc, 0) / total_gameclient_tu) if total_gameclient_tu else 0.0
        # Aggregator-coverage flag
        covered = ""
        if inc.lower() in {x.lower() for x in coretypes_stl}:
            covered = "coretypes"
        elif inc in math_aggregator_set:
            covered = "math_aggregator"
        # Recent change count: only resolvable for project-relative paths
        rec = 0
        if type_str == "project":
            candidates = [
                inc.replace("/", "\\"),
                f"KraftonEngine\\Source\\{inc.replace('/', chr(92))}",
                f"KraftonEngine\\Source\\Engine\\{inc.replace('/', chr(92))}",
            ]
            for c in candidates:
                if c.lower() in recent:
                    rec = recent[c.lower()]
                    break
        rows.append({
            "header": inc,
            "type": type_str,
            "include_count_unconditional": unconditional,
            "include_count_conditional": cond,
            "total_tu": total_tu,
            "ratio_unconditional": f"{ratio:.3f}",
            "gameclient_ratio": f"{gc_ratio:.3f}",
            "in_conditional_dominant": "true" if cond > unconditional else "false",
            "aggregator_coverage": covered,
            "recent_changes_6mo": rec,
            "target_pch": "unified",
        })

    # Sort by unconditional count desc
    rows.sort(key=lambda r: (-r["include_count_unconditional"], r["header"].lower()))

    out_path = ROOT / "Document" / "include_frequency.csv"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8", newline="") as f:
        cols = list(rows[0].keys()) if rows else []
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        for r in rows:
            w.writerow(r)

    summary = ROOT / "Document" / "include_frequency_summary.txt"
    with summary.open("w", encoding="utf-8") as f:
        f.write(f"Total TUs scanned: {total_tu}\n")
        f.write(f"GameClient TUs:    {total_gameclient_tu}\n")
        f.write(f"Distinct headers:  {len(rows)}\n")
        f.write(f"\nTop 30 by unconditional TU count:\n")
        for r in rows[:30]:
            f.write(f"  {r['include_count_unconditional']:>4} / {total_tu}  "
                    f"({float(r['ratio_unconditional'])*100:5.1f}%)  "
                    f"[{r['type']:7}] {r['header']}  cov={r['aggregator_coverage']}  rec={r['recent_changes_6mo']}\n")

    print(f"CSV   -> {out_path}")
    print(f"Summary -> {summary}")
    print(f"TUs={total_tu}  Headers={len(rows)}")


if __name__ == "__main__":
    main()
