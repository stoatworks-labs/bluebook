#!/usr/bin/env bash
#
# Every shader through glslc, the one shader check that needs no GL driver.
#
# The shaders come from `bbtest --shaders`, which writes each pass exactly as
# the plugin assembles it (Shaders.cpp's Assemble(), the generated constants
# included) and needs no GL context. So nothing here re-parses the C++, and
# there is no list of pieces to drift out of date.
#
#     tools/glslc.sh [BBTEST]          exit 0 when every shader compiles, or glslc is absent
#     GLSLC_REQUIRED=1 tools/glslc.sh  ...and a missing glslc is a failure
#
# --target-env=opengl4.5 with -fauto-map-locations: glslc targets SPIR-V, which
# demands an explicit layout( location ) on every uniform and varying. Those
# are Vulkan's rules, not GLSL's, and without the flag every shader "fails" for
# reasons that have nothing to do with the code.
#
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

BBTEST="${1:-build/bbtest}"

if ! command -v glslc >/dev/null 2>&1; then
	if [ "${GLSLC_REQUIRED:-0}" = 1 ]; then
		printf '   glslc is required here and not installed\n'
		exit 1
	fi
	printf '   skipped: glslc not installed (brew install shaderc)\n'
	exit 0
fi
[ -x "$BBTEST" ] || { printf '   no harness at %s: build it first\n' "$BBTEST"; exit 1; }

dir="$( mktemp -d )"
trap 'rm -rf "$dir"' EXIT
"$BBTEST" --shaders "$dir" || { printf '   bbtest --shaders failed\n'; exit 1; }

bad=0 n=0
for shader in "$dir"/*.vert "$dir"/*.frag; do
	[ -e "$shader" ] || continue
	n=$(( n + 1 ))
	if ! glslc --target-env=opengl4.5 -fauto-map-locations "$shader" -o /dev/null 2>"$dir/err"; then
		printf '   %s does not compile\n' "$( basename "$shader" )"
		sed "s|$dir/||; s|^|      |" "$dir/err"
		bad=$(( bad + 1 ))
	fi
done

# The vertex shader and one fragment shader per pass. Fewer is the harness
# having lost a pass, and a check that silently looks at less is worse than
# none.
if [ "$n" -lt 7 ]; then
	printf '   only %d shaders came out of bbtest --shaders (want 7)\n' "$n"
	exit 1
fi
[ "$bad" -eq 0 ] && printf '   %d shaders, all compile\n' "$n"
exit "$bad"
