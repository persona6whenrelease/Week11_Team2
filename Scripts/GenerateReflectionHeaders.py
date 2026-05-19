#!/usr/bin/env python3
"""
GenerateReflectionHeaders.py

Scans KraftonEngine/Source for C++ headers that use UE-style reflection macros:
    UCLASS(), USTRUCT(), UENUM(), FPROPERTY(), UFUNCTION(), GENERATED_BODY()

For each such header, generates a companion pair:
    <FileName>.generated.h   — GENERATED_BODY() macro definition (declarations only)
    <FileName>.generated.cpp — static member definitions (UClass, property descriptors, etc.)

Usage:
    python GenerateReflectionHeaders.py [--source-root PATH] [--dry-run] [files...]

Note: FPROPERTY is used instead of UPROPERTY in this codebase.
"""

import os
import re
import sys
import argparse
from dataclasses import dataclass, field
from typing import Optional


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------

@dataclass
class PropertyInfo:
    cpp_type: str
    name: str
    specifiers: str = ""
    meta: dict = field(default_factory=dict)   # e.g. {'min':'0.0f','max':'50.0f','DisplayName':'"Cast Shadows"'}


@dataclass
class FunctionInfo:
    return_type: str
    name: str
    params: str
    specifiers: str = ""


@dataclass
class EnumValueInfo:
    name: str
    value: Optional[str] = None


@dataclass
class ReflectedType:
    kind: str               # "class" | "struct" | "enum"
    name: str
    parent: Optional[str] = None
    specifiers: str = ""
    properties: list = field(default_factory=list)
    functions: list = field(default_factory=list)
    enum_values: list = field(default_factory=list)
    generated_body_line: int = -1   # 1-indexed line of GENERATED_BODY() in source


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

class HeaderParser:
    """
    Line-by-line + regex parser for C++ headers with UE-style reflection macros.
    Handles the common patterns found in KraftonEngine; not a full C++ parser.
    """

    _ANNOTATION_RE      = re.compile(r'\b(UCLASS|USTRUCT|UENUM|FPROPERTY|UFUNCTION)\s*\(([^)]*)\)')
    _CLASS_DECL_RE      = re.compile(
        r'\b(?:class|struct)\s+(\w+)'
        r'(?:\s*:\s*(?:public|protected|private)\s+(\w+))?'
    )
    _ENUM_DECL_RE       = re.compile(r'\benum\b(?:\s+class)?\s+(\w+)')
    _PROPERTY_RE        = re.compile(r'^([\w:_<>\*&\s,]+?)\s+(\w+)\s*(?:=.*)?;')
    _FUNCTION_RE        = re.compile(r'^([\w:_<>\*&\s]+?)\s+(\w+)\s*\(([^)]*)\)')
    _ENUM_VALUE_RE      = re.compile(r'^\s*(\w+)\s*(?:=\s*([^,\n/]+))?\s*,?')
    _GENERATED_BODY_RE  = re.compile(r'^\s*GENERATED_BODY\s*\(\s*\)')

    def parse(self, content: str) -> list:
        # Strip comments for parsing logic; line indices stay 1-to-1 with original
        stripped = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
        stripped = re.sub(r'//[^\n]*', '', stripped)
        lines = stripped.splitlines()

        results: list[ReflectedType] = []
        i = 0
        while i < len(lines):
            m = self._ANNOTATION_RE.search(lines[i])
            if m:
                keyword    = m.group(1)
                specifiers = m.group(2).strip()

                if keyword == 'UCLASS':
                    t = self._parse_class_body(lines, i + 1, 'class', specifiers)
                    if t:
                        results.append(t)
                elif keyword == 'USTRUCT':
                    t = self._parse_class_body(lines, i + 1, 'struct', specifiers)
                    if t:
                        results.append(t)
                elif keyword == 'UENUM':
                    t = self._parse_enum_body(lines, i + 1, specifiers)
                    if t:
                        results.append(t)
                # FPROPERTY / UFUNCTION inside a class are handled by _parse_class_body
            i += 1

        return results

    # ---- helpers -----------------------------------------------------------

    def _next_nonempty(self, lines: list, start: int):
        for j in range(start, len(lines)):
            if lines[j].strip():
                return j, lines[j]
        return -1, ''

    def _parse_class_body(self, lines: list, start: int, kind: str, specifiers: str):
        idx, line = self._next_nonempty(lines, start)
        if idx < 0:
            return None
        m = self._CLASS_DECL_RE.search(line)
        if not m:
            return None

        t = ReflectedType(
            kind=kind,
            name=m.group(1),
            parent=m.group(2),
            specifiers=specifiers,
        )

        # Walk the class body collecting FPROPERTY / UFUNCTION annotations
        brace_depth    = 0
        found_open     = False
        expect_prop    = False
        expect_func    = False
        pending_spec   = ""

        for j in range(idx, len(lines)):
            l = lines[j]
            opens  = l.count('{')
            closes = l.count('}')
            brace_depth += opens - closes

            if opens > 0:
                found_open = True

            if found_open and brace_depth <= 0:
                break   # end of class body

            # Track GENERATED_BODY() line number (1-indexed, from original file)
            if t.generated_body_line < 0 and self._GENERATED_BODY_RE.match(l):
                t.generated_body_line = j + 1
                continue

            ann = self._ANNOTATION_RE.search(l)
            if ann:
                if ann.group(1) == 'FPROPERTY':
                    expect_prop  = True
                    pending_spec = ann.group(2).strip()
                elif ann.group(1) == 'UFUNCTION':
                    expect_func  = True
                    pending_spec = ann.group(2).strip()
                continue

            stripped = l.strip()
            if not stripped:
                continue

            if expect_prop:
                prop = self._try_parse_property(stripped, pending_spec)
                if prop:
                    t.properties.append(prop)
                expect_prop  = False
                pending_spec = ""
                continue

            if expect_func:
                func = self._try_parse_function(stripped, pending_spec)
                if func:
                    t.functions.append(func)
                expect_func  = False
                pending_spec = ""
                continue

        return t

    def _parse_enum_body(self, lines: list, start: int, specifiers: str):
        idx, line = self._next_nonempty(lines, start)
        if idx < 0:
            return None
        m = self._ENUM_DECL_RE.search(line)
        if not m:
            return None

        t = ReflectedType(kind='enum', name=m.group(1), specifiers=specifiers)

        brace_depth = 0
        in_body     = False
        for j in range(idx, len(lines)):
            l = lines[j]
            brace_depth += l.count('{') - l.count('}')

            if '{' in l:
                in_body = True

            if in_body and brace_depth == 1:
                stripped = l.strip().lstrip('{').strip()
                vm = self._ENUM_VALUE_RE.match(stripped)
                if vm:
                    name = vm.group(1)
                    # Skip keywords / empty tokens
                    if name and name not in ('enum', 'class', 'struct', ''):
                        val_expr = vm.group(2).strip() if vm.group(2) else None
                        t.enum_values.append(EnumValueInfo(name, val_expr))

            if in_body and brace_depth <= 0:
                break

        return t

    @staticmethod
    def _parse_specifiers(spec_str: str) -> dict:
        """Parse 'min=0.0, max=50.0, DisplayName="Cast Shadows"' into a dict.
        Standalone keywords like 'ReadOnly', 'Hidden', 'Transient' are stored as True."""
        meta: dict = {}
        if not spec_str.strip():
            return meta
        # Quote-aware split: don't break on commas inside "..."
        parts, cur, in_q = [], [], False
        for ch in spec_str:
            if ch == '"':
                in_q = not in_q
                cur.append(ch)
            elif ch == ',' and not in_q:
                parts.append(''.join(cur).strip())
                cur = []
            else:
                cur.append(ch)
        if cur:
            parts.append(''.join(cur).strip())
        for part in parts:
            kv = re.match(r'(\w+)\s*=\s*(.+)', part)
            if kv:
                key = kv.group(1).strip()
                val = kv.group(2).strip()
                # Wrap bare numeric values with 'f' suffix for C++ float literals
                if re.match(r'^-?\d+\.?\d*$', val):
                    val = val + 'f'
                meta[key] = val
            elif re.match(r'^\w+$', part):
                # Standalone keyword: FPROPERTY(ReadOnly) → meta['ReadOnly'] = True
                meta[part] = True
        return meta

    def _try_parse_property(self, stripped: str, specifiers: str) -> Optional[PropertyInfo]:
        m = self._PROPERTY_RE.match(stripped)
        if not m:
            return None
        cpp_type = m.group(1).strip()
        name     = m.group(2).strip()
        # Reject obvious non-members: keywords, macros, etc.
        if cpp_type in ('return', 'delete', 'if', 'while', 'for', 'using') or '{' in cpp_type:
            return None
        meta = self._parse_specifiers(specifiers)
        return PropertyInfo(cpp_type=cpp_type, name=name, specifiers=specifiers, meta=meta)

    def _try_parse_function(self, stripped: str, specifiers: str) -> Optional[FunctionInfo]:
        m = self._FUNCTION_RE.match(stripped)
        if not m:
            return None
        ret  = m.group(1).strip()
        name = m.group(2).strip()
        params = m.group(3).strip()
        if ret in ('return', 'if', 'while', 'for') or '(' in ret:
            return None
        return FunctionInfo(return_type=ret, name=name, params=params, specifiers=specifiers)


# ---------------------------------------------------------------------------
# EClassFlags mapping
# ---------------------------------------------------------------------------

_SPECIFIER_TO_FLAG = {
    'Actor':                 'CF_Actor',
    'Component':             'CF_Component',
    'Camera':                'CF_Camera',
    'HiddenInComponentList': 'CF_HiddenInComponentList',
}


def _specifiers_to_flags(specifiers: str) -> str:
    parts = [s.strip() for s in specifiers.split(',') if s.strip()]
    flags = [_SPECIFIER_TO_FLAG[p] for p in parts if p in _SPECIFIER_TO_FLAG]
    return ' | '.join(flags) if flags else 'CF_None'


# ---------------------------------------------------------------------------
# EPropertyType mapping
# ---------------------------------------------------------------------------

_CPP_TO_EPROPERTY = {
    'bool':        'EPropertyType::Bool',
    'uint8':       'EPropertyType::ByteBool',
    'uint8_t':     'EPropertyType::ByteBool',
    'int':         'EPropertyType::Int',
    'int32':       'EPropertyType::Int',
    'int32_t':     'EPropertyType::Int',
    'uint32':      'EPropertyType::Int',
    'uint32_t':    'EPropertyType::Int',
    'float':       'EPropertyType::Float',
    'double':      'EPropertyType::Float',
    'FVector':     'EPropertyType::Vec3',
    'FVector3':    'EPropertyType::Vec3',
    'FVector4':    'EPropertyType::Vec4',
    'FRotator':    'EPropertyType::Rotator',
    'FString':     'EPropertyType::String',
    'std::string': 'EPropertyType::String',
    'FName':       'EPropertyType::Name',
    'FColor':      'EPropertyType::Color4',
}

def _cpp_type_to_eproperty(cpp_type: str, reflected_types: Optional[dict[str, str]] = None) -> str:
    clean = cpp_type.replace('const', '').replace('*', '').replace('&', '').strip()
    if reflected_types:
        kind = reflected_types.get(clean)
        if kind == 'struct':
            return 'EPropertyType::Struct'
        if kind == 'class':
            return 'EPropertyType::ObjectRef'
        if kind == 'enum':
            return 'EPropertyType::Enum'
    return _CPP_TO_EPROPERTY.get(clean, 'EPropertyType::Int')


def _parse_tarray_inner_type(cpp_type: str) -> Optional[str]:
    clean = cpp_type.replace('const', '').replace('&', '').strip()
    m = re.match(r'^TArray\s*<\s*(.+)\s*>$', clean)
    if not m:
        return None
    return m.group(1).strip()


def _parse_tset_inner_type(cpp_type: str) -> Optional[str]:
    clean = cpp_type.replace('const', '').replace('&', '').strip()
    m = re.match(r'^TSet\s*<\s*(.+)\s*>$', clean)
    return m.group(1).strip() if m else None


def _parse_tmap_key_value(cpp_type: str) -> Optional[tuple]:
    clean = cpp_type.replace('const', '').replace('&', '').strip()
    m = re.match(r'^TMap\s*<(.+)>$', clean)
    if not m:
        return None
    inner = m.group(1)
    depth, split = 0, -1
    for i, c in enumerate(inner):
        if c == '<':
            depth += 1
        elif c == '>':
            depth -= 1
        elif c == ',' and depth == 0:
            split = i
            break
    if split < 0:
        return None
    return inner[:split].strip(), inner[split + 1:].strip()


def _generate_type_node(
    out: list,
    node_prefix: str,
    cpp_type: str,
    reflected_types: dict,
    depth: int = 0,
) -> str:
    """Emit static FPropertyTypeDesc node(s) into *out* and return a C++ expression
    that evaluates to ``const FPropertyTypeDesc*`` for the given cpp_type.

    Supports recursive TArray nesting up to depth 4.
    """
    if depth > 4:
        return "GetBuiltinPropertyType(EPropertyType::Int)"

    ct = cpp_type.strip()

    # --- TArray<T> (recursive) ---
    inner = _parse_tarray_inner_type(ct)
    if inner:
        inner_ref = _generate_type_node(out, node_prefix + "_Element", inner, reflected_types, depth + 1)
        node = node_prefix
        out.append(
            f"    static const FPropertyTypeDesc {node}"
            f"{{ EPropertyType::Array, nullptr, nullptr, 0, nullptr,"
            f" &TArrayPropertyOps<{ct}>::GetSize,"
            f" &TArrayPropertyOps<{ct}>::Resize,"
            f" &TArrayPropertyOps<{ct}>::GetElement,"
            f" &TArrayPropertyOps<{ct}>::GetConstElement,"
            f" {inner_ref}, nullptr, nullptr }};"
        )
        return f"&{node}"

    # --- TSet<T> ---
    set_inner = _parse_tset_inner_type(ct)
    if set_inner is not None:
        inner_ref = _generate_type_node(out, node_prefix + "_Element", set_inner, reflected_types, depth + 1)
        node = node_prefix
        out.append(
            f"    static const FPropertyTypeDesc {node}"
            f"{{ EPropertyType::Set, nullptr, nullptr, 0, nullptr,"
            f" nullptr, nullptr, nullptr, nullptr, {inner_ref}, nullptr, nullptr,"
            f" &TSetPropertyOps<{ct}>::GetSize,"
            f" &TSetPropertyOps<{ct}>::Insert,"
            f" &TSetPropertyOps<{ct}>::Remove,"
            f" &TSetPropertyOps<{ct}>::Clear,"
            f" &TSetPropertyOps<{ct}>::Snapshot,"
            f" &TSetPropertyOps<{ct}>::ConstSnapshot,"
            f" &TSetPropertyOps<{ct}>::ElementSize }};"
        )
        return f"&{node}"

    # --- TMap<K,V> ---
    map_kv = _parse_tmap_key_value(ct)
    if map_kv is not None:
        key_type, val_type = map_kv
        key_ref = _generate_type_node(out, node_prefix + "_Key", key_type, reflected_types, depth + 1)
        val_ref = _generate_type_node(out, node_prefix + "_Val", val_type, reflected_types, depth + 1)
        node = node_prefix
        out.append(
            f"    static const FPropertyTypeDesc {node}"
            f"{{ EPropertyType::Map, nullptr, nullptr, 0, nullptr,"
            f" nullptr, nullptr, nullptr, nullptr, nullptr, {key_ref}, {val_ref},"
            f" nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,"
            f" &TMapPropertyOps<{ct}>::GetSize,"
            f" &TMapPropertyOps<{ct}>::Clear,"
            f" &TMapPropertyOps<{ct}>::Snapshot,"
            f" &TMapPropertyOps<{ct}>::ConstSnapshot,"
            f" &TMapPropertyOps<{ct}>::Insert,"
            f" &TMapPropertyOps<{ct}>::Remove,"
            f" &TMapPropertyOps<{ct}>::KeySize,"
            f" &TMapPropertyOps<{ct}>::ValueSize }};"
        )
        return f"&{node}"

    etype = _cpp_type_to_eproperty(cpp_type, reflected_types)

    if etype == 'EPropertyType::Enum':
        clean = _clean_cpp_type(cpp_type)
        node = node_prefix
        out.append(
            f"    static const FPropertyTypeDesc {node}"
            f"{{ EPropertyType::Enum, nullptr, {clean}_EnumNames, (uint32)GetEnumCount_{clean}(), nullptr,"
            f" nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }};"
        )
        return f"&{node}"

    if etype == 'EPropertyType::Struct':
        clean = _clean_cpp_type(cpp_type)
        node = node_prefix
        out.append(
            f"    static const FPropertyTypeDesc {node}"
            f"{{ EPropertyType::Struct, &{clean}::StaticClassInstance, nullptr, 0, nullptr,"
            f" nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }};"
        )
        return f"&{node}"

    if etype == 'EPropertyType::ObjectRef':
        clean = _clean_cpp_type(cpp_type)
        node = node_prefix
        out.append(
            f"    static const FPropertyTypeDesc {node}"
            f"{{ EPropertyType::ObjectRef, nullptr, nullptr, 0, &{clean}::StaticClassInstance,"
            f" nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }};"
        )
        return f"&{node}"

    return f"GetBuiltinPropertyType({etype})"


def _clean_cpp_type(cpp_type: str) -> str:
    return cpp_type.replace('const', '').replace('*', '').replace('&', '').strip()


def _resolve_object_ref_class(meta: dict) -> Optional[str]:
    object_class = meta.get('Class')
    if object_class:
        return _clean_cpp_type(object_class)
    return None


# ---------------------------------------------------------------------------
# .generated.h writer
# ---------------------------------------------------------------------------

_BANNER = (
    "// AUTO-GENERATED FILE - DO NOT EDIT.\n"
    "// Generated by Scripts/GenerateReflectionHeaders.py\n"
)


def _filename_to_file_id(filename: str) -> str:
    """'AActor.h' → 'AActor_h'  (used as CURRENT_FILE_ID prefix)."""
    return re.sub(r'[^A-Za-z0-9]', '_', os.path.splitext(filename)[0] + "_h")


def _build_generated_h(types: list[ReflectedType], source_filename: str) -> str:
    file_id = _filename_to_file_id(source_filename)

    out = [
        _BANNER,
        "#pragma once",
        '#include "Object/ReflectionMacros.h"',
        "#include <vector>",
        "",
        # Set CURRENT_FILE_ID for this file so GENERATED_BODY() dispatches correctly
        f"#undef  CURRENT_FILE_ID",
        f"#define CURRENT_FILE_ID {file_id}_LINE_",
        "",
    ]

    # UFunctionInfo guard (only needed if any class has functions)
    if any(t.functions for t in types if t.kind != 'enum'):
        out += [
            "#ifndef UFUNCTION_INFO_DEFINED",
            "#define UFUNCTION_INFO_DEFINED",
            "struct UFunctionInfo { const char* Name; const char* Signature; };",
            "#endif",
            "",
        ]

    for t in types:
        if t.kind == 'enum':
            _h_enum(out, t)
        else:
            _h_class(out, t, file_id)

    return "\n".join(out) + "\n"


def _h_class(out: list, t: ReflectedType, file_id: str):
    has_props = bool(t.properties)
    has_funcs = bool(t.functions)

    out.append(f"// ---- {t.name} ----")

    # Build the macro body
    body: list[str] = []

    if t.parent and t.kind == 'class':
        body.append(f"    using Super = {t.parent};")

    body += [
        f"    static UClass StaticClassInstance;",
        f"    static FClassRegistrar s_Registrar;",
        f"    static UClass* StaticClass() {{ return &StaticClassInstance; }}",
    ]

    if t.kind == 'class':
        body.append("    virtual UClass* GetClass() const override { return StaticClass(); }")

    if has_props:
        body.append("    static const std::vector<FPropertyDescriptor>& GetReflectedProperties();")

    if has_funcs:
        body.append("    static const std::vector<UFunctionInfo>& GetReflectedFunctions();")

    # Emit a per-line-number macro: FILE_ID_LINE_N  (N = generated_body_line)
    # If line number not yet known (pre-migration file with UCLASS but no GENERATED_BODY
    # in source yet), fall back to emitting the old #undef approach for compatibility.
    if t.generated_body_line > 0:
        macro_name = f"{file_id}{t.generated_body_line}"
    else:
        # Fallback: define as a named macro, not line-number keyed.
        # This happens when the generator is run on a header that still has DECLARE_CLASS
        # (i.e., hasn't been migrated yet).  Use the old single-define approach.
        macro_name = None

    if macro_name:
        out.append(f"#define {macro_name} \\")
    else:
        out += ["#ifdef GENERATED_BODY", "#undef GENERATED_BODY", "#endif", ""]
        out.append("#define GENERATED_BODY() \\")

    for i, line in enumerate(body):
        suffix = " \\" if i < len(body) - 1 else ""
        out.append(f"{line}{suffix}")

    out.append("")


def _h_enum(out: list, t: ReflectedType):
    names_list = ", ".join(f'"{ev.name}"' for ev in t.enum_values)
    out += [
        f"// ---- {t.name} ----",
        f"const char* GetEnumName_{t.name}({t.name} Value);",
        f"inline int   GetEnumCount_{t.name}() {{ return {len(t.enum_values)}; }}",
        f"inline const char* {t.name}_EnumNames[] = {{ {names_list} }};",
        "",
    ]


# ---------------------------------------------------------------------------
# .generated.cpp writer
# ---------------------------------------------------------------------------

def _build_generated_cpp(types: list[ReflectedType], source_include: str, reflected_types: Optional[dict[str, str]] = None) -> str:
    out = [
        _BANNER,
        f'#include "{source_include}"',
        '#include "Core/PropertyTypes.h"',
        "#include <cstddef>",     # offsetof
        "#include <vector>",
        "",
    ]

    for t in types:
        if t.kind == 'enum':
            _cpp_enum(out, t)
        else:
            _cpp_class(out, t, reflected_types or {})

    return "\n".join(out) + "\n"


# EPropertyType 값 중 GetTypeHash 표현식을 알 수 있는 것들
_HASHABLE_ETYPES = {
    'EPropertyType::Bool':   lambda name: f"GetTypeHash(v.{name})",
    'EPropertyType::ByteBool': lambda name: f"GetTypeHash(static_cast<uint8>(v.{name}))",
    'EPropertyType::Int':    lambda name: f"GetTypeHash(v.{name})",
    'EPropertyType::Float':  lambda name: f"GetTypeHash(v.{name})",
    'EPropertyType::Vec3':   lambda name: f"GetTypeHash(v.{name})",
    'EPropertyType::Vec4':   lambda name: f"GetTypeHash(v.{name})",
    'EPropertyType::Rotator':lambda name: f"GetTypeHash(v.{name})",
    'EPropertyType::Color4': lambda name: f"GetTypeHash(v.{name})",
    'EPropertyType::String': lambda name: f"GetTypeHash(v.{name})",
    'EPropertyType::Name':   lambda name: f"GetTypeHash(v.{name}.ToString())",
    'EPropertyType::Enum':   lambda name: f"GetTypeHash(static_cast<int32>(v.{name}))",
}


def _cpp_emit_get_type_hash(out: list, t: ReflectedType, reflected_types: dict):
    """Emit a free-function GetTypeHash(const T&) for a USTRUCT.

    Combines all hashable scalar properties via HashCombine.
    Properties whose types are not trivially hashable (Vec3, Struct, Array …)
    are skipped — they still contribute to equality but not to the hash, which
    is safe (only violates the *better-distribution* contract, not correctness).
    """
    lines = []
    for prop in t.properties:
        etype = _cpp_type_to_eproperty(prop.cpp_type, reflected_types)
        expr_fn = _HASHABLE_ETYPES.get(etype)
        if expr_fn:
            lines.append(f"    seed = HashCombine(seed, {expr_fn(prop.name)});")

    out += [
        f"size_t GetTypeHash(const {t.name}& v)",
        "{",
        "    size_t seed = 0;",
    ]
    if lines:
        out += lines
    else:
        # No hashable fields — return a constant; still valid for unordered containers.
        out.append(f'    (void)v;  // no hashable fields in {t.name}')
    out += [
        "    return seed;",
        "}",
        "",
    ]


def _cpp_class(out: list, t: ReflectedType, reflected_types: dict[str, str]):
    flags = _specifiers_to_flags(t.specifiers)

    if t.parent:
        parent_ref = f"&{t.parent}::StaticClassInstance"
    elif t.kind == 'class':
        # UCLASS with no explicit parent — default to UObject
        parent_ref = "&UObject::StaticClassInstance"
    else:
        # USTRUCT with no parent — no super-class in the reflection chain
        parent_ref = "nullptr"

    out += [
        f"// ---- {t.name} ----",
        f"UClass {t.name}::StaticClassInstance(",
        f'    "{t.name}",',
        f"    {parent_ref},",
        f"    sizeof({t.name}),",
        f"    {flags}",
        f");",
        f"FClassRegistrar {t.name}::s_Registrar(&{t.name}::StaticClassInstance);",
        "",
    ]

    if t.properties:
        out += [
            f"const std::vector<FPropertyDescriptor>& {t.name}::GetReflectedProperties() {{",
            "    // Static type nodes mirror the reflected type graph and keep metadata address-stable.",
        ]
        prop_infos = []
        for index, prop in enumerate(t.properties):
            inner_type = _parse_tarray_inner_type(prop.cpp_type)
            type_desc_ref = None

            if 'Type' in prop.meta:
                etype = f"EPropertyType::{prop.meta['Type']}"
                object_ref_class = _resolve_object_ref_class(prop.meta)
                if etype == 'EPropertyType::Struct':
                    clean_type = _clean_cpp_type(prop.cpp_type)
                    node_name = f"{t.name}_PropType_{index}"
                    out.append(
                        f"    static const FPropertyTypeDesc {node_name}"
                        f"{{ {etype}, &{clean_type}::StaticClassInstance, nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }};"
                    )
                    type_desc_ref = f"&{node_name}"
                elif object_ref_class:
                    node_name = f"{t.name}_PropType_{index}"
                    out.append(
                        f"    static const FPropertyTypeDesc {node_name}"
                        f"{{ EPropertyType::ObjectRef, nullptr, nullptr, 0, &{object_ref_class}::StaticClassInstance, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }};"
                    )
                    type_desc_ref = f"&{node_name}"
                elif etype == 'EPropertyType::Enum':
                    clean_enum = _clean_cpp_type(prop.cpp_type)
                    node_name = f"{t.name}_PropType_{index}"
                    out.append(
                        f"    static const FPropertyTypeDesc {node_name}"
                        f"{{ {etype}, nullptr, {clean_enum}_EnumNames, (uint32)GetEnumCount_{clean_enum}(), nullptr,"
                        f" nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }};"
                    )
                    type_desc_ref = f"&{node_name}"
                else:
                    type_desc_ref = f"GetBuiltinPropertyType({etype})"
            elif inner_type:
                # Delegate to recursive helper — supports TArray<Enum>, TArray<TArray<T>>, etc.
                outer_node_name = f"{t.name}_PropType_{index}"
                inner_ref = _generate_type_node(
                    out, outer_node_name + "_Element", inner_type, reflected_types
                )
                out.append(
                    f"    static const FPropertyTypeDesc {outer_node_name}"
                    f"{{ EPropertyType::Array, nullptr, nullptr, 0, nullptr,"
                    f" &TArrayPropertyOps<{prop.cpp_type}>::GetSize,"
                    f" &TArrayPropertyOps<{prop.cpp_type}>::Resize,"
                    f" &TArrayPropertyOps<{prop.cpp_type}>::GetElement,"
                    f" &TArrayPropertyOps<{prop.cpp_type}>::GetConstElement,"
                    f" {inner_ref}, nullptr, nullptr }};"
                )
                type_desc_ref = f"&{outer_node_name}"
            else:
                type_desc_ref = _generate_type_node(
                    out, f"{t.name}_PropType_{index}", prop.cpp_type, reflected_types
                )

            prop_infos.append((prop, type_desc_ref))

        out += [
            "    static std::vector<FPropertyDescriptor> Props = {",
        ]
        for prop, type_desc_ref in prop_infos:
            offset    = f"reinterpret_cast<void*>(offsetof({t.name}, {prop.name}))"
            min_val   = prop.meta.get('min',   '0.0f')
            max_val   = prop.meta.get('max',   '0.0f')
            speed_val = prop.meta.get('speed', '0.1f')
            cat_val   = prop.meta.get('Category', '"Default"')
            disp_name = prop.meta.get('DisplayName', f'"{prop.name}"')
            tooltip_val = prop.meta.get('Tooltip', '""')
            flag_parts = []
            if 'ReadOnly'  in prop.meta: flag_parts.append('EPF_ReadOnly')
            if 'Hidden'    in prop.meta: flag_parts.append('EPF_Hidden')
            if 'Transient' in prop.meta: flag_parts.append('EPF_Transient')
            flags_val = ' | '.join(flag_parts) if flag_parts else 'EPF_None'
            out.append(
                f'        FPropertyDescriptor{{ {disp_name}, {offset},'
                f' {min_val}, {max_val}, {speed_val}, {cat_val}, {tooltip_val}, {flags_val}, {type_desc_ref} }},'
            )
        out += [
            "    };",
            "    return Props;",
            "}",
            "",
            # Register the getter in UClass so ActorComponent can walk the chain
            f"static bool {t.name}_PropsRegistered = []() {{",
            f"    {t.name}::StaticClassInstance.SetPropertiesGetter(",
            f"        &{t.name}::GetReflectedProperties);",
            f"    return true;",
            f"}}();",
            "",
        ]

    # Emit GetTypeHash for USTRUCT so it can be used as TSet element / TMap key.
    # Only struct kinds get this — classes are reference types, not value-hashed.
    if t.kind == 'struct':
        _cpp_emit_get_type_hash(out, t, reflected_types)

    if t.functions:
        out += [
            f"const std::vector<UFunctionInfo>& {t.name}::GetReflectedFunctions() {{",
            "    static std::vector<UFunctionInfo> Funcs = {",
        ]
        for func in t.functions:
            sig = f"{func.return_type} {func.name}({func.params})"
            out.append(f'        UFunctionInfo{{ "{func.name}", "{sig}" }},')
        out += [
            "    };",
            "    return Funcs;",
            "}",
            "",
        ]


def _cpp_enum(out: list, t: ReflectedType):
    out += [
        f"// ---- {t.name} ----",
        f"const char* GetEnumName_{t.name}({t.name} Value) {{",
        "    switch (Value) {",
    ]
    for ev in t.enum_values:
        out.append(f"        case {t.name}::{ev.name}: return \"{ev.name}\";")
    out += [
        "        default: return \"Unknown\";",
        "    }",
        "}",
        "",
    ]


# ---------------------------------------------------------------------------
# File-level processing
# ---------------------------------------------------------------------------

def _compute_include(h_path: str, source_root: str) -> str:
    """Return forward-slash relative path from source_root to h_path."""
    return os.path.relpath(h_path, source_root).replace("\\", "/")


def _content_changed(path: str, new_content: str) -> bool:
    if not os.path.exists(path):
        return True
    with open(path, encoding="utf-8") as f:
        return f.read() != new_content


def _source_newer(src: str, gen: str) -> bool:
    return not os.path.exists(gen) or os.path.getmtime(src) > os.path.getmtime(gen)


def process_header(h_path: str, source_root: str, dry_run: bool = False, reflected_types: Optional[dict[str, str]] = None) -> bool:
    """
    Parse h_path; generate .generated.h/.generated.cpp if reflection macros found.
    Returns True when files were (or would be) written.
    """
    with open(h_path, encoding="utf-8", errors="replace") as f:
        content = f.read()

    # Fast early-out: skip files with no reflection macros at all
    if not re.search(r'\b(UCLASS|USTRUCT|UENUM)\b', content):
        return False

    types = HeaderParser().parse(content)
    if not types:
        return False

    base         = os.path.splitext(h_path)[0]
    gen_h_path   = base + ".generated.h"
    gen_cpp_path = base + ".generated.cpp"
    src_include  = _compute_include(h_path, source_root)

    gen_h_content   = _build_generated_h(types, os.path.basename(h_path))
    gen_cpp_content = _build_generated_cpp(types, src_include, reflected_types)

    if dry_run:
        rel = os.path.relpath(h_path)
        print(f"  [dry-run] {rel}  →  .generated.h + .generated.cpp")
        for t in types:
            print(f"    {t.kind:6s} {t.name}"
                  + (f" : {t.parent}" if t.parent else "")
                  + (f"  props={len(t.properties)}" if t.properties else "")
                  + (f"  funcs={len(t.functions)}" if t.functions else "")
                  + (f"  values={len(t.enum_values)}" if t.enum_values else ""))
        return True

    wrote = False

    if _source_newer(h_path, gen_h_path) or _content_changed(gen_h_path, gen_h_content):
        with open(gen_h_path, "w", encoding="utf-8") as f:
            f.write(gen_h_content)
        print(f"  wrote  {os.path.relpath(gen_h_path)}")
        wrote = True

    if _source_newer(h_path, gen_cpp_path) or _content_changed(gen_cpp_path, gen_cpp_content):
        with open(gen_cpp_path, "w", encoding="utf-8") as f:
            f.write(gen_cpp_content)
        print(f"  wrote  {os.path.relpath(gen_cpp_path)}")
        wrote = True

    return wrote


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    script_dir   = os.path.dirname(os.path.abspath(__file__))
    repo_root    = os.path.dirname(script_dir)
    default_src  = os.path.join(repo_root, "KraftonEngine", "Source")

    ap = argparse.ArgumentParser(
        description="Generate .generated.h / .generated.cpp for UE-style reflection macros."
    )
    ap.add_argument(
        "--source-root", default=default_src, metavar="PATH",
        help=f"Source directory to scan (default: {default_src})",
    )
    ap.add_argument(
        "--dry-run", action="store_true",
        help="Print what would be generated without writing any files",
    )
    ap.add_argument(
        "files", nargs="*",
        help="Specific .h files to process (default: scan all of --source-root)",
    )
    args = ap.parse_args()

    source_root = os.path.abspath(args.source_root)
    if not os.path.isdir(source_root):
        print(f"ERROR: source root not found: {source_root}", file=sys.stderr)
        sys.exit(1)

    if args.files:
        h_files = [os.path.abspath(f) for f in args.files]
    else:
        h_files = []
        for dirpath, _, filenames in os.walk(source_root):
            for fname in filenames:
                # Skip already-generated files
                if fname.endswith(".h") and ".generated." not in fname:
                    h_files.append(os.path.join(dirpath, fname))

    mode = "dry-run" if args.dry_run else "generating"
    print(f"[GenerateReflectionHeaders] {mode} - scanning {len(h_files)} header(s)")

    generated_count = 0
    reflected_types: dict[str, str] = {}
    for h_path in h_files:
        with open(h_path, encoding="utf-8", errors="replace") as f:
            content = f.read()
        if not re.search(r'\b(UCLASS|USTRUCT|UENUM)\b', content):
            continue
        for t in HeaderParser().parse(content):
            reflected_types[t.name] = t.kind

    for h_path in sorted(h_files):
        if process_header(h_path, source_root, dry_run=args.dry_run, reflected_types=reflected_types):
            generated_count += 1

    print(f"\nDone. {generated_count} header(s) produced generated files.")


if __name__ == "__main__":
    main()
