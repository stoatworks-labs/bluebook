#pragma once

#include "Controls.h"
#include "PassBuffer.h"
#include "Shaders.h"

#include <FFGLSDK.h>

// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace bluebook
{
/// One glyph as the composite draws it: its code, where its cell's top-left
/// corner lands (output pixels, y up) and how far it has faded in.
struct PlacedGlyph
{
	int code;
	float left;
	float top;
	float reveal;
};

/// The caption, laid out.
struct CaptionLayout
{
	std::vector< PlacedGlyph > glyphs;
	float scale = 1.0f;///< output pixels per atlas pixel
	float box[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

/// What the CPU decided for a frame. The harness reads it back; nothing in
/// the plugin's own operation does.
struct FrameState
{
	long long steps   = 0;  ///< prints taken since the instance started
	double fade       = 1.0;///< the dissolve, 0 the print before, 1 the newest
	double decay      = 0.0;
	bool captured     = false;///< a print was taken this frame
	bool frozen       = false;
	double grey       = 0.0;
	double flash      = 0.0;
	double push       = 1.0;
	double generation = 0.0;///< how far in the enlargement is, in generations
	double magnify    = 1.0;
	double scale      = 1.0;///< the frame pass's output-to-source scale
	double biasX      = 0.0;
	double biasY      = 0.0;
	double torchX     = 0.0;///< frame heights
	double torchY     = 0.0;
	double exposure   = 1.0;
	int layerCount    = 1;
	float layerScale[ shaders::kMaxLayers ]  = {};
	float layerWeight[ shaders::kMaxLayers ] = {};
	bool reallocated  = false;
};

/**
    The plugin.

    Everything the intro does to a picture, as stages that stack: a step
    printer (Print), a photo-interpretation zoom (Enlarge), the stretched face
    (Stretch), the glowing hand (Aura), a torch in the dark (Torch), the grade
    (Film) and the caption; and the freeze-frame hit of a different
    procedural (Freeze). See AGENTS.md for why each is built the way it is.
*/
class BluebookPlugin : public CFFGLPlugin
{
public:
	BluebookPlugin();

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;

	/// Caption is a real text parameter; the About line is display-only but
	/// MUST accept its own default: instantiateGL pushes every declared
	/// default back through the setters on a fresh instance and deletes the
	/// instance if one fails, and CFFGLPlugin's SetTextParameter is a stub
	/// that returns exactly that failure.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char* GetTextParameter( unsigned int index ) override;

	FFResult SetTime( double time ) override;
	void SetBeatInfo( float bpm, float barPhase ) override;

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------

	/// The harness DECLARES its clock unit (seconds) rather than letting the
	/// plugin vote on it against the wall clock: a harness renders hundreds of
	/// frames a second, which the vote would read as milliseconds.
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	void SetHooksForTest( int bits )
	{
		hooks = bits;
	}
	const FrameState& LastFrame() const
	{
		return state;
	}
	const CaptionLayout& LastCaption() const
	{
		return layout;
	}

	/// The caption's layout for a raster, as ProcessOpenGL makes it.
	CaptionLayout LayOutCaption( int width, int height, const std::string& text, long long steps ) const;

private:
	void UpdateClock();
	void UpdateSteps( double dt );
	bool ensureBuffers( GLsizei width, GLsizei height );
	void copyInto( PassBuffer& target, GLuint texture, float maxU, float maxV, GLsizei width, GLsizei height, float overscan,
	               float offsetX, float offsetY );

	float params[ PT_COUNT ] = {};

	mutable std::mutex textMutex;
	std::string captionText;

	ffglex::FFGLShader programs[ static_cast< int >( shaders::Pass::Count ) ];
	ffglex::FFGLShader& program( shaders::Pass pass )
	{
		return programs[ static_cast< int >( pass ) ];
	}
	ffglex::FFGLScreenQuad quad;

	PassBuffer held[ 2 ]; ///< the two newest prints
	PassBuffer trail[ 2 ];///< the print stage's result, this frame's and last
	PassBuffer frozen;    ///< what was on screen when Freeze was pressed
	PassBuffer framed;    ///< the geometry's result, mipmapped for the mask
	PassBuffer mask;      ///< quarter size
	PassBuffer blurA;
	PassBuffer blurB;
	GLuint atlasTexture = 0;
	int newest          = 0;///< which held print is the newer
	int current         = 0;///< which trail buffer holds the last result
	int glowWidth       = 0;
	int glowHeight      = 0;

	FrameState state;
	CaptionLayout layout;

	//-------------------------------------------------------------------
	// Time. See rosette: the host's clock unit is voted on against the wall
	// clock, because Resolume has sent both seconds and milliseconds.
	//-------------------------------------------------------------------
	double hostTime     = -1.0;
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	double wallStart    = -1.0;
	double clockScale   = 0.0;
	int secondsVotes    = 0;
	int millisVotes     = 0;
	int clockFrames     = 0;
	double now          = 0.0;
	double lastNow      = -1.0;
	bool settledJump    = false;
	/// Seconds this instance has run, by its own dt: what every animation
	/// reads, so a clock that jumps (or overflows a float) moves nothing.
	double elapsed = 0.0;

	//-------------------------------------------------------------------
	// The step printer's clock, and the beat.
	//-------------------------------------------------------------------
	double stepPosition  = 0.0;///< in prints; a print is taken when its floor moves
	long long stepCount  = 0;
	bool stepPrimed      = false;
	float bpm            = 120.0f;
	float barPhase       = 0.0f;
	float lastBarPhase   = -1.0f;
	long long bars       = 0;
	bool hostBeatSeen    = false;

	//-------------------------------------------------------------------
	// The freeze.
	//-------------------------------------------------------------------
	int freezePresses   = 0;
	bool freezeHeld     = false;
	bool freezing       = false;
	double freezeAge    = 0.0;

	double torchPhase = 0.0;
	int hooks         = 0;
};

} // namespace bluebook
