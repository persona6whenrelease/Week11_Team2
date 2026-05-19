#!/usr/bin/env python3
"""
MigrateReflection.py

Migrates KraftonEngine source files from manual DECLARE_CLASS / IMPLEMENT_CLASS
macros to the UE-style UCLASS() + GENERATED_BODY() reflection system.

Three phases:
  1. Parse .cpp files to extract GetEditableProperties() bodies -> FPROPERTY metadata
  2. Migrate .h files:
       - Replace DECLARE_CLASS(X,Y) with GENERATED_BODY()
       - Insert UCLASS() before each class declaration
       - Insert FPROPERTY() annotations before direct member fields
       - Remove GetEditableProperties declarations (keep only ActorComponent.h base)
       - Append #include "Stem.generated.h" after the last #include
  3. Migrate .cpp files:
       - IMPLEMENT_CLASS(X,Y) -> REGISTER_FACTORY(X)
       - DEFINE_CLASS(X,Y)    -> (remove)
       - Remove GetEditableProperties() function bodies

Usage:
    python Scripts/MigrateReflection.py [--source-root PATH] [--dry-run]
"""

import os
import re
import sys
import argparse
from dataclasses import dataclass
from typing import Optional


# ---------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------

@dataclass
class PropEntry:
    display_name: str
    prop_type: str     # EPropertyType value name, e.g. "Float", "Vec3", "Bool"
    member_name: str   # direct member variable name (no dots / arrows)
    min_val: str = ""
    max_val: str = ""
    speed_val: str = ""


# ---------------------------------------------------------------------------
# Phase 1  — collect property entries from GetEditableProperties() in .cpp
# ---------------------------------------------------------------------------

_PUSH_BACK_RE = re.compile(
    r'OutProps\.push_back\(\s*\{'
    r'\s*"([^"]+)"'                          # group 1: display name
    r'\s*,\s*EPropertyType::(\w+)'           # group 2: property type
    r'\s*,\s*&([\w.>\-\[\]]+)'              # group 3: member expression
    r'(?:\s*,\s*([\d.fFeE+\-]+))?'          # group 4: min  (optional)
    r'(?:\s*,\s*([\d.fFeE+\-]+))?'          # group 5: max  (optional)
    r'(?:\s*,\s*([\d.fFeE+\-]+))?'          # group 6: speed (optional)
    r'\s*\}\s*\)'
)

_GEP_FN_RE = re.compile(
    r'void\s+(\w+)::GetEditableProperties\s*\('
    r'TArray<FPropertyDescriptor>&\s*\w+'
    r'\)'
)


def _strip_comments(text: str) -> str:
    text = re.sub(r'/\*.*?\*/',
                  lambda m: '\n' * m.group().count('\n'), text, flags=re.DOTALL)
    text = re.sub(r'//[^\n]*', '', text)
    return text


def collect_cpp_props(path: str) -> dict:
    """Parse a .cpp file; return {ClassName: [PropEntry]}."""
    with open(path, encoding='utf-8', errors='replace') as f:
        raw = f.read()

    sc    = _strip_comments(raw)
    lines = sc.splitlines()
    result: dict[str, list[PropEntry]] = {}

    i = 0
    while i < len(lines):
        m = _GEP_FN_RE.search(lines[i])
        if m:
            cname = m.group(1)
            props: list[PropEntry] = []
            depth, found_open = 0, False
            j = i
            while j < len(lines):
                l      = lines[j]
                depth += l.count('{') - l.count('}')
                if '{' in l:
                    found_open = True

                pm = _PUSH_BACK_RE.search(l)
                if pm:
                    mexpr = pm.group(3)
                    # Skip sub-member access (RelativeTransform.Location, ptr->field)
                    if '.' not in mexpr and '->' not in mexpr:
                        props.append(PropEntry(
                            display_name=pm.group(1),
                            prop_type=pm.group(2),
                            member_name=mexpr,
                            min_val=(pm.group(4) or '').strip(),
                            max_val=(pm.group(5) or '').strip(),
                            speed_val=(pm.group(6) or '').strip(),
                        ))

                if found_open and depth <= 0:
                    i = j
                    break
                j += 1
            result[cname] = props
        i += 1

    return result


def scan_all_cpp_props(source_root: str) -> dict:
    """Walk source_root and return combined {ClassName: [PropEntry]} from all .cpp files."""
    all_props: dict[str, list[PropEntry]] = {}
    for dirpath, _, filenames in os.walk(source_root):
        for fname in filenames:
            if fname.endswith('.cpp') and '.generated.' not in fname:
                path = os.path.join(dirpath, fname)
                for cname, entries in collect_cpp_props(path).items():
                    all_props[cname] = entries
    return all_props


# ---------------------------------------------------------------------------
# Phase 2  — migrate .h files
# ---------------------------------------------------------------------------

_DECLARE_CLASS_RE  = re.compile(r'^(\s*)DECLARE_CLASS\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)')
_INCLUDE_RE        = re.compile(r'^\s*#\s*include\b')
_GEP_DECL_RE       = re.compile(r'^\s*virtual\s+void\s+GetEditableProperties\s*\(')


def _fproperty_line(entry: PropEntry, indent: str) -> str:
    parts: list[str] = []
    if entry.display_name != entry.member_name:
        parts.append(f'DisplayName="{entry.display_name}"')
    parts.append(f'Type={entry.prop_type}')
    if entry.min_val:
        parts.append(f'min={entry.min_val}')
    if entry.max_val:
        parts.append(f'max={entry.max_val}')
    if entry.speed_val:
        parts.append(f'speed={entry.speed_val}')
    return f'{indent}FPROPERTY({", ".join(parts)})'


def _is_field_decl(line: str, member: str) -> bool:
    """Heuristic: is this line a field declaration containing `member`?"""
    stripped = line.strip()
    # Must contain the member name as a whole word
    if not re.search(r'\b' + re.escape(member) + r'\b', stripped):
        return False
    # Must end with ; (possibly after = ... initializer)
    if not stripped.endswith(';'):
        return False
    # Must not be a function signature: no '(' before the member name on the same line
    # (initializers like "= FVector(1,2,3)" are OK — they come AFTER the name)
    before_member = stripped[:stripped.index(member)]
    if '(' in before_member:
        return False
    return True


def migrate_header(h_path: str,
                   props_map: dict,
                   keep_gep_decl: bool = False) -> Optional[str]:
    """
    Returns migrated source text, or None if no DECLARE_CLASS found.
    `keep_gep_decl=True` retains the GetEditableProperties virtual declaration
    (used only for ActorComponent.h, which is the base of the interface).
    """
    with open(h_path, encoding='utf-8', errors='replace') as f:
        original = f.read()

    if 'DECLARE_CLASS' not in original:
        return None

    filename = os.path.basename(h_path)
    stem     = os.path.splitext(filename)[0]
    gen_incl = f'#include "{stem}.generated.h"'

    # Already migrated?
    if 'GENERATED_BODY()' in original and gen_incl in original:
        return None

    lines = original.splitlines(keepends=True)

    # --- Collect DECLARE_CLASS positions and class names ---
    # key = line index (0-based), value = (class_name, parent_name, indent)
    declare_map: dict[int, tuple[str, str, str]] = {}
    for idx, line in enumerate(lines):
        m = _DECLARE_CLASS_RE.match(line.rstrip('\n\r'))
        if m:
            declare_map[idx] = (m.group(2), m.group(3), m.group(1))

    # No actual DECLARE_CLASS usage found (only #define macro definitions)
    if not declare_map:
        return None

    class_names = {info[0] for info in declare_map.values()}

    # --- Find class declaration lines (for UCLASS() insertion) ---
    # For each class name, find the line "class ClassName" or "struct ClassName"
    # that starts the full definition (has ':' public or '{' or next non-empty is '{')
    class_decl_lines: set[int] = set()
    for idx, line in enumerate(lines):
        stripped = line.strip()
        for cname in class_names:
            pat = re.compile(r'\b(?:class|struct)\s+' + re.escape(cname) + r'\b')
            if pat.search(stripped) and not stripped.endswith(';'):
                class_decl_lines.add(idx)

    # --- Find last #include line ---
    last_include_idx = -1
    for idx, line in enumerate(lines):
        if _INCLUDE_RE.match(line):
            last_include_idx = idx

    # --- Build per-member annotation lookup from props_map ---
    # member_name -> PropEntry  (for all classes declared in this file)
    annotations: dict[str, PropEntry] = {}
    for cname in class_names:
        for entry in props_map.get(cname, []):
            annotations[entry.member_name] = entry

    # --- Track which members have been annotated (to avoid duplicates) ---
    annotated: set[str] = set()

    # --- Build output ---
    out: list[str] = []
    gen_incl_added = gen_incl in original  # skip if already present

    for idx, line in enumerate(lines):
        stripped = line.rstrip('\n\r')

        # Insert UCLASS() before class declaration
        if idx in class_decl_lines:
            # Determine indentation from the class line
            lead = re.match(r'^(\s*)', stripped)
            indent = lead.group(1) if lead else ''
            out.append(f'{indent}UCLASS()\n')

        # After last #include, add generated.h include
        if idx == last_include_idx and not gen_incl_added:
            out.append(line)
            out.append(f'{gen_incl}\n')
            gen_incl_added = True
            continue

        # Replace DECLARE_CLASS with GENERATED_BODY
        m = _DECLARE_CLASS_RE.match(stripped)
        if m:
            indent = m.group(1)
            out.append(f'{indent}GENERATED_BODY()\n')
            continue

        # Remove GetEditableProperties override declaration (except base)
        if not keep_gep_decl and _GEP_DECL_RE.match(stripped):
            continue

        # Insert FPROPERTY annotation before matching field declarations
        for mname, entry in list(annotations.items()):
            if mname in annotated:
                continue
            if _is_field_decl(stripped, mname):
                lead = re.match(r'^(\s+)', stripped)
                fld_indent = lead.group(1) if lead else '\t'
                out.append(_fproperty_line(entry, fld_indent) + '\n')
                annotated.add(mname)
                break  # at most one FPROPERTY per line

        out.append(line)

    return ''.join(out)


# ---------------------------------------------------------------------------
# Phase 3  — migrate .cpp files
# ---------------------------------------------------------------------------

_IMPLEMENT_CLASS_RE  = re.compile(r'^(\s*)IMPLEMENT_CLASS\s*\(\s*(\w+)\s*,\s*\w+\s*\)')
_DEFINE_CLASS_RE     = re.compile(r'^(\s*)DEFINE_CLASS(?:_WITH_FLAGS)?\s*\(\s*\w+.*?\)')
_GEP_IMPL_RE         = re.compile(
    r'void\s+\w+::GetEditableProperties\s*\(TArray<FPropertyDescriptor>&\s*\w+\)'
)


def migrate_cpp(cpp_path: str) -> Optional[str]:
    """Returns migrated source text, or None if nothing to change."""
    with open(cpp_path, encoding='utf-8', errors='replace') as f:
        original = f.read()

    # Quick check — does this file need migration?
    has_work = (
        'IMPLEMENT_CLASS' in original
        or re.search(r'\bDEFINE_CLASS\b', original)
        or 'GetEditableProperties' in original
    )
    if not has_work:
        return None

    lines  = original.splitlines(keepends=True)
    out: list[str] = []
    i = 0

    while i < len(lines):
        line = lines[i]
        stripped = line.rstrip('\n\r')

        # IMPLEMENT_CLASS(X, Y)  →  REGISTER_FACTORY(X)
        m = _IMPLEMENT_CLASS_RE.match(stripped)
        if m:
            indent = m.group(1)
            cname  = m.group(2)
            out.append(f'{indent}REGISTER_FACTORY({cname})\n')
            i += 1
            continue

        # DEFINE_CLASS(X, Y) or DEFINE_CLASS_WITH_FLAGS(X, Y, F) → remove
        if _DEFINE_CLASS_RE.match(stripped):
            i += 1
            continue

        # Remove GetEditableProperties() function body
        if _GEP_IMPL_RE.search(stripped):
            # Skip the function signature line and entire brace-delimited body
            depth, found_open = 0, False
            j = i
            while j < len(lines):
                l      = lines[j]
                depth += l.count('{') - l.count('}')
                if '{' in l:
                    found_open = True
                if found_open and depth <= 0:
                    i = j + 1
                    break
                j += 1
            else:
                i = j + 1
            # Consume one trailing blank line to avoid excessive whitespace
            if i < len(lines) and lines[i].strip() == '':
                i += 1
            continue

        out.append(line)
        i += 1

    result = ''.join(out)

    # Collapse 3+ consecutive blank lines down to 2
    result = re.sub(r'\n{4,}', '\n\n\n', result)

    return result if result != original else None


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    script_dir  = os.path.dirname(os.path.abspath(__file__))
    repo_root   = os.path.dirname(script_dir)
    default_src = os.path.join(repo_root, 'KraftonEngine', 'Source')

    ap = argparse.ArgumentParser(
        description='Migrate KraftonEngine sources to UE-style UCLASS/GENERATED_BODY reflection.'
    )
    ap.add_argument('--source-root', default=default_src, metavar='PATH')
    ap.add_argument('--dry-run', action='store_true',
                    help='Print what would change without writing files')
    args = ap.parse_args()

    source_root = os.path.abspath(args.source_root)
    if not os.path.isdir(source_root):
        print(f'ERROR: source root not found: {source_root}', file=sys.stderr)
        sys.exit(1)

    dry = args.dry_run
    mode = 'dry-run' if dry else 'migrating'
    print(f'[MigrateReflection] {mode}  source-root={source_root}')

    # Phase 1: collect all properties
    print('\n--- Phase 1: collecting GetEditableProperties entries ---')
    props_map = scan_all_cpp_props(source_root)
    total_props = sum(len(v) for v in props_map.values())
    print(f'  found {len(props_map)} classes with {total_props} direct properties')

    # Phase 2: migrate headers
    print('\n--- Phase 2: migrating .h files ---')
    h_count = 0
    for dirpath, _, filenames in os.walk(source_root):
        for fname in sorted(filenames):
            if not fname.endswith('.h') or '.generated.' in fname:
                continue
            h_path = os.path.join(dirpath, fname)
            stem   = os.path.splitext(fname)[0]
            keep   = (stem == 'ActorComponent')
            result = migrate_header(h_path, props_map, keep_gep_decl=keep)
            if result is None:
                continue
            rel = os.path.relpath(h_path)
            if dry:
                print(f'  [h] {rel}')
            else:
                with open(h_path, 'w', encoding='utf-8') as f:
                    f.write(result)
                print(f'  wrote {rel}')
            h_count += 1

    # Phase 3: migrate cpp files
    print('\n--- Phase 3: migrating .cpp files ---')
    cpp_count = 0
    for dirpath, _, filenames in os.walk(source_root):
        for fname in sorted(filenames):
            if not fname.endswith('.cpp') or '.generated.' in fname:
                continue
            cpp_path = os.path.join(dirpath, fname)
            result   = migrate_cpp(cpp_path)
            if result is None:
                continue
            rel = os.path.relpath(cpp_path)
            if dry:
                print(f'  [cpp] {rel}')
            else:
                with open(cpp_path, 'w', encoding='utf-8') as f:
                    f.write(result)
                print(f'  wrote {rel}')
            cpp_count += 1

    print(f'\nDone. {h_count} header(s), {cpp_count} source(s) migrated.')

    if not dry:
        print('\nNext step: run  python Scripts/GenerateReflectionHeaders.py')


if __name__ == '__main__':
    main()
