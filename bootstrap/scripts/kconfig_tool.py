#!/usr/bin/env python3
"""Kconfig helper for the Tracey project.

Subcommands:
  menuconfig     - open the interactive menuconfig TUI
  olddefconfig   - normalise .config, filling defaults for new symbols
  gen-header     - emit kconfig_autoconf.h from .config
  gen-cmake      - emit a CMake include file (TRACEY_CFG_* variables) from .config
"""
import argparse
import os
import sys

# --- Dependency check -------------------------------------------------------
try:
    import kconfiglib
except ImportError:
    print("ERROR: kconfiglib is not installed.", file=sys.stderr)
    print("  Install it with:", file=sys.stderr)
    print(f"    {sys.executable} -m pip install kconfiglib", file=sys.stderr)
    sys.exit(127)

# The menuconfig TUI ships as a separate module bundled with kconfiglib.
# In kconfiglib 14.x, `kconfiglib.menuconfig` does NOT exist; import this module.
try:
    from menuconfig import menuconfig as _run_menuconfig
except ImportError:
    _run_menuconfig = None


# --- Helpers ----------------------------------------------------------------
def load(kconfig_path, config_path):
    """Load the Kconfig tree, then .config if it exists."""
    # Paths are already validated by callers.
    if not os.path.exists(kconfig_path):
        print(f"ERROR: {kconfig_path} not found", file=sys.stderr)
        sys.exit(1)

    kconf = kconfiglib.Kconfig(kconfig_path, warn_to_stderr=False)

    if config_path and os.path.exists(config_path):
        kconf.load_config(config_path)

    return kconf


def _validate_path(path: str, description: str) -> str:
    """Validate and sanitize a file path to prevent injection."""
    if not path or not path.strip():
        print(f"ERROR: {description} path is empty", file=sys.stderr)
        sys.exit(1)
    # Resolve to an absolute path to avoid directory-traversal confusion.
    abs_path = os.path.abspath(path)
    # Basic validation - the path must not contain NUL bytes or newlines.
    if '\0' in path or '\n' in path or '\r' in path:
        print(f"ERROR: {description} path contains invalid characters", file=sys.stderr)
        sys.exit(1)
    return abs_path


# --- Subcommands ------------------------------------------------------------
def cmd_menuconfig(args):
    if _run_menuconfig is None:
        print(
            "ERROR: the 'menuconfig' module was not found.\n"
            "  This usually means the kconfiglib install is incomplete.\n"
            "  Try reinstalling:\n"
            f"    {sys.executable} -m pip install --force-reinstall kconfiglib",
            file=sys.stderr,
        )
        sys.exit(127)

    config_path = _validate_path(args.config, "config")
    kconfig_path = _validate_path(args.kconfig, "kconfig")
    kconf = load(kconfig_path, config_path)

    # --- Default Tracey theme (black, red, white) ---
    # Applied automatically on every menuconfig run, without having to set
    # environment variables by hand in the terminal.
    os.environ["MENUCONFIG_STYLE"] = (
        "path=fg:white,bg:black,bold "
        "separator=fg:white,bg:black "
        "list=fg:brightred,bg:black "
        "selection=fg:white,bg:red,bold "
        "inv-list=fg:brightred,bg:black "
        "inv-selection=fg:white,bg:red,bold "
        "help=fg:white,bg:black "
        "frame=fg:white,bg:black "
        "body=fg:white,bg:black "
        "edit=fg:black,bg:white "
        "jump-edit=fg:black,bg:white "
        "text=fg:white,bg:black"
    )
    # -------------------------------------------------------

    # The menuconfig module reads its output path from KCONFIG_CONFIG.
    # Use the validated absolute path to prevent injection.
    os.environ["KCONFIG_CONFIG"] = config_path

    _run_menuconfig(kconf)

    # Rewrite to make sure edits are persisted.
    kconf.write_config(config_path)
    print(f"[kconfig] saved   -> {config_path}")


def cmd_olddefconfig(args):
    config_path = _validate_path(args.config, "config")
    kconfig_path = _validate_path(args.kconfig, "kconfig")
    kconf = load(kconfig_path, config_path)
    kconf.write_config(config_path)
    print(f"[kconfig] updated -> {config_path}")


def cmd_gen_header(args):
    config_path = _validate_path(args.config, "config")
    header_path = _validate_path(args.header, "header")
    kconfig_path = _validate_path(args.kconfig, "kconfig")

    kconf = load(kconfig_path, config_path)

    # If .config does not exist yet, create it from the defaults so the header
    # is still valid.
    if not os.path.exists(config_path):
        kconf.write_config(config_path)
        print(f"[kconfig] created -> {config_path}")

    header_dir = os.path.dirname(header_path)
    if header_dir:
        os.makedirs(header_dir, exist_ok=True)

    kconf.write_autoconf(header_path)
    print(f"[kconfig] wrote   -> {header_path}")


def _cmake_literal(sym):
    """Convert a kconfiglib symbol to a CMake literal.

    Booleans become 1/0; every other type (int, string, hex) is passed through
    as its resolved string value. Returns None for symbols with no meaningful
    value (unset bools, choice members that are not selected).
    """
    if sym.type in (kconfiglib.BOOL, kconfiglib.TRISTATE):
        # str_value is "y" / "m" / "n".
        return {"y": "1", "m": "2"}.get(sym.str_value, "0")
    if sym.str_value == "n":
        # A non-boolean symbol that somehow resolved to "n"; skip it.
        return None
    return sym.str_value


def cmd_gen_cmake(args):
    config_path = _validate_path(args.config, "config")
    cmake_path = _validate_path(args.cmake, "cmake")
    kconfig_path = _validate_path(args.kconfig, "kconfig")

    kconf = load(kconfig_path, config_path)

    # If .config does not exist yet, create it from the defaults so the
    # generated variables are still complete.
    if not os.path.exists(config_path):
        kconf.write_config(config_path)
        print(f"[kconfig] created -> {config_path}")

    cmake_dir = os.path.dirname(cmake_path)
    if cmake_dir:
        os.makedirs(cmake_dir, exist_ok=True)

    lines = [
        "# Generated by kconfig_tool.py gen-cmake - DO NOT EDIT.\n",
        "# Regenerated automatically whenever .config changes.\n",
        "\n",
    ]

    # Sort for deterministic output so incremental builds do not churn.
    # kconf.syms is a dict mapping symbol name -> Symbol object. Skip kconfiglib
    # built-ins and anonymous constant symbols (they have no definition node).
    defined = (s for s in kconf.syms.values() if s.nodes)
    for sym in sorted(defined, key=lambda s: s.name):
        value = _cmake_literal(sym)
        if value is None:
            continue
        lines.append(f"set(TRACEY_CFG_{sym.name} {value})\n")

    with open(cmake_path, "w", encoding="utf-8") as fh:
        fh.writelines(lines)

    print(f"[kconfig] wrote   -> {cmake_path}")


# --- CLI --------------------------------------------------------------------
def build_parser():
    parser = argparse.ArgumentParser(
        prog="kconfig_tool",
        description="Kconfig helper for the Tracey project.",
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    def add_common(sp):
        sp.add_argument("--kconfig", required=True, help="Path to the Kconfig file")
        sp.add_argument("--config", required=True, help="Path to the .config file")

    p_mc = sub.add_parser("menuconfig", help="Open the interactive menuconfig TUI")
    add_common(p_mc)

    p_od = sub.add_parser("olddefconfig", help="Normalise .config against defaults")
    add_common(p_od)

    p_gh = sub.add_parser("gen-header", help="Emit kconfig_autoconf.h")
    add_common(p_gh)
    p_gh.add_argument("--header", required=True,
                      help="Output path for kconfig_autoconf.h")

    p_gc = sub.add_parser("gen-cmake", help="Emit a CMake include file with TRACEY_CFG_* variables")
    add_common(p_gc)
    p_gc.add_argument("--cmake", required=True,
                      help="Output path for the generated CMake file")

    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)

    dispatch = {
        "menuconfig": cmd_menuconfig,
        "olddefconfig": cmd_olddefconfig,
        "gen-header": cmd_gen_header,
        "gen-cmake": cmd_gen_cmake,
    }
    dispatch[args.cmd](args)


if __name__ == "__main__":
    main()
