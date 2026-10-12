#!/bin/sh
# Validates every shader the renderer generated as WebGL2 would compile it.
#   PETARI_GLSL_DUMP=/tmp/glsl ./build/native/smg --game ~/smg/game ...   (play a while)
#   tools/bench/validate_glsl.sh /tmp/glsl
# Needs glslangValidator (brew install glslang / apt install glslang-tools).
dir=${1:?usage: $0 <PETARI_GLSL_DUMP dir>}
n=0; fail=0
for f in "$dir"/*.vert "$dir"/*.frag; do
  [ -f "$f" ] || continue
  n=$((n + 1))
  if ! out=$(glslangValidator -S "${f##*.}" "$f" 2>&1); then
    fail=$((fail + 1))
    echo "FAIL $f"; echo "$out" | grep ERROR | head -5
  fi
done
echo "$n shaders, $fail failed"
[ "$fail" -eq 0 ] && [ "$n" -gt 0 ]
