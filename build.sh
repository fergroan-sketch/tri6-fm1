#!/bin/sh
# Build TRI-6 FM-1 on a Mac or Linux: run from the FM1 folder that holds fm1-base/ and tri6-fm1/.
#   sh tri6-fm1/build.sh          the firmware: tri6-fm1/app/build/tri6fm1.fwsc
#   sh tri6-fm1/build.sh host     the host simulator, then its checks
set -e
cd "$(dirname "$0")/.."
export PATH="$HOME/.docker/bin:$PATH"
export AC79_SDK="${AC79_SDK:-$PWD/fm1-base/.toolchain/ac79-sdk-full}"
PY=fm1-base/.venv/bin/python
[ -x "$PY" ] || PY=python3
if [ "$1" = host ]; then
    "$PY" fm1-base/tools/build.py tri6-fm1/app --host
    for t in basic features; do
        tri6-fm1/app/build/host/tri6fm1_host "tri6-fm1/app/tests/$t.txt" /tmp/tri6out
    done
else
    "$PY" fm1-base/tools/build.py tri6-fm1/app
fi
