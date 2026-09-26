#!/usr/bin/env python3
"""Helper Kconfig untuk proyek tracey.

Subcommand:
  menuconfig     - buka TUI menuconfig
  olddefconfig   - normalisasi .config ke default untuk symbol baru
  gen-header     - hasilkan kconfig_autoconf.h dari .config
"""
import argparse
import os
import sys

# --- Dependency check -------------------------------------------------------
try:
    import kconfiglib
except ImportError:
    print("ERROR: kconfiglib tidak terinstal.", file=sys.stderr)
    print("  Install dengan:", file=sys.stderr)
    print(f"    {sys.executable} -m pip install kconfiglib", file=sys.stderr)
    sys.exit(127)

# Modul menuconfig TUI terpisah, dipaket bersama kconfiglib.
# Di kconfiglib 14.x, `kconfiglib.menuconfig` TIDAK ada; gunakan modul ini.
try:
    from menuconfig import menuconfig as _run_menuconfig
except ImportError:
    _run_menuconfig = None


# --- Helpers ----------------------------------------------------------------
def load(kconfig_path, config_path):
    """Muat Kconfig, lalu .config jika ada."""
    # Paths are already validated by callers
    if not os.path.exists(kconfig_path):
        print(f"ERROR: {kconfig_path} tidak ditemukan", file=sys.stderr)
        sys.exit(1)

    kconf = kconfiglib.Kconfig(kconfig_path, warn_to_stderr=False)

    if config_path and os.path.exists(config_path):
        kconf.load_config(config_path)

    return kconf


# --- Subcommands ------------------------------------------------------------
def _validate_path(path: str, description: str) -> str:
    """Validate and sanitize a file path to prevent injection."""
    if not path or not path.strip():
        print(f"ERROR: {description} path is empty", file=sys.stderr)
        sys.exit(1)
    # Resolve to absolute path to prevent directory traversal confusion
    abs_path = os.path.abspath(path)
    # Basic validation - ensure path doesn't contain null bytes or newlines
    if '\0' in path or '\n' in path or '\r' in path:
        print(f"ERROR: {description} path contains invalid characters", file=sys.stderr)
        sys.exit(1)
    return abs_path


def cmd_menuconfig(args):
    if _run_menuconfig is None:
        print(
            "ERROR: modul 'menuconfig' tidak ditemukan.\n"
            "  Ini biasanya karena kconfiglib terinstal tidak lengkap.\n"
            "  Coba reinstall:\n"
            f"    {sys.executable} -m pip install --force-reinstall kconfiglib",
            file=sys.stderr,
        )
        sys.exit(127)

    config_path = _validate_path(args.config, "config")
    kconfig_path = _validate_path(args.kconfig, "kconfig")
    kconf = load(kconfig_path, config_path)

    # --- TEMA DEFAULT PROYEK TRACEY (HITAM, MERAH, PUTIH) ---
    # Konfigurasi ini akan otomatis diterapkan setiap kali menuconfig dijalankan,
    # tanpa perlu mengatur variabel lingkungan secara manual di terminal.
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

    # Modul menuconfig membaca path output dari env var KCONFIG_CONFIG.
    # Use validated absolute path to prevent injection.
    os.environ["KCONFIG_CONFIG"] = config_path

    _run_menuconfig(kconf)

    # Tulis ulang untuk memastikan hasil edit tersimpan.
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

    # Jika .config belum ada, buat dari default supaya header tetap valid.
    if not os.path.exists(config_path):
        kconf.write_config(config_path)
        print(f"[kconfig] created -> {config_path}")

    header_dir = os.path.dirname(header_path)
    if header_dir:
        os.makedirs(header_dir, exist_ok=True)

    kconf.write_autoconf(header_path)
    print(f"[kconfig] wrote   -> {header_path}")


# --- CLI --------------------------------------------------------------------
def build_parser():
    parser = argparse.ArgumentParser(
        prog="kconfig_tool",
        description="Helper Kconfig untuk proyek tracey.",
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    def add_common(sp):
        sp.add_argument("--kconfig", required=True, help="Path ke file Kconfig")
        sp.add_argument("--config", required=True, help="Path ke file .config")

    p_mc = sub.add_parser("menuconfig", help="Buka TUI menuconfig")
    add_common(p_mc)

    p_od = sub.add_parser("olddefconfig", help="Normalisasi .config ke default")
    add_common(p_od)

    p_gh = sub.add_parser("gen-header", help="Hasilkan kconfig_autoconf.h")
    add_common(p_gh)
    p_gh.add_argument("--header", required=True,
                      help="Path output file kconfig_autoconf.h")

    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)

    dispatch = {
        "menuconfig": cmd_menuconfig,
        "olddefconfig": cmd_olddefconfig,
        "gen-header": cmd_gen_header,
    }
    dispatch[args.cmd](args)


if __name__ == "__main__":
    main()
