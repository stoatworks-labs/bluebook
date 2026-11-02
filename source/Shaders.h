#pragma once

#include <string>

/**
	The passes, in order.

	1. **capture** -- on a print only: the clip into the newer of the two held
	   prints, set a little off true (Weave) the way a re-photographed print
	   never sits where the last one did. Overscanned by the weave's reach so
	   the print's edge never shows.
	2. **print** -- picture size, RGBA16F: the two held prints dissolved into
	   each other, mixed with the live clip by Stutter, replaced by the frozen
	   frame while a freeze holds, then smeared into the last frame's result
	   (Trail). This buffer is the next frame's "last frame", so it ping-pongs.
	3. **frame** -- picture size, RGBA16F, mipmapped: the geometry. Freeze
	   Push and Enlarge are one affine map, worked out on the CPU; Stretch is
	   the face pulled long around its centre. Magnified, the print is read
	   with a cubic B-spline rather than bilinearly, because an enlargement is
	   soft, not faceted.
	4. **mask** -- quarter size, R16F: the Aura's subject, keyed off the
	   frame's mip level 2.
	5. **blur** -- quarter size, twice (across, then up): the mask's Gaussian.
	   What leaks out past the subject's edge is the glow.
	6. **composite** -- to the host: Silhouette, Contrast, Tint, the torch,
	   the aura's light, exposure flicker, vignette, grain, the freeze's grey
	   and flash, the caption, Mix.

	The fragment shaders are assembled from pieces at InitGL -- kVersion +
	kCommon + the pass -- by `Assemble()`, the one place the order is written.
	`bbtest --shaders DIR` writes what Assemble() returns, and tools/glslc.sh
	compiles that, so the text checked is the text the plugin runs.

	No sin, cos, tan or atan anywhere (GLSL promises nothing about their
	accuracy): the torch's direction comes from the CPU. Randomness is PCG
	integer hashing, never fract( sin() ).
*/
namespace bluebook::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kCommon;
extern const char* const kCaptureFragment;
extern const char* const kPrintFragment;
extern const char* const kFrameFragment;
extern const char* const kMaskFragment;
extern const char* const kBlurFragment;
extern const char* const kCompositeFragment;

enum class Pass
{
	Capture,
	Print,
	Frame,
	Mask,
	Blur,
	Composite,
	Count
};
std::string Assemble( Pass pass );
const char* NameOf( Pass pass );

/// The most glyphs a caption draws (its text and, as a legend, its number).
constexpr int kMaxGlyphs = 64;

/// The most grain layers: the live one and one per generation before it.
constexpr int kMaxLayers = 9;

/// Negative-control hooks: bits of the `Hooks` uniform. A shipped instance
/// sets 0, and every branch they guard is then the shipped model. Each one
/// exists so a harness check can be shown to fail against a wrong model.
enum Hook : int
{
	kHookSquash        = 1 << 0,///< Stretch divides where it should multiply: the face squashed
	kHookFlatGrain     = 1 << 1,///< every grain layer at the live layer's size
	kHookTorchOffset   = 1 << 2,///< the beam a tenth of a frame right of where it is aimed
	kHookGlowInside    = 1 << 3,///< the aura lights the subject, not what is round it
	kHookCaptionBottom = 1 << 4,///< glyphs hung from their cell's bottom, not its top
	kHookNoGrey        = 1 << 5,///< the freeze keeps its colour
	kHookDissolveSnap  = 1 << 6 ///< the dissolve jumps to the newer print at its halfway
};

} // namespace bluebook::shaders
