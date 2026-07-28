#!/usr/bin/env bash
# Собирает ym-player в один переносимый бинарник:
#   PyInstaller (onefile, без Samba-хвоста от системной libmpv)
#   -> staticx (вшивает glibc, чтобы не зависеть от версии glibc на целевой машине).
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

VENV=.venv

if [ ! -d "$VENV" ]; then
    python3 -m venv "$VENV"
fi

if ! command -v patchelf >/dev/null; then
    echo "Нужен системный patchelf (например: sudo zypper install patchelf)" >&2
    exit 1
fi

"$VENV/bin/pip" install -q --upgrade pip
"$VENV/bin/pip" install -q -e .
"$VENV/bin/pip" install -q pyinstaller staticx

export PATH="$PWD/$VENV/bin:$PATH"

rm -rf build dist

pyinstaller ym.spec

# glibc-hwcaps на этой машине прячет "настоящую" libz.so.1 в подпапке
# (glibc-hwcaps/...), из-за чего staticx её не находит автоматически —
# указываем каноничный путь явно. Только 64-битные каталоги: на этой
# системе /usr/lib и /lib — это 32-битный multilib, а не алиас lib64.
LIBZ=$(find /usr/lib64 /lib64 -maxdepth 1 -name 'libz.so.1' 2>/dev/null | head -1)
if [ -z "$LIBZ" ]; then
    echo "Не нашёл libz.so.1, нужно для staticx" >&2
    exit 1
fi

python -m staticx -l "$LIBZ" dist/ym dist/ym.static
mv -f dist/ym.static dist/ym

echo "Готово: dist/ym (statix-обёртка, не зависит от glibc целевой машины)"
