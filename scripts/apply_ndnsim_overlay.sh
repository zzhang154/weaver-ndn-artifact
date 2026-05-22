#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 /path/to/ns-3" >&2
  exit 2
fi

NS3_ROOT="$1"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ ! -d "$NS3_ROOT/src/ndnSIM" ]; then
  echo "error: $NS3_ROOT does not look like an ns-3 tree with ndnSIM installed" >&2
  exit 1
fi

mkdir -p "$NS3_ROOT/src/ndnSIM/apps/cfnagg"
mkdir -p "$NS3_ROOT/src/ndnSIM/examples/cfnagg"

rsync -a "$REPO_ROOT/src/ndnSIM/apps/cfnagg/" \
  "$NS3_ROOT/src/ndnSIM/apps/cfnagg/"

rsync -a "$REPO_ROOT/src/ndnSIM/examples/cfnagg/" \
  "$NS3_ROOT/src/ndnSIM/examples/cfnagg/"

EXAMPLES_WSCRIPT="$NS3_ROOT/src/ndnSIM/examples/wscript"
if [ ! -f "$EXAMPLES_WSCRIPT" ]; then
  cp "$REPO_ROOT/src/ndnSIM/examples/wscript" "$EXAMPLES_WSCRIPT"
elif ! grep -q "bld.recurse('cfnagg')" "$EXAMPLES_WSCRIPT"; then
  python3 - "$EXAMPLES_WSCRIPT" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
needle = "def build(bld):\n"
insert = "def build(bld):\n    bld.recurse('cfnagg')\n"
if needle not in text:
    raise SystemExit("could not find build(bld) in target examples/wscript")
path.write_text(text.replace(needle, insert, 1))
PY
fi

echo "Applied Weaver ndnSIM overlay to $NS3_ROOT"
