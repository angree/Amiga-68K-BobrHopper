#!/bin/sh
# Build the Amiga game and print only what matters: the stage headings, any compiler error, any ICE-ladder note,
# and the deploy line. Exists because quoting a grep pattern through PowerShell -> wsl.exe -> sh loses the quotes
# (docs/LEFTOFF_AMIGA.md: "cudzysłowy w linii komend giną między PowerShell a wsl.exe").
cd "$(dirname "$0")/.." || exit 1
sh build/build_amiga.sh "${1:-game}" > /tmp/bh_amiga_build.log 2>&1
code=$?
grep -E "ERROR|error:|internal compiler|needed -f|needed -O|^=== |deployed" /tmp/bh_amiga_build.log | tail -40
echo "build_amiga exit=$code  (full log: /tmp/bh_amiga_build.log)"
exit $code
