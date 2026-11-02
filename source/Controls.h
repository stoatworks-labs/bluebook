#pragma once

#include "StoatworksAboutLinks.h"

/**
    The host's parameters, and what they mean.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange`
    could widen it; Generations is a real integer. The conversions live in
    Controls.cpp, one function per control. Option parameters hold the element
    index; Caption is text.

    Lengths on the picture are in FRAME HEIGHTS (the frame is aspect x 1), so a
    setting means the same thing at 720p and at 4K. Time is in seconds.
*/
namespace bluebook
{
/// Every control, in the order Resolume shows them. The About block is LAST,
/// so the day a user guide adds a button no control moves.
enum ParamId : unsigned int
{
	// -- Print: the moving photograph -----------------------------------------
	PT_STUTTER = 0,
	PT_STEP_ON,
	PT_STEP_RATE,
	PT_DISSOLVE,
	PT_WEAVE,
	PT_TRAIL,

	// -- Enlarge: the photo interpretation zoom --------------------------------
	PT_ENLARGE,
	PT_ENLARGE_RUN,
	PT_MAGNIFY,
	PT_GENERATIONS,
	PT_ENLARGE_X,
	PT_ENLARGE_Y,

	// -- Stretch: the face pulled long ------------------------------------------
	PT_STRETCH,
	PT_STRETCH_WIDTH,
	PT_WRITHE,
	PT_STRETCH_X,
	PT_STRETCH_Y,

	// -- Aura: the glowing hand -------------------------------------------------
	PT_AURA,
	PT_AURA_SIZE,
	PT_AURA_KEY,
	PT_AURA_THRESHOLD,
	PT_AURA_COLOUR,
	PT_CORONA,
	PT_SILHOUETTE,

	// -- Torch: a flashlight across the dark ------------------------------------
	PT_TORCH,
	PT_TORCH_SIZE,
	PT_TORCH_SWEEP,
	PT_TORCH_ANGLE,
	PT_TORCH_X,
	PT_TORCH_Y,

	// -- Film: the grade --------------------------------------------------------
	PT_TINT,
	PT_TINT_AMOUNT,
	PT_CONTRAST,
	PT_GRAIN,
	PT_GRAIN_SIZE,
	PT_FLICKER,
	PT_VIGNETTE,

	// -- Caption ----------------------------------------------------------------
	PT_CAPTION,
	PT_CAPTION_SHOW,
	PT_CAPTION_STYLE,
	PT_CAPTION_SIZE,
	PT_CAPTION_SPACING,
	PT_CAPTION_GLOW,
	PT_CAPTION_Y,

	// -- Freeze: the procedural's freeze-frame hit -----------------------------
	PT_FREEZE,
	PT_FREEZE_HOLD,
	PT_FREEZE_FADE,
	PT_FREEZE_FLASH,
	PT_FREEZE_PUSH,

	// -- Output -----------------------------------------------------------------
	PT_MIX,

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_FIRST,
	PT_COUNT = PT_ABOUT_FIRST + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,///< 0..1, converted in Controls.cpp
	Option,  ///< an element index
	Integer, ///< a real integer, minimum..maximum
	Text,    ///< a string
	Event,   ///< a press
	XPos,    ///< 0..1 across the frame
	YPos     ///< 0..1 up the frame
};

/// One control as the host is told about it.
struct ControlInfo
{
	unsigned int id;
	const char* name; ///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< in the host's units: 0..1, the index or the integer
	const char* const* options;
	int optionCount;
	float minimum = 0.0f;///< Integer only
	float maximum = 1.0f;
	const char* text = nullptr;///< Text only: the default string
};

/// The table, indexed by ParamId, for every id below PT_ABOUT_FIRST.
const ControlInfo& InfoOf( unsigned int id );

//---------------------------------------------------------------------------
// Options.
//---------------------------------------------------------------------------
enum class StepOn
{
	Rate = 0,   ///< Step Rate prints a second
	Beat,       ///< one print a beat of Resolume's clock
	HalfBeat,   ///< one a quaver
	QuarterBeat,///< one a semiquaver
	Bar,        ///< one a bar
	Count
};

enum class EnlargeRun
{
	Manual = 0,///< Enlarge says how far in
	Cycle,     ///< one generation further in on every print, then back out
	Count
};

enum class AuraKey
{
	Bright = 0,///< the subject is what is brighter than Aura Threshold
	Dark,      ///< the subject is what is darker: a hand against a light box
	Alpha,     ///< the subject is the clip's alpha
	Count
};

enum class AuraColour
{
	Blue = 0,
	White,
	Green,
	Red,
	Violet,
	Count
};

enum class Tint
{
	Teal = 0,///< cold blue-green shadows, cool white light
	Green,   ///< the sickly fluorescent green
	Blue,    ///< night, moonlight, the lab
	Amber,   ///< the map under the lamp
	Silver,  ///< plain black and white
	Natural, ///< the clip's own colour, graded only by Contrast
	Count
};

enum class CaptionStyle
{
	Title = 0,///< centred, letters fading in from the left
	Legend,   ///< right-aligned, with a catalogue number counting the prints beneath
	Count
};

/// An option's index from the value the host stored (it reads back 0..1
/// whatever the count -- the fleet's trap -- so the table's own count is
/// what bounds it).
int OptionIndex( float value, int count );

/// The caption a fresh instance shows.
extern const char* const kDefaultCaption;

/// The longest caption drawn, in characters; the rest is dropped.
constexpr int kMaxCaption = 48;

//---------------------------------------------------------------------------
// The mappings. Each says its range and its shape.
//---------------------------------------------------------------------------

/// Prints a second at Step On Rate: 1 to 30, geometrically.
double StepRateFromParam( float value );
float ParamFromStepRate( double perSecond );

/// Weave: how far a print sits off true, as a fraction of the frame height,
/// 0 to 1.2 %, linearly. A per-frame shimmer of 12 % of that rides on top.
double WeaveFromParam( float value );

/// Trail: the smear's half-life, 0 to 1.5 s, quadratically (the short end is
/// where the detail is). 0 is no smear at all.
double TrailHalfLifeFromParam( float value );

/// Magnify: the last generation's magnification, 1.5x to 24x, geometrically.
double MagnifyFromParam( float value );
float ParamFromMagnify( double times );

/// Stretch: the most the face is pulled long, 1x to 5x, linearly.
double StretchFromParam( float value );

/// Stretch Width: how wide the pull is, 0.04 to 0.8 frame heights (the
/// Gaussian's 1/e half-width), geometrically.
double StretchWidthFromParam( float value );

/// Aura Size: the glow's standard deviation, 0.3 % to 12 % of the frame
/// height, geometrically.
double AuraSizeFromParam( float value );
float ParamFromAuraSize( double heights );

/// Torch Size: the beam's 1/e radius, 4 % to 80 % of the frame height,
/// geometrically.
double TorchSizeFromParam( float value );
float ParamFromTorchSize( double heights );

/// Torch Sweep: seconds per pass across the frame, or 0 (exactly 0 at the
/// bottom of the travel: the beam stands still at Torch X / Y). Above that,
/// 20 s down to 0.5 s, geometrically.
double TorchSweepSecondsFromParam( float value );

/// Torch Angle: the direction the beam travels, 0 to 360 degrees; 0 is left
/// to right, 90 bottom to top.
double TorchAngleFromParam( float value );

/// Grain Size: the grain's cell, 1 to 4 output pixels at 1080 lines,
/// geometrically, and in proportion to the frame height elsewhere.
double GrainSizeFromParam( float value );

/// Caption Size: the capital height, 1.5 % to 15 % of the frame height,
/// geometrically.
double CaptionSizeFromParam( float value );
float ParamFromCaptionSize( double heights );

/// Caption Spacing: letter spacing added to every advance, 0 to 0.6 em.
double CaptionSpacingFromParam( float value );

/// Freeze Hold: how long a freeze lasts, 0.25 to 10 s, geometrically; the
/// top of the travel (exactly 1) latches it until Freeze is pressed again.
/// Returns 0 for the latch.
double FreezeHoldFromParam( float value );

/// Freeze Fade: colour to grey, 0 to 1.5 s, linearly. 0 cuts.
double FreezeFadeFromParam( float value );

/// Freeze Push: how far in the camera pushes once frozen, 0 to 20 %. It
/// eases in with a 0.35 s time constant (kPushSettle), whatever the hold.
double FreezePushFromParam( float value );

} // namespace bluebook
