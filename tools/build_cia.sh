#!/bin/sh
# Empaqueta Steam3DS.cia a partir del .elf ya compilado (./mk.sh).
# Necesita bannertool y makerom: TOOLS=/ruta/a/bin ./tools/build_cia.sh
set -e
cd "$(dirname "$0")/.."

VER_MAJOR=1
VER_MINOR=0
VER_MICRO=0
VER=$(( (VER_MAJOR << 10) | (VER_MINOR << 4) | VER_MICRO ))

TOOLS=${TOOLS:-}
BT=${TOOLS:+$TOOLS/}bannertool
MR=${TOOLS:+$TOOLS/}makerom
command -v "$BT" >/dev/null || { echo "falta bannertool"; exit 1; }
command -v "$MR" >/dev/null || { echo "falta makerom"; exit 1; }
[ -f Steam3DS.elf ] || { echo "falta Steam3DS.elf: compila antes con ./mk.sh"; exit 1; }
grep -q "APP_VERSION *\"$VER_MAJOR.$VER_MINOR.$VER_MICRO\"" source/app.h || { echo "la versión no coincide con source/app.h"; exit 1; }

mkdir -p build
# Siempre desde cero: si bannertool fallara, no se colaría un banner viejo
rm -f build/banner.bnr build/icon.icn Steam3DS.cia
"$BT" makebanner -i meta/banner.png -a meta/banner.wav -o build/banner.bnr
"$BT" makesmdh -s "Steam3DS" -l "Tienda de Steam en tu 3DS" -p "VENOMSHiNE" -i meta/icon.png -o build/icon.icn
[ -s build/banner.bnr ] && [ -s build/icon.icn ] || { echo "bannertool no generó el banner o el icono"; exit 1; }
"$MR" -f cia -o Steam3DS.cia -elf Steam3DS.elf -rsf meta/app.rsf -icon build/icon.icn -banner build/banner.bnr \
	-exefslogo -target t -ver "$VER"
echo "Steam3DS.cia v$VER_MAJOR.$VER_MINOR.$VER_MICRO (TMD $VER)"
