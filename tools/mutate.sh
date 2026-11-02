#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders would pass every GLSL mutant here.
#
# Each mutant is a copy of the tree in a temporary directory (the FFGL SDK is
# symlinked, not copied), built arm64-only, with only the named check run at
# 320x180, under a time limit (a mutant can hang; a timeout counts as caught).
#
#     tools/mutate.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one.
MUTANTS=(
	"source/Shaders.cpp|Q.y           = StretchCentre.y + d.y / k;|Q.y           = StretchCentre.y + d.y * k;|--stretch|GLSL: the face squashed, / -> *"
	"source/Shaders.cpp|TorchAmount * ( 0.03 + 1.25 * beam )|TorchAmount * ( 0.04 + 1.25 * beam )|--torch|GLSL: the room keeps 4 % of its light, 3 -> 4"
	"source/Shaders.cpp|y       = mix( y, y * y * ( 3.0 - 2.0 * y ), 0.6 );|y       = mix( y, y * y * ( 3.0 - 2.0 * y ), 0.7 );|--freeze|GLSL: the freeze's grey punchier than stated, 6 -> 7"
	"source/Shaders.cpp|c = mix( c, c * c * ( 3.0 - 2.0 * c ), ContrastAmount );|c = mix( c, c * c * ( 3.0 - 3.0 * c ), ContrastAmount );|--grade|GLSL: Contrast's curve not smoothstep, 2 -> 3"
	"source/Shaders.cpp|Vignette * smoothstep( 0.25, 1.05, length( fromCentre ) )|Vignette * smoothstep( 0.35, 1.05, length( fromCentre ) )|--grade|GLSL: the vignette starting further out, 2 -> 3"
	"source/Bluebook.cpp|std::clamp( into / dissolve, 0.0, 1.0 )|std::clamp( into * dissolve, 0.0, 1.0 )|--print|C++: the dissolve's fraction multiplied, / -> *"
	"source/Bluebook.cpp|case StepOn::Beat: return 1.0;|case StepOn::Beat: return 2.0;|--beat|C++: Step On Beat printing twice a beat, 1 -> 2"
	"source/Bluebook.cpp|std::pow( 0.5, dt / halfLife )|std::pow( 0.6, dt / halfLife )|--trail|C++: the smear's half-life not a half, 5 -> 6"
	"source/Bluebook.cpp|state.scale          = 1.0 / ( push * m );|state.scale          = 2.0 / ( push * m );|--enlarge|C++: the enlargement half what it says, 1 -> 2"
	"source/Bluebook.cpp|const double w = std::clamp( depth - j, 0.0, 1.0 );|const double w = std::clamp( depth - j, 0.0, 2.0 );|--grain|C++: an old generation's grain at twice its weight, 1 -> 2"
	"source/Bluebook.cpp|const double ox     = weave * signedHash( stepCount, 1u ) / aspect;|const double ox     = weave * signedHash( stepCount, 1u ) * aspect;|--weave|C++: the weave across scaled by aspect squared, / -> *"
	"source/Bluebook.cpp|const double reach  = 1.0 / ( 1.0 - 2.0 * weave );|const double reach  = 1.0 / ( 1.0 - 0.0 * weave );|--weave|C++: no overscan, so a print's edge shows, 2 -> 0"
	"source/Bluebook.cpp|AuraSizeFromParam( params[ PT_AURA_SIZE ] ) * height / 4.0 )|AuraSizeFromParam( params[ PT_AURA_SIZE ] ) * height / 2.0 )|--aura|C++: the glow's blur twice Aura Size, 4 -> 2"
	"source/Bluebook.cpp|g.top    = static_cast< float >( baseline + font::kBaseline * scale );|g.top    = static_cast< float >( baseline - font::kBaseline * scale );|--lettering|C++: glyphs hung below the baseline, + -> -"
	"source/Bluebook.cpp|constexpr double kFlashDecay = 0.08;|constexpr double kFlashDecay = 0.09;|--freeze|C++: the flash dying slower than stated, 8 -> 9"
	"source/Bluebook.cpp|std::clamp( params[ PT_MIX ], 0.0f, 1.0f )|std::clamp( params[ PT_MIX ], 0.1f, 1.0f )|--identity|C++: Mix never quite reaching 0, 0 -> 1"
	"source/Controls.cpp|return 1.5 * v * v;|return 1.6 * v * v;|--mappings|C++: Trail's half-life 7 % longer than the README says, 5 -> 6"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

caught=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	tree="$WORK/tree"
	rm -rf "$tree"
	mkdir -p "$tree/external"
	cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/fonts" "$REPO/CMakeLists.txt" "$tree/"
	ln -s "$REPO/external/ffgl" "$tree/external/ffgl"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
	cmake --build "$tree/build" --target bbtest -j"$(sysctl -n hw.ncpu)" >/dev/null 2>&1
	status=0
	limit 300 "$tree/build/bbtest" "$check" --size 320x180 >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant (exit %d):\n' "$check" "$status"
		# A mutant that timed out printed no FAIL: grep finding none must not end the run.
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "${#MUTANTS[@]}" "$caught"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]
