# -*- mode: python ; coding: utf-8 -*-
import os
import shutil
import subprocess
import tempfile

a = Analysis(
    ['run_ym.py'],
    pathex=[],
    binaries=[],
    datas=[],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)

# libmpv on this system is a "kitchen sink" build (Samba/SMB client, disc
# support, etc.) that PyInstaller pulls in transitively via ldd. libavformat
# is *linked* against libsmbclient (DT_NEEDED), so the whole Samba chain
# must still be loadable at process start even though ym-player never
# touches smb:// URLs — the dynamic linker resolves NEEDED libraries eagerly,
# not lazily by feature use. So we can't drop these libs, only neutralize
# their absolute RUNPATH (which is what actually broke staticx).
def has_absolute_runpath(path: str) -> bool:
    try:
        out = subprocess.run(
            ["patchelf", "--print-rpath", path],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False
    return out.startswith("/")


def soname_of(path: str):
    try:
        out = subprocess.run(
            ["patchelf", "--print-soname", path],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    return out or None


_patched_dir = tempfile.mkdtemp(prefix="ym-patched-")
_filtered_binaries = []
for dest, src, kind in a.binaries:
    if src.endswith(".so") or ".so." in os.path.basename(src):
        if has_absolute_runpath(src):
            patched = os.path.join(_patched_dir, os.path.basename(src))
            shutil.copy2(src, patched)
            os.chmod(patched, 0o755)
            subprocess.run(["patchelf", "--remove-rpath", patched], check=True)
            _filtered_binaries.append((dest, patched, kind))
            continue
    _filtered_binaries.append((dest, src, kind))

# PyInstaller places some transitively-discovered shared libraries under
# their real (patch-versioned) filename instead of the on-disk SONAME
# symlink they were resolved through — e.g. libjpeg.so.8.3.2 instead of
# libjpeg.so.8. The dynamic linker resolves DT_NEEDED entries by SONAME, so
# on a machine that doesn't already happen to have a matching system
# library, resolution fails. Add a second copy under the SONAME so it
# always resolves from the bundle itself.
_existing_names = {os.path.basename(dest) for dest, _, _ in _filtered_binaries}
_soname_aliases = []
for dest, src, kind in _filtered_binaries:
    base = os.path.basename(dest)
    if not (src.endswith(".so") or ".so." in base):
        continue
    soname = soname_of(src)
    if soname and soname != base and soname not in _existing_names:
        _soname_aliases.append((soname, src, kind))
        _existing_names.add(soname)

a.binaries = _filtered_binaries + _soname_aliases

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='ym',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
