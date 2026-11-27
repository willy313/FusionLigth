#!/bin/sh
# Portable checks, no Fusion needed. Run on Linux/macOS with any C++17 compiler.
#   1. core unit tests + benchmark
#   2. extension layer against a mock of the DarkEdif API (both with and without object access)
#   3. spec/C++ consistency, and the CI assembly script against a mock template
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
GEN=extension/generated_for_tests
mkdir -p "$GEN"
python3 extension/gen_ext_json.py --selfcheck
python3 - <<'PY'
import sys, json
sys.path.insert(0, "extension")
import gen_ext_json as g
open("extension/generated_for_tests/LumenMembers.inc", "w").write(g.members_inc())
open("extension/generated_for_tests/LumenCtor.inc", "w").write(g.ctor_inc())
PY
INC="-Itests/mock -Icore -Iextension -I$GEN"
$CXX -std=c++17 -O2 -Wall -Wextra -o /tmp/lumen_core_tests tests/test_core.cpp core/lumen.cpp
/tmp/lumen_core_tests
echo "extension mock test"
$CXX -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -DFUSION_INTERNAL_ACCESS $INC -o /tmp/lumen_ext_tests tests/test_ext.cpp extension/LumenExt.cpp core/lumen.cpp
/tmp/lumen_ext_tests
echo "png encoder"
rm -rf /tmp/lumen_png && mkdir -p /tmp/lumen_png
$CXX -std=c++17 -O1 -Wall -Wextra -o /tmp/lumen_png_gen tests/test_png.cpp
/tmp/lumen_png_gen /tmp/lumen_png
python3 tests/check_png.py /tmp/lumen_png | tail -1
python3 ci/test_assemble.py | tail -1
rm -rf "$GEN"
