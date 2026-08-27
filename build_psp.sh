#!/bin/sh
# PSP build via WSL, using the WSL-native-filesystem protocol from the
# PSPHASE log: drvfs (/mnt/c) serves STALE file content after Windows-side
# writes, so sources are streamed in via stdin tar and built on ext4.
# Run from Git Bash on Windows, from the repo root:  ./build_psp.sh
#
# One-time WSL prep:
#   pspdev toolchain at /opt/pspdev
#   apt-get install -y python3-pil        (font generator needs PIL)
#
# Output: dist/PSP/EBOOT.PBP (+ INSTALL_HOW_TO.txt, mapping.xml, config.xml)
# Note: /root/pt is wiped each run = full rebuild (~2-3 min at -j8). Fine
# for now; revisit if iteration speed hurts.

set -e
cd "$(dirname "$0")"
SRC_WIN="$(pwd)"            # /c/Users/... in Git Bash
DST_WSL="/mnt${SRC_WIN}"    # /mnt/c/Users/...

# games/ holds a multi-GB local game collection and samples/ the raw
# audio work -- neither is a build input, and streaming them over the
# WSL pipe stalls the build. Exclude them explicitly.
tar -cf - --exclude=.git --exclude=dist --exclude=games --exclude=samples . | wsl -e bash -c '
  set -e
  rm -rf /root/pt && mkdir -p /root/pt && tar -xf - -C /root/pt
  cd /root/pt/projects
  export PSPDEV=/opt/pspdev
  export PATH=$PATH:/opt/pspdev/bin
  make PLATFORM=PSP -j$(nproc)
'

mkdir -p dist/PSP
wsl -e bash -c "cp /root/pt/projects/buildPSP/EBOOT.PBP '$DST_WSL/dist/PSP/'"
cp projects/resources/PSP/INSTALL_HOW_TO.txt projects/resources/PSP/mapping.xml projects/resources/PSP/config.xml projects/resources/PSP/custom_font.xml dist/PSP/ 2>/dev/null || true
echo "OK: dist/PSP/EBOOT.PBP"
