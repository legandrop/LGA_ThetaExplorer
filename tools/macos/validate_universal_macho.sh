#!/bin/bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Uso: $0 <ruta1> [ruta2 ...]"
    exit 1
fi

command -v file >/dev/null 2>&1 || { echo "Falta 'file'"; exit 1; }
command -v lipo >/dev/null 2>&1 || { echo "Falta 'lipo'"; exit 1; }

declare -a files
for target in "$@"; do
    [ -e "$target" ] || continue
    if [ -f "$target" ]; then
        files+=("$target")
    else
        while IFS= read -r -d '' fp; do files+=("$fp"); done < <(find "$target" -type f -print0)
    fi
done

# bash 3.2 de macOS con set -u revienta al expandir un array vacio.
if [ ${#files[@]} -eq 0 ]; then
    echo "No se encontraron archivos en: $*"
    exit 0
fi

bad=0
checked=0
for f in "${files[@]}"; do
    info="$(file -b "$f" 2>/dev/null || true)"
    [[ "$info" == *"Mach-O"* ]] || continue
    checked=$((checked + 1))
    archs="$(lipo -info "$f" 2>/dev/null || true)"
    if [[ "$archs" != *"x86_64"* || "$archs" != *"arm64"* ]]; then
        echo "No universal2: $f :: $archs"
        bad=1
    fi
done

if [ "$checked" -eq 0 ]; then
    echo "No se detectaron binarios Mach-O."
    exit 0
fi

if [ "$bad" -ne 0 ]; then
    exit 1
fi
echo "OK: $checked binarios Mach-O universal2."
