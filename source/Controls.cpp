#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace bluebook
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
double linear( float v, double low, double high )
{
	return low + ( high - low ) * clamp01( v );
}

constexpr double kStepRateLow = 1.0, kStepRateHigh = 30.0;
constexpr double kMagnifyLow = 1.5, kMagnifyHigh = 24.0;
constexpr double kStretchWidthLow = 0.04, kStretchWidthHigh = 0.8;
constexpr double kAuraLow = 0.003, kAuraHigh = 0.12;
constexpr double kTorchLow = 0.04, kTorchHigh = 0.8;
constexpr double kSweepSlow = 20.0, kSweepFast = 0.5;
constexpr double kCaptionLow = 0.015, kCaptionHigh = 0.15;
constexpr double kHoldLow = 0.25, kHoldHigh = 10.0;

const char* const kStepOnNames[]       = { "Rate", "Beat", "1/2 Beat", "1/4 Beat", "Bar" };
const char* const kEnlargeRunNames[]   = { "Manual", "Cycle" };
const char* const kAuraKeyNames[]      = { "Bright", "Dark", "Alpha" };
const char* const kAuraColourNames[]   = { "Blue", "White", "Green", "Red", "Violet" };
const char* const kTintNames[]         = { "Teal", "Green", "Blue", "Amber", "Silver", "Natural" };
const char* const kCaptionStyleNames[] = { "Title", "Legend" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0 };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count };
}
ControlInfo of( unsigned int id, const char* name, const char* group, Kind kind, float value )
{
	return { id, name, group, kind, value, nullptr, 0 };
}
ControlInfo integer( unsigned int id, const char* name, const char* group, float value, float minimum, float maximum )
{
	return { id, name, group, Kind::Integer, value, nullptr, 0, minimum, maximum };
}
ControlInfo text( unsigned int id, const char* name, const char* group, const char* value )
{
	return { id, name, group, Kind::Text, 0.0f, nullptr, 0, 0.0f, 1.0f, value };
}

//---------------------------------------------------------------------------
// The defaults: what the intro does to every shot. A cold teal grade with
// crushed blacks, grain, a flickering exposure and a dark edge, and the clip
// step-printed at eight prints a second with a short dissolve between them.
// Everything that belongs to one shot -- the zoom, the stretched face, the
// glowing hand, the torch, the caption and the freeze -- starts off.
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	standard( PT_STUTTER, "Stutter", "Print", 1.0f ),
	option( PT_STEP_ON, "Step On", "Print", 0.0f, kStepOnNames, static_cast< int >( StepOn::Count ) ),
	standard( PT_STEP_RATE, "Step Rate", "Print", ParamFromStepRate( 8.0 ) ),
	standard( PT_DISSOLVE, "Dissolve", "Print", 0.35f ),
	standard( PT_WEAVE, "Weave", "Print", 0.3f ),
	standard( PT_TRAIL, "Trail", "Print", 0.0f ),

	standard( PT_ENLARGE, "Enlarge", "Enlarge", 0.0f ),
	option( PT_ENLARGE_RUN, "Enlarge Run", "Enlarge", 0.0f, kEnlargeRunNames, static_cast< int >( EnlargeRun::Count ) ),
	standard( PT_MAGNIFY, "Magnify", "Enlarge", ParamFromMagnify( 8.0 ) ),
	integer( PT_GENERATIONS, "Generations", "Enlarge", 4.0f, 1.0f, 8.0f ),
	of( PT_ENLARGE_X, "Enlarge X", "Enlarge", Kind::XPos, 0.5f ),
	of( PT_ENLARGE_Y, "Enlarge Y", "Enlarge", Kind::YPos, 0.5f ),

	standard( PT_STRETCH, "Stretch", "Stretch", 0.0f ),
	standard( PT_STRETCH_WIDTH, "Stretch Width", "Stretch", 0.45f ),
	standard( PT_WRITHE, "Writhe", "Stretch", 0.3f ),
	of( PT_STRETCH_X, "Stretch X", "Stretch", Kind::XPos, 0.5f ),
	of( PT_STRETCH_Y, "Stretch Y", "Stretch", Kind::YPos, 0.5f ),

	standard( PT_AURA, "Aura", "Aura", 0.0f ),
	standard( PT_AURA_SIZE, "Aura Size", "Aura", ParamFromAuraSize( 0.025 ) ),
	option( PT_AURA_KEY, "Aura Key", "Aura", 0.0f, kAuraKeyNames, static_cast< int >( AuraKey::Count ) ),
	standard( PT_AURA_THRESHOLD, "Aura Threshold", "Aura", 0.5f ),
	option( PT_AURA_COLOUR, "Aura Colour", "Aura", 0.0f, kAuraColourNames, static_cast< int >( AuraColour::Count ) ),
	standard( PT_CORONA, "Corona", "Aura", 0.5f ),
	standard( PT_SILHOUETTE, "Silhouette", "Aura", 0.0f ),

	standard( PT_TORCH, "Torch", "Torch", 0.0f ),
	standard( PT_TORCH_SIZE, "Torch Size", "Torch", ParamFromTorchSize( 0.22 ) ),
	standard( PT_TORCH_SWEEP, "Torch Sweep", "Torch", 0.5f ),
	standard( PT_TORCH_ANGLE, "Torch Angle", "Torch", 0.0f ),
	of( PT_TORCH_X, "Torch X", "Torch", Kind::XPos, 0.5f ),
	of( PT_TORCH_Y, "Torch Y", "Torch", Kind::YPos, 0.5f ),

	option( PT_TINT, "Tint", "Film", 0.0f, kTintNames, static_cast< int >( Tint::Count ) ),
	standard( PT_TINT_AMOUNT, "Tint Amount", "Film", 0.85f ),
	standard( PT_CONTRAST, "Contrast", "Film", 0.5f ),
	standard( PT_GRAIN, "Grain", "Film", 0.35f ),
	standard( PT_GRAIN_SIZE, "Grain Size", "Film", 0.35f ),
	standard( PT_FLICKER, "Flicker", "Film", 0.3f ),
	standard( PT_VIGNETTE, "Vignette", "Film", 0.5f ),

	text( PT_CAPTION, "Caption", "Caption", kDefaultCaption ),
	standard( PT_CAPTION_SHOW, "Caption Show", "Caption", 0.0f ),
	option( PT_CAPTION_STYLE, "Caption Style", "Caption", 0.0f, kCaptionStyleNames, static_cast< int >( CaptionStyle::Count ) ),
	standard( PT_CAPTION_SIZE, "Caption Size", "Caption", ParamFromCaptionSize( 0.045 ) ),
	standard( PT_CAPTION_SPACING, "Caption Spacing", "Caption", 0.4f ),
	standard( PT_CAPTION_GLOW, "Caption Glow", "Caption", 0.5f ),
	of( PT_CAPTION_Y, "Caption Y", "Caption", Kind::YPos, 0.5f ),

	of( PT_FREEZE, "Freeze", "Freeze", Kind::Event, 0.0f ),
	standard( PT_FREEZE_HOLD, "Freeze Hold", "Freeze", 0.45f ),
	standard( PT_FREEZE_FADE, "Freeze Fade", "Freeze", 0.2f ),
	standard( PT_FREEZE_FLASH, "Freeze Flash", "Freeze", 0.6f ),
	standard( PT_FREEZE_PUSH, "Freeze Push", "Freeze", 0.3f ),

	standard( PT_MIX, "Mix", "Output", 1.0f ),
};

static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_FIRST, "every control has one row" );
} // namespace

const char* const kDefaultCaption = "NOTHING TO SEE HERE";

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_FIRST - 1 ) ];
}

int OptionIndex( float value, int count )
{
	if( !std::isfinite( value ) )
		return 0;
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

double StepRateFromParam( float value )
{
	return geometric( value, kStepRateLow, kStepRateHigh );
}
float ParamFromStepRate( double perSecond )
{
	return inverseGeometric( perSecond, kStepRateLow, kStepRateHigh );
}

double WeaveFromParam( float value )
{
	return linear( value, 0.0, 0.012 );
}

double TrailHalfLifeFromParam( float value )
{
	const double v = clamp01( value );
	return 1.5 * v * v;
}

double MagnifyFromParam( float value )
{
	return geometric( value, kMagnifyLow, kMagnifyHigh );
}
float ParamFromMagnify( double times )
{
	return inverseGeometric( times, kMagnifyLow, kMagnifyHigh );
}

double StretchFromParam( float value )
{
	return linear( value, 1.0, 5.0 );
}

double StretchWidthFromParam( float value )
{
	return geometric( value, kStretchWidthLow, kStretchWidthHigh );
}

double AuraSizeFromParam( float value )
{
	return geometric( value, kAuraLow, kAuraHigh );
}
float ParamFromAuraSize( double heights )
{
	return inverseGeometric( heights, kAuraLow, kAuraHigh );
}

double TorchSizeFromParam( float value )
{
	return geometric( value, kTorchLow, kTorchHigh );
}
float ParamFromTorchSize( double heights )
{
	return inverseGeometric( heights, kTorchLow, kTorchHigh );
}

double TorchSweepSecondsFromParam( float value )
{
	if( !( value > 0.0f ) )
		return 0.0;
	return geometric( value, kSweepSlow, kSweepFast );
}

double TorchAngleFromParam( float value )
{
	return linear( value, 0.0, 360.0 );
}

double GrainSizeFromParam( float value )
{
	return geometric( value, 1.0, 4.0 );
}

double CaptionSizeFromParam( float value )
{
	return geometric( value, kCaptionLow, kCaptionHigh );
}
float ParamFromCaptionSize( double heights )
{
	return inverseGeometric( heights, kCaptionLow, kCaptionHigh );
}

double CaptionSpacingFromParam( float value )
{
	return linear( value, 0.0, 0.6 );
}

double FreezeHoldFromParam( float value )
{
	if( value >= 1.0f )
		return 0.0;
	return geometric( value, kHoldLow, kHoldHigh );
}

double FreezeFadeFromParam( float value )
{
	return linear( value, 0.0, 1.5 );
}

double FreezePushFromParam( float value )
{
	return linear( value, 0.0, 0.2 );
}

} // namespace bluebook
