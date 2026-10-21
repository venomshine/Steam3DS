#!/bin/sh
# Compila con la imagen oficial de devkitPro (Docker). Uso: ./mk.sh [DEV=1] [clean]
set -e
docker info >/dev/null 2>&1 || { nohup setsid dockerd >/tmp/dockerd.log 2>&1 & sleep 5; }
exec docker run --rm -v "$PWD":/src -w /src mirror.gcr.io/devkitpro/devkitarm make -j4 "$@"
