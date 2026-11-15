#!/usr/bin/env python3

import json
import glob
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(ROOT, ".."))
DEFAULT_CLANG = (
    os.environ.get("CLANG_BIN") or shutil.which("clang") or
    "/opt/homebrew/opt/llvm/bin/clang"
)
ARCH_PLUGIN = os.path.join(
    ROOT, "build-" + platform.machine().lower(), "TransformType.so")
DEFAULT_PLUGIN = os.environ.get("TRANSFORM_TYPE_PLUGIN") or (
    ARCH_PLUGIN if os.path.exists(ARCH_PLUGIN) else
    os.path.join(ROOT, "TransformType.so"))
DEFAULT_OUT_DIR = os.path.join(ROOT, "out")
GSL_INCLUDE = "/opt/homebrew/opt/gsl/include"
GSL_LIB = "/opt/homebrew/opt/gsl/lib"


def _parse_bool(value):
    if isinstance(value, bool):
        return value
    if value is None:
        return True
    return str(value).lower() in ("1", "true", "yes", "on")


def _split_csv(value):
    if not value:
        return []
    items = []
    for part in str(value).split(","):
        part = part.strip()
        if part:
            items.append(part)
    return items


def _expand_globs(values):
    expanded = []
    for value in values:
        for item in _split_csv(value):
            if os.path.isdir(item):
                matches = glob.glob(os.path.join(item, "*.c"))
                if not matches:
                    raise ValueError(f"--src directory has no .c files: {item}")
                expanded.extend(sorted(matches))
                continue
            matches = glob.glob(item)
            if matches:
                expanded.extend(sorted(matches))
            else:
                expanded.append(item)
    return expanded


def _dedupe_preserve_order(items):
    result = []
    seen = set()
    for item in items:
        key = os.path.abspath(item)
        if key in seen:
            continue
        seen.add(key)
        result.append(item)
    return result


def _config_source_basenames(value):
    names = set()
    if isinstance(value, dict):
        for key, item in value.items():
            if key == "file" and isinstance(item, str) and item:
                names.add(os.path.basename(item))
            else:
                names.update(_config_source_basenames(item))
    elif isinstance(value, list):
        for item in value:
            names.update(_config_source_basenames(item))
    return names


def _rewrite_config_source_basename(value, old_basename, new_basename):
    if isinstance(value, dict):
        for key, item in value.items():
            if (key == "file" and isinstance(item, str) and
                    os.path.basename(item) == old_basename):
                value[key] = os.path.join(os.path.dirname(item), new_basename)
            else:
                _rewrite_config_source_basename(item, old_basename, new_basename)
    elif isinstance(value, list):
        for item in value:
            _rewrite_config_source_basename(item, old_basename, new_basename)


def align_single_source_config(var_config, sources):
    """Match a renamed single source to the original filename stored in config.

    Precision-search outputs are commonly renamed to the configuration id (for
    example, adi.c -> 19.c), while source locations in the JSON intentionally
    retain the original filename.  The plugin matches rules by basename, so
    adapt only the temporary config passed to it.  Multi-source configs remain
    untouched because their filename mapping is inherently ambiguous.
    """
    if len(sources) != 1:
        return None
    config_names = _config_source_basenames(var_config)
    if len(config_names) != 1:
        return None
    old_basename = next(iter(config_names))
    new_basename = os.path.basename(sources[0])
    if old_basename == new_basename:
        return None
    _rewrite_config_source_basename(var_config, old_basename, new_basename)
    return old_basename, new_basename


def _validate_sources(sources):
    checked = []
    for src in _dedupe_preserve_order(sources):
        if os.path.isdir(src):
            raise ValueError(f"--src entry is a directory after expansion: {src}")
        if not os.path.exists(src):
            raise FileNotFoundError(src)
        if not src.lower().endswith((".c", ".cc", ".cpp", ".cxx")):
            raise ValueError(f"--src entry is not a C/C++ source file: {src}")
        checked.append(src)
    return checked


def _normalize_include(value):
    include = value[2:] if value.startswith("-I") else value
    if os.path.isfile(include) or os.path.splitext(include)[1].lower() in (".h", ".hh", ".hpp"):
        include = os.path.dirname(include) or "."
    return include


def _normalize_includes(includes):
    return _dedupe_preserve_order(_normalize_include(include) for include in includes)


class Options:
    def __init__(self):
        self.sources = []
        self.config = ""
        self.out_dir = DEFAULT_OUT_DIR
        self.clang = DEFAULT_CLANG
        self.plugin = DEFAULT_PLUGIN
        self.strict_config = False
        self.caller_driven_clone = False
        self.enable_field_change = None
        self.x86_fp16_abi_compat = platform.machine().lower() in ("x86_64", "amd64")
        self.includes = []
        self.extra_clang_args = []
        self.build_exe = ""
        self.run = False
        self.keep_config = False


def parse_args(argv):
    opts = Options()
    positional = []
    after_separator = False
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == "--":
            after_separator = True
            i += 1
            continue
        if after_separator:
            opts.extra_clang_args.append(arg)
            i += 1
            continue

        def take_value(prefix):
            nonlocal i
            if arg.startswith(prefix + "="):
                return arg.split("=", 1)[1]
            if i + 1 >= len(argv):
                raise ValueError("missing value for " + prefix)
            i += 1
            return argv[i]

        def take_values(prefix, allow_dash_i=False):
            nonlocal i
            if arg.startswith(prefix + "="):
                return _split_csv(arg.split("=", 1)[1])
            values = []
            while i + 1 < len(argv):
                nxt = argv[i + 1]
                if nxt == "--":
                    break
                if nxt.startswith("-") and not (allow_dash_i and nxt.startswith("-I")):
                    break
                i += 1
                values.extend(_split_csv(nxt))
            if not values:
                raise ValueError("missing value for " + prefix)
            return values

        if arg in ("--strict-config", "-strict-config"):
            opts.strict_config = True
        elif arg.startswith("--strict-config=") or arg.startswith("-strict-config="):
            opts.strict_config = _parse_bool(arg.split("=", 1)[1])
        elif arg in ("--caller-driven-clone", "-caller-driven-clone"):
            opts.caller_driven_clone = True
        elif arg.startswith("--caller-driven-clone=") or arg.startswith("-caller-driven-clone="):
            opts.caller_driven_clone = _parse_bool(arg.split("=", 1)[1])
        elif arg in ("--enable-field-change", "-enable-field-change"):
            opts.enable_field_change = True
        elif arg.startswith("--enable-field-change=") or arg.startswith("-enable-field-change="):
            opts.enable_field_change = _parse_bool(arg.split("=", 1)[1])
        elif arg in ("--x86-fp16-abi-compat", "-x86-fp16-abi-compat"):
            opts.x86_fp16_abi_compat = True
        elif arg.startswith("--x86-fp16-abi-compat=") or arg.startswith("-x86-fp16-abi-compat="):
            opts.x86_fp16_abi_compat = _parse_bool(arg.split("=", 1)[1])
        elif arg.startswith("--src"):
            opts.sources.extend(_expand_globs(take_values("--src")))
        elif arg.startswith("-src"):
            opts.sources.extend(_expand_globs(take_values("-src")))
        elif arg.startswith("--config"):
            opts.config = take_value("--config")
        elif arg.startswith("-config"):
            opts.config = take_value("-config")
        elif arg.startswith("--out-dir"):
            opts.out_dir = take_value("--out-dir")
        elif arg.startswith("--clang"):
            opts.clang = take_value("--clang")
        elif arg.startswith("--plugin"):
            opts.plugin = take_value("--plugin")
        elif arg.startswith("--include") or arg == "-I":
            prefix = "--include" if arg.startswith("--include") else "-I"
            for value in take_values(prefix, allow_dash_i=True):
                opts.includes.append(_normalize_include(value))
        elif arg.startswith("-I") and len(arg) > 2:
            opts.includes.append(_normalize_include(arg))
        elif arg.startswith("--build-exe"):
            opts.build_exe = take_value("--build-exe")
        elif arg == "--keep-config":
            opts.keep_config = True
        elif arg.startswith("--keep-config="):
            opts.keep_config = _parse_bool(arg.split("=", 1)[1])
        elif arg == "--run":
            opts.run = True
        elif arg.startswith("-"):
            opts.extra_clang_args.append(arg)
        else:
            positional.append(arg)
        i += 1

    if positional:
        if not opts.sources:
            opts.sources = _expand_globs(positional[:1])
        if len(positional) > 1 and not opts.config:
            opts.config = positional[1]
        for extra in positional[2:]:
            opts.extra_clang_args.append(extra)

    opts.sources = _validate_sources(opts.sources)
    opts.includes = _normalize_includes(opts.includes)
    if not opts.sources or not opts.config:
        raise SystemExit(
            "usage: trans-type.py --src <file.c|dir|glob[,more...]> "
            "--config <config.json> [--include <dir|-Idir|header.h>] "
            "[--strict-config=true] [--caller-driven-clone=true] "
            "[--enable-field-change=true|false] [--out-dir DIR] "
            "[--build-exe EXE] [--keep-config=true] -- [extra clang args]"
        )
    return opts


def plugin_arg(value):
    return ["-Xclang", "-plugin-arg-trans-type", "-Xclang", value]


def source_output_name(config_name, src_file, source_dir):
    cfg_base = os.path.splitext(os.path.basename(config_name))[0]
    _, ext = os.path.splitext(src_file)
    if not ext:
        ext = ".c"
    return os.path.join(source_dir, cfg_base + ext)


def source_include_dirs(sources):
    include_dirs = []
    seen = set()
    for src_file in sources:
        src_dir = os.path.dirname(os.path.abspath(src_file))
        if src_dir and src_dir not in seen:
            include_dirs.append("-I" + src_dir)
            seen.add(src_dir)
    return include_dirs


def copy_generated_headers(src_dir, dst_dir):
    if not src_dir or not dst_dir or not os.path.isdir(src_dir):
        return
    os.makedirs(dst_dir, exist_ok=True)
    for name in os.listdir(src_dir):
        lower = name.lower()
        if not (lower.endswith(".h") or lower.endswith(".hh") or lower.endswith(".hpp")):
            continue
        src_path = os.path.join(src_dir, name)
        dst_path = os.path.join(dst_dir, name)
        if os.path.isfile(src_path):
            shutil.copy2(src_path, dst_path)


def norm_type(type_text):
    return re.sub(r"\s+", " ", str(type_text).replace(" *", "*").replace("*", " * ")).strip()


def same_type(lhs, rhs):
    return norm_type(lhs) == norm_type(rhs)


def floating_storage_base_type(type_text):
    t = norm_type(type_text or "")
    t = re.sub(r"\[[^\]]*\]", "", t)
    t = t.replace("*", " ").replace("&", " ")
    t = re.sub(r"\bconst\b", " ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t if t in ("float", "double", "long double", "_Float16", "__fp16", "__half") else ""


def is_floating_storage_type(type_text):
    return bool(floating_storage_base_type(type_text))


def should_use_actual_arg_type(actual_type, desired_type, declared_type):
    return (
        is_floating_storage_type(actual_type) or
        is_floating_storage_type(desired_type) or
        is_floating_storage_type(declared_type)
    )


def is_placeholder_type(type_text):
    t = norm_type(type_text or "")
    return (
        not t or
        t == "*" or
        t == "const *" or
        t == "integer" or
        t.startswith("integer ") or
        t.startswith("integer[")
    )


def split_c_args(arg_text):
    args = []
    cur = []
    depth = 0
    in_str = None
    escaped = False
    for ch in arg_text:
        if in_str:
            cur.append(ch)
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == in_str:
                in_str = None
            continue
        if ch in ("'", '"'):
            in_str = ch
            cur.append(ch)
            continue
        if ch in "([{":
            depth += 1
        elif ch in ")]}" and depth > 0:
            depth -= 1
        if ch == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    tail = "".join(cur).strip()
    if tail:
        args.append(tail)
    return args


def strip_c_comments(text):
    return re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)


def parse_param_list(param_text):
    params = []
    for raw_param in split_c_args(param_text):
        param = re.sub(r"\s+", " ", strip_c_comments(raw_param).strip())
        if not param or param == "void" or "..." in param:
            continue
        match = re.search(r"([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*$", param)
        if not match:
            continue
        name = match.group(1)
        array_suffix = match.group(2) or ""
        type_part = param[:match.start(1)].strip()
        if array_suffix:
            type_part = f"{type_part}{array_suffix}"
        params.append({"name": name, "type": norm_type(type_part)})
    return params


def discover_source_function_defs(sources, var_config):
    defs = {}
    pattern = re.compile(
        r"(?m)^[ \t]*(?:static[ \t]+)?(?:inline[ \t]+)?"
        r"([A-Za-z_][\w\s\*]*?)\s+([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*\{"
    )
    for src_file in sources:
        try:
            with open(src_file, "r", encoding="utf-8", errors="ignore") as f:
                text = f.read()
        except OSError:
            continue
        for match in pattern.finditer(text):
            name = match.group(2)
            defs[name] = {
                "name": name,
                "file": src_file,
                "line": text.count("\n", 0, match.start()) + 1,
                "params": parse_param_list(match.group(3)),
            }
    for func_name in (var_config.get("local") or {}).keys():
        if func_name not in defs and func_name.endswith("_") and func_name[:-1] in defs:
            defs[func_name] = defs[func_name[:-1]]
    return defs


def resolve_source_path(config_file, sources):
    if config_file and os.path.exists(config_file):
        return config_file
    config_base = os.path.basename(config_file or "")
    for src_file in sources:
        if os.path.basename(src_file) == config_base:
            return src_file
    return config_file


def line_statement(src_file, line_no, sources, max_lines=12):
    src_file = resolve_source_path(src_file, sources)
    try:
        with open(src_file, "r", encoding="utf-8", errors="ignore") as f:
            lines = f.readlines()
    except OSError:
        return ""
    if line_no < 1 or line_no > len(lines):
        return ""
    stmt = []
    for line in lines[line_no - 1:min(len(lines), line_no - 1 + max_lines)]:
        stmt.append(line.strip())
        if ";" in line:
            break
    return " ".join(stmt)


def extract_call_args(statement, callee):
    names = [callee]
    if callee.endswith("_"):
        names.append(callee[:-1])
    for name in names:
        idx = statement.find(name + "(")
        if idx < 0:
            idx = statement.find(name + " (")
        if idx < 0:
            continue
        open_idx = statement.find("(", idx + len(name))
        if open_idx < 0:
            continue
        rparen = find_matching_paren(statement, open_idx)
        if rparen >= 0:
            return split_c_args(statement[open_idx + 1:rparen])
    return []


def config_var_type_index(var_config):
    globals_by_name = {}
    for entry in var_config.get("global") or []:
        globals_by_name[entry.get("name")] = entry.get("type")

    locals_by_func = {}
    for func, entries in (var_config.get("local") or {}).items():
        by_name = {}
        for entry in entries or []:
            by_name[entry.get("name")] = entry.get("type")
        locals_by_func[func] = by_name
    return globals_by_name, locals_by_func


def arg_base_name(arg):
    expr = strip_outer_parens(arg)
    while expr.startswith("&") or expr.startswith("*"):
        expr = strip_outer_parens(expr[1:])
    match = re.match(r"([A-Za-z_]\w*)", expr)
    return match.group(1) if match else ""


def strip_outer_parens(expr):
    expr = expr.strip()
    while expr.startswith("(") and expr.endswith(")"):
        depth = 0
        matched_outer = True
        for i, ch in enumerate(expr):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0 and i != len(expr) - 1:
                    matched_outer = False
                    break
        if not matched_outer:
            break
        expr = expr[1:-1].strip()
    return expr


def array_element_type(type_text):
    type_text = norm_type(type_text)
    if "[" not in type_text:
        return ""
    return norm_type(type_text.split("[", 1)[0])


def is_array_subscript_expr(expr):
    expr = strip_outer_parens(expr)
    return re.match(r"^[A-Za-z_]\w*(?:\s*\[[^\]]+\])+\s*$", expr) is not None


def subscript_count(expr):
    expr = strip_outer_parens(expr)
    if not is_array_subscript_expr(expr):
        return 0
    return len(re.findall(r"\[[^\]]+\]", expr))


def decay_array_to_pointer(type_text):
    type_text = norm_type(type_text)
    elem_type = array_element_type(type_text)
    if elem_type:
        return norm_type(elem_type + " *")
    return type_text


def add_pointer_type(type_text):
    type_text = norm_type(type_text)
    return norm_type(type_text + " *")


def peel_one_indirection(type_text):
    type_text = norm_type(type_text)
    array_match = re.search(r"\s*\[[^\]]*\]\s*$", type_text)
    if array_match:
        return norm_type(type_text[:array_match.start()])
    if type_text.endswith("*"):
        return norm_type(type_text[:-1])
    return type_text


def type_after_subscripts(base_type, count):
    result = norm_type(base_type)
    for _ in range(count):
        result = peel_one_indirection(result)
    return result


def actual_arg_type(arg, caller, globals_by_name, locals_by_func):
    expr = strip_outer_parens(arg)
    name = arg_base_name(expr)
    if not name:
        return ""
    base_type = (locals_by_func.get(caller) or {}).get(name) or globals_by_name.get(name) or ""
    if is_placeholder_type(base_type):
        return ""
    base_type = norm_type(base_type)
    if expr.startswith("&"):
        target = strip_outer_parens(expr[1:])
        target_type = type_after_subscripts(base_type, subscript_count(target))
        return add_pointer_type(target_type)
    if expr.startswith("*"):
        target = strip_outer_parens(expr[1:])
        target_type = type_after_subscripts(base_type, subscript_count(target))
        return peel_one_indirection(target_type)
    if is_array_subscript_expr(expr):
        return type_after_subscripts(base_type, subscript_count(expr))
    return decay_array_to_pointer(base_type)


def configured_param_rules(var_config, func_name, source_name=None):
    rules = {}
    for name in (func_name, source_name):
        if not name:
            continue
        for entry in (var_config.get("local") or {}).get(name, []) or []:
            if entry.get("isParam") and entry.get("name"):
                rule_type = entry.get("type", "")
                if not is_placeholder_type(rule_type):
                    rules[entry["name"]] = rule_type
    return rules


def c_param_decl(type_name, param_name):
    type_name = norm_type(type_name)
    array_match = re.search(r"(\[[^\]]*\])$", type_name)
    if array_match:
        return f"{type_name[:array_match.start()].strip()} {param_name}{array_match.group(1)}"
    return f"{type_name} {param_name}".strip()


def clone_signature_key(source_name, desired_types):
    return source_name + "(" + ",".join(norm_type(t) for t in desired_types) + ")"


def source_aliases(callee, func_def):
    aliases = [callee]
    source_name = func_def.get("name")
    if source_name and source_name not in aliases:
        aliases.append(source_name)
    if callee.endswith("_") and callee[:-1] not in aliases:
        aliases.append(callee[:-1])
    return aliases


def caller_driven_clone_plans(sources, var_config, enabled):
    if not enabled:
        return {"clones": {}, "calls": []}
    func_defs = discover_source_function_defs(sources, var_config)
    globals_by_name, locals_by_func = config_var_type_index(var_config)
    clones = {}
    calls = []
    clone_counts = {}

    for caller, call_entries in (var_config.get("calls") or {}).items():
        for call_entry in call_entries or []:
            callee = call_entry.get("type")
            func_def = func_defs.get(callee)
            if not callee or not func_def:
                continue
            call_file = call_entry.get("file") or ""
            def_file = func_def.get("file") or ""

            statement = line_statement(call_file, int(call_entry.get("line", 0) or 0), sources)
            args = extract_call_args(statement, callee)
            params = func_def.get("params") or []
            if len(args) != len(params):
                continue

            source_name = func_def.get("name") or callee
            configured_rules = configured_param_rules(var_config, callee, source_name)
            desired_types = []
            changed = False
            for arg, param in zip(args, params):
                declared_type = param.get("type", "")
                desired_type = configured_rules.get(param.get("name", ""), declared_type)
                actual_type = actual_arg_type(arg, caller, globals_by_name, locals_by_func)
                if (actual_type and
                        not same_type(actual_type, desired_type) and
                        should_use_actual_arg_type(actual_type, desired_type, declared_type)):
                    desired_type = actual_type
                desired_types.append(desired_type)
                changed = changed or not same_type(desired_type, declared_type)
            if not changed:
                continue

            key = clone_signature_key(source_name, desired_types)
            if key not in clones:
                clone_index = clone_counts.get(source_name, 0)
                clone_counts[source_name] = clone_index + 1
                clone_name = f"{source_name}_cd_{clone_index}"
                clones[key] = {
                    "callee": callee,
                    "source_name": source_name,
                    "clone_name": clone_name,
                    "def_file": def_file,
                    "params": params,
                    "desired_types": desired_types,
                    "aliases": source_aliases(callee, func_def),
                }
            calls.append({
                "caller": caller,
                "call_file": resolve_source_path(call_file, sources),
                "callee": callee,
                "args": args,
                "clone_key": key,
            })
    return {"clones": clones, "calls": calls}


def find_matching_paren(text, open_idx):
    depth = 0
    in_str = None
    escaped = False
    for pos in range(open_idx, len(text)):
        ch = text[pos]
        if in_str:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == in_str:
                in_str = None
            continue
        if ch in ("'", '"'):
            in_str = ch
            continue
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return pos
    return -1


def find_matching_brace(text, open_idx):
    depth = 0
    in_str = None
    escaped = False
    for pos in range(open_idx, len(text)):
        ch = text[pos]
        if in_str:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == in_str:
                in_str = None
            continue
        if ch in ("'", '"'):
            in_str = ch
            continue
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return pos
    return -1


def find_function_definition_text(text, func_name):
    pattern = re.compile(
        r"(?m)^([ \t]*(?:static[ \t]+)?[A-Za-z_][A-Za-z0-9_ \t\*]*?[ \t]+)"
        + re.escape(func_name) + r"\s*\("
    )
    for match in pattern.finditer(text):
        lparen = text.find("(", match.end() - 1)
        rparen = find_matching_paren(text, lparen)
        if rparen < 0:
            continue
        scan = rparen + 1
        while scan < len(text) and text[scan].isspace():
            scan += 1
        if scan >= len(text) or text[scan] != "{":
            continue
        end = find_matching_brace(text, scan)
        if end < 0:
            continue
        return {
            "prefix": match.group(1),
            "body": text[scan:end + 1],
        }
    return None


def iter_function_definitions(text):
    pattern = re.compile(
        r"(?m)^([ \t]*(?:static[ \t]+)?[A-Za-z_][A-Za-z0-9_ \t\*]*?[ \t]+)"
        r"([A-Za-z_]\w*)\s*\("
    )
    for match in pattern.finditer(text):
        name = match.group(2)
        lparen = text.find("(", match.end() - 1)
        rparen = find_matching_paren(text, lparen)
        if rparen < 0:
            continue
        scan = rparen + 1
        while scan < len(text) and text[scan].isspace():
            scan += 1
        if scan >= len(text) or text[scan] != "{":
            continue
        yield {
            "name": name,
            "prefix": match.group(1),
            "params": parse_param_list(text[lparen + 1:rparen]),
        }


def candidate_clone_name(source_name, func_name):
    return (
        func_name == source_name or
        func_name.startswith(source_name + "_") or
        func_name.startswith(source_name + "__")
    )


def find_existing_matching_function(text, clone_plan):
    source_name = clone_plan["source_name"]
    desired_types = [norm_type(t) for t in clone_plan["desired_types"]]
    for func_def in iter_function_definitions(text):
        name = func_def["name"]
        if not candidate_clone_name(source_name, name):
            continue
        actual_types = [norm_type(p.get("type", "")) for p in func_def.get("params", [])]
        if len(actual_types) != len(desired_types):
            continue
        if all(same_type(actual, desired) for actual, desired in zip(actual_types, desired_types)):
            param_text = ", ".join(
                c_param_decl(param["type"], param.get("name") or f"p{i}")
                for i, param in enumerate(func_def.get("params", []))
            )
            clone_plan["clone_name"] = name
            clone_plan["prototype"] = f"{func_def['prefix']}{name}({param_text});\n"
            clone_plan["uses_existing"] = True
            return name
    return ""


def replace_identifier_tokens(text, old_name, new_name):
    return re.sub(r"\b" + re.escape(old_name) + r"\b", new_name, text)


def build_clone_definition(transformed_text, clone_plan):
    if find_existing_matching_function(transformed_text, clone_plan):
        return ""
    source_name = clone_plan["source_name"]
    func_def = find_function_definition_text(transformed_text, source_name)
    if not func_def:
        return ""
    params = clone_plan["params"]
    desired_types = clone_plan["desired_types"]
    clone_name = clone_plan["clone_name"]
    param_text = ", ".join(
        c_param_decl(desired_type, param.get("name") or f"p{i}")
        for i, (param, desired_type) in enumerate(zip(params, desired_types))
    )
    body = replace_identifier_tokens(func_def["body"], source_name, clone_name)
    clone_plan["prototype"] = f"{func_def['prefix']}{clone_name}({param_text});\n"
    return f"{func_def['prefix']}{clone_name}({param_text})\n{body}\n"


def rewrite_calls_to_clone(text, call_plans, clones):
    for call_plan in call_plans:
        clone_plan = clones.get(call_plan["clone_key"])
        if not clone_plan:
            continue
        clone_name = clone_plan["clone_name"]
        for alias in clone_plan["aliases"]:
            pattern = re.compile(r"\b" + re.escape(alias) + r"\s*\(")
            pos = 0
            while True:
                match = pattern.search(text, pos)
                if not match:
                    break
                lparen = text.find("(", match.end() - 1)
                rparen = find_matching_paren(text, lparen)
                if rparen < 0:
                    break
                args = split_c_args(text[lparen + 1:rparen])
                if len(args) == len(call_plan["args"]) and all(
                    re.sub(r"\s+", "", a) == re.sub(r"\s+", "", b)
                    for a, b in zip(args, call_plan["args"])
                ):
                    text = text[:match.start()] + clone_name + text[lparen:]
                    pos = match.start() + len(clone_name)
                else:
                    pos = rparen + 1
    return text


def is_top_level_pos(text, pos):
    depth = 0
    i = 0
    in_string = None
    escaped = False
    in_line_comment = False
    in_block_comment = False

    while i < pos:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < pos else ""

        if in_line_comment:
            if ch == "\n":
                in_line_comment = False
            i += 1
            continue
        if in_block_comment:
            if ch == "*" and nxt == "/":
                in_block_comment = False
                i += 2
            else:
                i += 1
            continue
        if in_string:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == in_string:
                in_string = None
            i += 1
            continue

        if ch == "/" and nxt == "/":
            in_line_comment = True
            i += 2
            continue
        if ch == "/" and nxt == "*":
            in_block_comment = True
            i += 2
            continue
        if ch in ("\"", "'"):
            in_string = ch
        elif ch == "{":
            depth += 1
        elif ch == "}" and depth > 0:
            depth -= 1
        i += 1

    return depth == 0


def first_function_definition_pos(text):
    pattern = re.compile(
        r"(?m)^[ \t]*(?:(?:static|inline|extern)\s+)*"
        r"[A-Za-z_][A-Za-z0-9_\s\*]*?\s+[A-Za-z_]\w*\s*\("
    )
    for match in pattern.finditer(text):
        if not is_top_level_pos(text, match.start()):
            continue
        lparen = text.find("(", match.end() - 1)
        rparen = find_matching_paren(text, lparen)
        if rparen < 0:
            continue
        scan = rparen + 1
        while scan < len(text) and text[scan].isspace():
            scan += 1
        if scan < len(text) and text[scan] == "{":
            return match.start()
    return -1


def insert_clone_prototypes(text, prototypes):
    if not prototypes:
        return text
    insert_pos = first_function_definition_pos(text)
    if insert_pos < 0:
        insert_pos = 0
        for match in re.finditer(r"(?m)^[ \t]*#include[^\n]*(?:\n|$)", text):
            insert_pos = match.end()
    return text[:insert_pos] + "\n" + "".join(prototypes) + "\n" + text[insert_pos:]


def apply_caller_driven_per_callsite_clones(sources, transformed_sources, clone_info):
    clones = clone_info.get("clones") or {}
    call_plans = clone_info.get("calls") or []
    if not clones:
        return

    transformed_by_base = {
        os.path.basename(src_file): transformed
        for src_file, transformed in zip(sources, transformed_sources)
    }

    clones_by_file = {}
    for clone_plan in clones.values():
        transformed = transformed_by_base.get(os.path.basename(clone_plan["def_file"]))
        if transformed:
            clones_by_file.setdefault(transformed, []).append(clone_plan)

    for transformed, file_clones in clones_by_file.items():
        try:
            with open(transformed, "r", encoding="utf-8", errors="ignore") as f:
                text = f.read()
        except OSError:
            continue
        additions = []
        for clone_plan in file_clones:
            if find_existing_matching_function(text, clone_plan):
                continue
            if re.search(r"\b" + re.escape(clone_plan["clone_name"]) + r"\s*\(", text):
                continue
            clone_text = build_clone_definition(text, clone_plan)
            if clone_text:
                additions.append(clone_text)
        if additions:
            with open(transformed, "w", encoding="utf-8") as f:
                f.write(text.rstrip() + "\n\n" + "\n\n".join(additions) + "\n")

    calls_by_file = {}
    for call_plan in call_plans:
        transformed = transformed_by_base.get(os.path.basename(call_plan["call_file"]))
        if transformed:
            calls_by_file.setdefault(transformed, []).append(call_plan)

    for transformed, file_calls in calls_by_file.items():
        try:
            with open(transformed, "r", encoding="utf-8", errors="ignore") as f:
                text = f.read()
        except OSError:
            continue
        text = rewrite_calls_to_clone(text, file_calls, clones)
        prototypes = []
        for call_plan in file_calls:
            clone_plan = clones.get(call_plan["clone_key"])
            if not clone_plan:
                continue
            proto = clone_plan.get("prototype", "")
            if proto and proto not in prototypes and proto not in text:
                prototypes.append(proto)
        if prototypes:
            text = insert_clone_prototypes(text, prototypes)
        with open(transformed, "w", encoding="utf-8") as f:
            f.write(text)


def transform_type_args(opts, phase="all", enable_clone=False):
    args = [
        *plugin_arg("--enable-change=true"),
        *plugin_arg("--transform-phase=" + phase),
    ]
    if opts.enable_field_change:
        args += plugin_arg("--enable-field-change=true")
    if opts.strict_config:
        args += plugin_arg("--strict-config=true")
    if enable_clone:
        args += plugin_arg("--caller-driven-clone=true")
    if opts.x86_fp16_abi_compat:
        args += plugin_arg("--x86-fp16-abi-compat=true")
    return args


def run_transform_phase(opts, src, config_path, out_dir, obj_path, include_dirs,
                        phase="all", enable_clone=False, extra_include_dirs=None):
    os.makedirs(out_dir, exist_ok=True)
    merged_includes = []
    seen = set()
    for inc in (extra_include_dirs or []) + include_dirs:
        if inc not in seen:
            merged_includes.append(inc)
            seen.add(inc)

    phase_cflags = []
    if phase == "types-only":
        phase_cflags += [
            "-Wno-incompatible-pointer-types",
            "-Wno-error=incompatible-pointer-types",
        ]

    cmd = [
        opts.clang,
        "-Xclang", "-load", "-Xclang", opts.plugin,
        "-Xclang", "-plugin", "-Xclang", "trans-type",
        *plugin_arg("-output-path"),
        *plugin_arg(out_dir),
        *plugin_arg("-input-config"),
        *plugin_arg(config_path),
    ] + transform_type_args(opts, phase=phase, enable_clone=enable_clone)
    cmd += merged_includes + opts.extra_clang_args + phase_cflags
    cmd += [src, "-c", "-o", obj_path]
    subprocess.run(cmd, check=True)


def transform_sources(opts):
    with open(opts.config, "r", encoding="utf-8") as f:
        var_config = json.load(f)
    source_alias = align_single_source_config(var_config, opts.sources)
    if source_alias:
        print("Config source filename:", source_alias[0], "->", source_alias[1])
    if opts.enable_field_change is None:
        opts.enable_field_change = bool(var_config.get("structs"))

    os.makedirs(opts.out_dir, exist_ok=True)
    cfg_temp = None
    if opts.keep_config:
        cfg_dir = os.path.join(opts.out_dir, "config")
        os.makedirs(cfg_dir, exist_ok=True)
    else:
        cfg_temp = tempfile.TemporaryDirectory(prefix="trans-type-config-")
        cfg_dir = cfg_temp.name

    try:
        return _transform_sources_with_config_dir(opts, var_config, cfg_dir)
    finally:
        if cfg_temp is not None:
            cfg_temp.cleanup()


def _transform_sources_with_config_dir(opts, var_config, cfg_dir):
    # The C++ plugin now owns caller-driven cloning and external ABI bridging.
    # Keep the older Python per-callsite clone pass disabled; it can generate
    # stale *_cd_* calls/prototypes after the plugin has already cloned functions.
    clone_info = {"clones": {}, "calls": []}

    default_includes = [
        "-I" + os.path.abspath(opts.out_dir),
        "-I" + GSL_INCLUDE,
    ]
    default_includes += source_include_dirs(opts.sources)
    default_includes += ["-I" + os.path.abspath(inc) for inc in opts.includes]

    transformed_sources = []
    cfg_base = os.path.splitext(os.path.basename(opts.config))[0]
    for i, src in enumerate(opts.sources):
        if not os.path.exists(src):
            raise FileNotFoundError(src)
        src_base = os.path.splitext(os.path.basename(src))[0]
        if len(opts.sources) == 1:
            plugin_config = os.path.join(cfg_dir, os.path.basename(opts.config))
        else:
            plugin_config = os.path.join(cfg_dir, f"{cfg_base}_{i}_{src_base}.json")
        with open(plugin_config, "w", encoding="utf-8") as f:
            json.dump(var_config, f, indent=4)

        obj_path = os.path.join(opts.out_dir, f"{cfg_base}_{i}_{src_base}.o")
        if opts.caller_driven_clone:
            with tempfile.TemporaryDirectory(prefix=f"trans-type-stage1-{i}-") as stage1_dir:
                stage1_obj = os.path.join(stage1_dir, f"{cfg_base}_{i}_{src_base}.o")
                run_transform_phase(
                    opts,
                    src,
                    plugin_config,
                    stage1_dir,
                    stage1_obj,
                    default_includes,
                    phase="types-only",
                    enable_clone=False,
                )
                stage1_source = source_output_name(plugin_config, src, stage1_dir)
                if not os.path.exists(stage1_source):
                    raise FileNotFoundError("first transform phase did not produce " + stage1_source)
                copy_generated_headers(stage1_dir, opts.out_dir)
                run_transform_phase(
                    opts,
                    stage1_source,
                    plugin_config,
                    opts.out_dir,
                    obj_path,
                    default_includes,
                    phase="clone-only",
                    enable_clone=opts.caller_driven_clone,
                    extra_include_dirs=["-I" + os.path.abspath(stage1_dir)],
                )
        else:
            run_transform_phase(
                opts,
                src,
                plugin_config,
                opts.out_dir,
                obj_path,
                default_includes,
                phase="all",
                enable_clone=False,
            )

        transformed = source_output_name(plugin_config, src, opts.out_dir)
        if not os.path.exists(transformed):
            raise FileNotFoundError("transform did not produce " + transformed)
        transformed_sources.append(transformed)

    if clone_info.get("clones"):
        apply_caller_driven_per_callsite_clones(opts.sources, transformed_sources, clone_info)
    return transformed_sources


def build_executable(opts, transformed_sources):
    if not opts.build_exe:
        return
    cmd = [
        opts.clang,
        "-o", opts.build_exe,
        "-I" + os.path.abspath(opts.out_dir),
    ]
    cmd += source_include_dirs(opts.sources)
    cmd += ["-I" + os.path.abspath(inc) for inc in opts.includes]
    cmd += transformed_sources + opts.extra_clang_args
    cmd += ["-L" + GSL_LIB, "-lgsl", "-lgslcblas", "-lm"]
    subprocess.run(cmd, check=True)
    if opts.run:
        subprocess.run([opts.build_exe], check=True)


def main():
    opts = parse_args(sys.argv[1:])
    print("Starting source-to-source transformation...")
    print("Sources:")
    for src in opts.sources:
        print("  " + src)
    print("Config:", opts.config)
    print("Output:", opts.out_dir)
    print("Strict config:", opts.strict_config)
    print("Caller-driven clone:", opts.caller_driven_clone)
    transformed_sources = transform_sources(opts)
    print("Field change:", opts.enable_field_change)
    print("Transformed sources:")
    for src in transformed_sources:
        print("  " + src)
    build_executable(opts, transformed_sources)
    print("Done.")


if __name__ == "__main__":
    main()
