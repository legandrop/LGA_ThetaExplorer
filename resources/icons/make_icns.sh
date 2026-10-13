#!/bin/bash
# Genera ThetaExplorer.icns a partir de ThetaExplorer.svg
# Uso: bash make_icns.sh (desde la carpeta resources/icons/)

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SVG="$SCRIPT_DIR/ThetaExplorer.svg"
ICONSET="$SCRIPT_DIR/ThetaExplorer.iconset"
ICNS="$SCRIPT_DIR/ThetaExplorer.icns"
TMP_PNG="/tmp/ThetaExplorer_1024.png"

echo "→ Renderizando SVG a PNG 1024x1024..."
qlmanage -t -s 1024 -o /tmp/ "$SVG" > /dev/null 2>&1
mv /tmp/ThetaExplorer.svg.png "$TMP_PNG"

echo "→ Creando iconset..."
rm -rf "$ICONSET"
mkdir "$ICONSET"

sips -z 16   16   "$TMP_PNG" --out "$ICONSET/icon_16x16.png"       > /dev/null
sips -z 32   32   "$TMP_PNG" --out "$ICONSET/icon_16x16@2x.png"    > /dev/null
sips -z 32   32   "$TMP_PNG" --out "$ICONSET/icon_32x32.png"       > /dev/null
sips -z 64   64   "$TMP_PNG" --out "$ICONSET/icon_32x32@2x.png"    > /dev/null
sips -z 128  128  "$TMP_PNG" --out "$ICONSET/icon_128x128.png"     > /dev/null
sips -z 256  256  "$TMP_PNG" --out "$ICONSET/icon_128x128@2x.png"  > /dev/null
sips -z 256  256  "$TMP_PNG" --out "$ICONSET/icon_256x256.png"     > /dev/null
sips -z 512  512  "$TMP_PNG" --out "$ICONSET/icon_256x256@2x.png"  > /dev/null
sips -z 512  512  "$TMP_PNG" --out "$ICONSET/icon_512x512.png"     > /dev/null
cp "$TMP_PNG"                     "$ICONSET/icon_512x512@2x.png"

echo "→ Convirtiendo a .icns..."
iconutil -c icns "$ICONSET" -o "$ICNS"

echo "→ Limpiando temporales..."
rm -rf "$ICONSET"
rm -f "$TMP_PNG"

echo "✓ Generado: $ICNS ($(du -h "$ICNS" | cut -f1))"
