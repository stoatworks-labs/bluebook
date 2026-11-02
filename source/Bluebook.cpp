#include "Bluebook.h"

#include "Diag.h"
#include "Font.h"
#include "GLState.h"

//FFGLSDK.h includes every other scoped binding and omits this one (SDK
//b1afaf9), so it has to be asked for by name.
#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

using namespace ffglex;

namespace bluebook
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;

/// Seconds of host time a single frame may advance by. A stalled host must
/// not age a freeze or a smear by a minute in one frame.
constexpr double kMaxFrameDelta = 0.25;

/// The film the grain, the flicker and the shimmer are drawn on runs at 24.
constexpr double kFilmRate = 24.0;

/// How fast the freeze's flash dies away, and its push settles, seconds.
constexpr double kFlashDecay = 0.08;
constexpr double kPushSettle = 0.35;

/// Letters over which the caption's reveal ramps from nothing to full.
constexpr double kRevealLetters = 3.0;

constexpr double kPi = 3.14159265358979323846;

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

/// glGetString returns nullptr when there is no current context.
std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

uint32_t pcg( uint32_t v )
{
	const uint32_t state = v * 747796405u + 2891336453u;
	const uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}
/// The same hash as the GLSL's hash01, on the CPU: 0..1.
double hash01( uint32_t a, uint32_t b )
{
	return pcg( a * 2654435761u ^ pcg( b + 0x9e3779b9u ) ) * ( 1.0 / 4294967296.0 );
}
/// -1..1.
double signedHash( long long a, uint32_t b )
{
	return 2.0 * hash01( static_cast< uint32_t >( a ), b ) - 1.0;
}

struct Duotone
{
	float dark[ 3 ];
	float light[ 3 ];
};
/// The tints, as code values: shadows to light along the luma.
const Duotone kTints[] = {
	{ { 0.000f, 0.035f, 0.050f }, { 0.80f, 0.97f, 0.95f } },// Teal
	{ { 0.010f, 0.050f, 0.020f }, { 0.80f, 1.00f, 0.78f } },// Green
	{ { 0.000f, 0.020f, 0.070f }, { 0.78f, 0.88f, 1.00f } },// Blue
	{ { 0.060f, 0.025f, 0.000f }, { 1.00f, 0.86f, 0.60f } },// Amber
	{ { 0.000f, 0.000f, 0.000f }, { 0.97f, 0.97f, 0.97f } },// Silver
	{ { 0.000f, 0.000f, 0.000f }, { 1.00f, 1.00f, 1.00f } },// Natural: never mixed in
};
static_assert( sizeof( kTints ) / sizeof( kTints[ 0 ] ) == static_cast< size_t >( Tint::Count ), "one duotone per tint" );

const float kAuraColours[][ 3 ] = {
	{ 0.35f, 0.60f, 1.00f },// Blue
	{ 0.90f, 0.95f, 1.00f },// White
	{ 0.40f, 1.00f, 0.50f },// Green
	{ 1.00f, 0.25f, 0.15f },// Red
	{ 0.70f, 0.40f, 1.00f },// Violet
};
static_assert( sizeof( kAuraColours ) / sizeof( kAuraColours[ 0 ] ) == static_cast< size_t >( AuraColour::Count ),
               "one colour per aura" );

/// Prints a beat: what each Step On but Rate divides a beat into.
double printsPerBeat( StepOn on )
{
	switch( on )
	{
	case StepOn::Beat: return 1.0;
	case StepOn::HalfBeat: return 2.0;
	case StepOn::QuarterBeat: return 4.0;
	case StepOn::Bar: return 0.25;
	default: return 1.0;
	}
}
} // namespace

//---------------------------------------------------------------------------
// The buttons are declared one per link, so the run in the enum and the run the
// block actually has must agree.
static_assert( PT_COUNT - PT_ABOUT_FIRST == stoatworks::about::kParamCount,
               "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
BluebookPlugin::BluebookPlugin()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	//Prints, dissolves, smears, sweeps and freezes all run in seconds.
	SetTimeSupported( true );

	captionText = kDefaultCaption;

	//---------------------------------------------------------------------
	// Declaration, from the table in Controls.cpp. Every ranged parameter is
	// a plain 0..1 float: SetParamInfo clamps a STANDARD default into 0..1
	// before a range could be attached (SDK b1afaf9). Generations is a real
	// integer, which SetParamInfo leaves alone.
	//---------------------------------------------------------------------
	for( unsigned int id = 0; id < PT_ABOUT_FIRST; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		params[ id ]          = info.defaultValue;
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( id, info.name, FF_TYPE_STANDARD, info.defaultValue ); break;
		case Kind::XPos: SetParamInfo( id, info.name, FF_TYPE_XPOS, info.defaultValue ); break;
		case Kind::YPos: SetParamInfo( id, info.name, FF_TYPE_YPOS, info.defaultValue ); break;
		case Kind::Integer:
			SetParamInfo( id, info.name, FF_TYPE_INTEGER, info.defaultValue );
			SetParamRange( id, info.minimum, info.maximum );
			break;
		case Kind::Option:
			SetOptionParamInfo( id, info.name, static_cast< unsigned int >( info.optionCount ), info.defaultValue );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		case Kind::Text: SetParamInfo( id, info.name, FF_TYPE_TEXT, info.text ); break;
		case Kind::Event: SetParamInfo( id, info.name, FF_TYPE_EVENT, false ); break;
		}
		SetParamGroup( id, info.group );
	}

	// The About block. Inline rather than through a helper: SetParamInfo is
	// protected on CFFGLPlugin, so nothing outside the class can call it.
	SetParamInfo( PT_ABOUT_FIRST, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_FIRST + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_FIRST; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
FFResult BluebookPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	const std::string vertex = std::string( shaders::kVersion ) + shaders::kQuadVertex;
	for( int p = 0; p < static_cast< int >( shaders::Pass::Count ); ++p )
	{
		const auto pass            = static_cast< shaders::Pass >( p );
		const std::string fragment = shaders::Assemble( pass );
		if( programs[ p ].Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. This line is the only record of which pass.
		diag::error( std::string( "the " ) + shaders::NameOf( pass ) + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Bluebook: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	//The caption's typeface. Bound and unbound by hand: nothing is bound yet,
	//and this must leave nothing.
	glGenTextures( 1, &atlasTexture );
	glBindTexture( GL_TEXTURE_2D, atlasTexture );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, font::kAtlasWidth, font::kAtlasHeight, 0, GL_RED, GL_UNSIGNED_BYTE, font::kAtlas );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );

	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
bool BluebookPlugin::ensureBuffers( GLsizei width, GLsizei height )
{
	//All of it up front, before anything is bound: every ffglex::Scoped*
	//binding clears to 0 on exit rather than restoring, and
	//FFGLFBO::Initialise binds under one.
	//Every buffer follows the raster, so a held print of another size (or
	//none) means everything is about to be new and empty. Not a new GL id:
	//a driver hands the name it just freed straight back, and a resize then
	//smeared the first frame out of nothing.
	state.reallocated = !held[ 0 ].IsValid() || held[ 0 ].GetWidth() != static_cast< GLuint >( width )
	                 || held[ 0 ].GetHeight() != static_cast< GLuint >( height );
	glowWidth  = std::max( 1, ( width + 3 ) / 4 );
	glowHeight = std::max( 1, ( height + 3 ) / 4 );
	bool ok    = true;
	for( PassBuffer& b : held )
		ok = ok && b.Ensure( width, height, GL_RGBA16F, PassBuffer::Sampling::Linear );
	//The smear is full float. A one-pole in half floats stops short of where
	//it is going -- once a frame's step is under half a half-float ULP it
	//rounds to nothing -- and at Trail 1 that is 6 % short: a ghost of every
	//bright thing that never leaves.
	for( PassBuffer& b : trail )
		ok = ok && b.Ensure( width, height, GL_RGBA32F, PassBuffer::Sampling::Linear );
	ok = ok && frozen.Ensure( width, height, GL_RGBA16F, PassBuffer::Sampling::Linear );
	ok = ok && framed.Ensure( width, height, GL_RGBA16F, PassBuffer::Sampling::Mipmapped );
	ok = ok && mask.Ensure( glowWidth, glowHeight, GL_R16F, PassBuffer::Sampling::Linear );
	ok = ok && blurA.Ensure( glowWidth, glowHeight, GL_R16F, PassBuffer::Sampling::Linear );
	ok = ok && blurB.Ensure( glowWidth, glowHeight, GL_R16F, PassBuffer::Sampling::Linear );
	return ok;
}

//---------------------------------------------------------------------------
void BluebookPlugin::copyInto( PassBuffer& target, GLuint texture, float maxU, float maxV, GLsizei width, GLsizei height,
                               float overscan, float offsetX, float offsetY )
{
	FFGLShader& shader = program( shaders::Pass::Capture );
	ScopedFBOBinding fbo( target.GetGLID(), ScopedFBOBinding::RB_REVERT );
	glViewport( 0, 0, width, height );
	ScopedShaderBinding binding( shader.GetGLID() );
	bindUnit( 0, texture );
	shader.Set( "InputTexture", 0 );
	shader.Set( "MaxUV", maxU, maxV );
	shader.Set( "Texel", 1.0f / static_cast< float >( width ), 1.0f / static_cast< float >( height ) );
	shader.Set( "Overscan", overscan );
	shader.Set( "Offset", offsetX, offsetY );
	shader.Set( "Hooks", hooks );
	quad.Draw();
	unbindTextureUnits( 1 );
}

//---------------------------------------------------------------------------
void BluebookPlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;

	const double raw = hostTime;

	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime, and nothing in the call says which. Vote on it against the wall
	//clock for a few frames, then stop asking. (Rosette's code, by way of
	//millpond and schlieren.)
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;

			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale = millisVotes > secondsVotes ? 0.001 : 1.0;
				//The clock is about to jump from wall time to the host's own,
				//which can be hours in: no dt across the jump.
				settledJump = true;
			}
		}
	}

	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;

	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( now ) );
}

//---------------------------------------------------------------------------
void BluebookPlugin::UpdateSteps( double dt )
{
	const auto on      = static_cast< StepOn >( OptionIndex( params[ PT_STEP_ON ], static_cast< int >( StepOn::Count ) ) );
	const double prior = stepPosition;

	if( on == StepOn::Rate )
	{
		const double rate = StepRateFromParam( params[ PT_STEP_RATE ] );
		stepPosition += dt * rate;
		stepDuration = 1.0 / rate;
	}
	else
	{
		const double perBeat = printsPerBeat( on );
		stepDuration         = 60.0 / std::max( 1.0f, bpm ) / perBeat;
		//On the host's bar when it sends one, so a print lands ON the beat;
		//on its tempo alone when it does not.
		if( hostBeatSeen )
			stepPosition = ( static_cast< double >( bars ) + barPhase ) * 4.0 * perBeat;
		else
			stepPosition += dt * bpm / 60.0 * perBeat;
	}

	state.captured = false;
	if( !stepPrimed )
	{
		stepPrimed = true;
		return;
	}
	//However far the position moved (a mode change can move it anywhere), one
	//print: there is only one frame to take it from.
	if( std::floor( stepPosition ) != std::floor( prior ) )
	{
		++stepCount;
		state.captured = true;
	}
}

//---------------------------------------------------------------------------
CaptionLayout BluebookPlugin::LayOutCaption( int width, int height, const std::string& text, long long steps, double exposure ) const
{
	(void)exposure;
	CaptionLayout out;
	const auto style   = static_cast< CaptionStyle >( OptionIndex( params[ PT_CAPTION_STYLE ], static_cast< int >( CaptionStyle::Count ) ) );
	const double cap   = CaptionSizeFromParam( params[ PT_CAPTION_SIZE ] ) * height;
	const double scale = cap / font::kCapHeight;
	const double em    = font::kEm * scale;
	const double gap   = CaptionSpacingFromParam( params[ PT_CAPTION_SPACING ] ) * em;
	out.scale          = static_cast< float >( scale );

	std::vector< std::string > lines;
	lines.push_back( text.substr( 0, static_cast< size_t >( kMaxCaption ) ) );
	if( style == CaptionStyle::Legend )
	{
		//A catalogue number that moves on with every print, as the stills'
		//did, so a run of prints reads as a run of exhibits.
		char number[ 32 ];
		std::snprintf( number, sizeof( number ), "NO. %04lld-%02u", ( 1013 + steps ) % 10000,
		               static_cast< unsigned >( pcg( static_cast< uint32_t >( steps ) ) % 100u ) );
		lines.emplace_back( number );
	}

	int total = 0;
	for( const std::string& line : lines )
		total += static_cast< int >( line.size() );
	const double show = std::clamp( static_cast< double >( params[ PT_CAPTION_SHOW ] ), 0.0, 1.0 );
	const double head = show * ( total + kRevealLetters );

	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	int index = 0;
	for( size_t k = 0; k < lines.size(); ++k )
	{
		const std::string& line = lines[ k ];
		double widthOf          = 0.0;
		for( char ch : line )
			widthOf += font::AdvanceOf( static_cast< unsigned char >( ch ) ) * em + gap;
		if( !line.empty() )
			widthOf -= gap;

		double pen = style == CaptionStyle::Legend ? width * 0.94 - widthOf : 0.5 * ( width - widthOf );
		//Caption Y is where the first line's capitals are centred.
		const double baseline = params[ PT_CAPTION_Y ] * height - 0.5 * cap - static_cast< double >( k ) * 1.9 * cap;
		for( char ch : line )
		{
			int code = static_cast< unsigned char >( ch );
			if( code < font::kFirst || code > 126 )
				code = '?';
			const double reveal = std::clamp( ( head - index ) / kRevealLetters, 0.0, 1.0 );
			if( code != ' ' && reveal > 0.0 && static_cast< int >( out.glyphs.size() ) < shaders::kMaxGlyphs )
			{
				PlacedGlyph g;
				g.code   = code;
				g.left   = static_cast< float >( pen - font::kPenX * scale );
				g.top    = static_cast< float >( baseline + font::kBaseline * scale );
				g.reveal = static_cast< float >( reveal );
				out.glyphs.push_back( g );
				x0 = std::min( x0, static_cast< double >( g.left ) );
				x1 = std::max( x1, g.left + font::kCellWidth * scale );
				y1 = std::max( y1, static_cast< double >( g.top ) );
				y0 = std::min( y0, g.top - font::kCellHeight * scale );
			}
			pen += font::AdvanceOf( code ) * em + gap;
			++index;
		}
	}
	if( !out.glyphs.empty() )
	{
		out.box[ 0 ] = static_cast< float >( x0 );
		out.box[ 1 ] = static_cast< float >( y0 );
		out.box[ 2 ] = static_cast< float >( x1 );
		out.box[ 3 ] = static_cast< float >( y1 );
	}
	return out;
}

//---------------------------------------------------------------------------
FFResult BluebookPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr || pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;

	const FFGLTextureStruct& source = *( pgl->inputTextures[ 0 ] );
	const GLsizei width             = static_cast< GLsizei >( source.Width );
	const GLsizei height            = static_cast< GLsizei >( source.Height );
	if( width <= 0 || height <= 0 )
		return FF_FAIL;

	//The host's viewport and blend state, put back however this returns.
	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );

	//---------------------------------------------------------------------
	// Time and the beat.
	//---------------------------------------------------------------------
	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	const double dt = lastNow >= 0.0 ? std::clamp( now - lastNow, 0.0, kMaxFrameDelta ) : 0.0;
	lastNow         = now;
	elapsed += dt;

	if( lastBarPhase >= 0.0f && barPhase != lastBarPhase )
	{
		hostBeatSeen = true;
		if( barPhase < lastBarPhase - 0.5f )
			++bars;
	}
	lastBarPhase = barPhase;

	if( !ensureBuffers( width, height ) )
	{
		diag::error( "could not allocate the pass buffers" );
		return FF_FAIL;
	}
	const bool fresh = state.reallocated;

	UpdateSteps( dt );
	state.steps = stepCount;

	const double aspect     = static_cast< double >( width ) / static_cast< double >( height );
	const FFGLTexCoords max = GetMaxGLTexCoords( source );
	const long long film    = static_cast< long long >( std::floor( elapsed * kFilmRate ) );

	//---------------------------------------------------------------------
	// The freeze. A press freezes what is on screen; with a timed hold a
	// press while frozen hits again, latched it lets go.
	//---------------------------------------------------------------------
	const double hold   = FreezeHoldFromParam( params[ PT_FREEZE_HOLD ] );
	bool freezeCapture  = false;
	if( freezePresses > 0 )
	{
		if( freezing && hold == 0.0 )
			freezing = false;
		else
		{
			freezing      = true;
			freezeAge     = 0.0;
			freezeCapture = true;
		}
		freezePresses = 0;
	}
	else if( freezing )
	{
		freezeAge += dt;
		if( hold > 0.0 && freezeAge >= hold )
			freezing = false;
	}
	const double fadeTime = FreezeFadeFromParam( params[ PT_FREEZE_FADE ] );
	state.frozen          = freezing;
	state.grey            = freezing ? ( fadeTime > 0.0 ? std::clamp( freezeAge / fadeTime, 0.0, 1.0 ) : 1.0 ) : 0.0;
	state.flash           = freezing ? std::clamp( static_cast< double >( params[ PT_FREEZE_FLASH ] ), 0.0, 1.0 ) * std::exp( -freezeAge / kFlashDecay ) : 0.0;
	state.push            = freezing ? 1.0 + FreezePushFromParam( params[ PT_FREEZE_PUSH ] ) * ( 1.0 - std::exp( -freezeAge / kPushSettle ) ) : 1.0;

	//---------------------------------------------------------------------
	// 1. The prints. A fresh set of buffers starts from this frame, so
	//    nothing fades in from black: the prints, the freeze and the smear.
	//---------------------------------------------------------------------
	const double weave = WeaveFromParam( params[ PT_WEAVE ] );
	if( fresh )
	{
		for( PassBuffer& b : held )
			copyInto( b, source.Handle, max.s, max.t, width, height, 1.0f, 0.0f, 0.0f );
		copyInto( frozen, source.Handle, max.s, max.t, width, height, 1.0f, 0.0f, 0.0f );
	}
	else
	{
		if( freezeCapture )
			copyInto( frozen, trail[ current ].TextureID(), 1.0f, 1.0f, width, height, 1.0f, 0.0f, 0.0f );
		if( state.captured )
		{
			//A re-photographed print never sits where the last one did; the
			//overscan keeps its edge out of the frame however far it lands.
			newest              = 1 - newest;
			const double reach  = 1.0 / ( 1.0 - 2.0 * weave );
			const double ox     = weave * signedHash( stepCount, 1u ) / aspect;
			const double oy     = weave * signedHash( stepCount, 2u );
			copyInto( held[ newest ], source.Handle, max.s, max.t, width, height, static_cast< float >( reach ),
			          static_cast< float >( ox ), static_cast< float >( oy ) );
		}
	}

	const double dissolve = std::clamp( static_cast< double >( params[ PT_DISSOLVE ] ), 0.0, 1.0 );
	const double into     = stepPosition - std::floor( stepPosition );
	state.fade            = dissolve > 0.0 ? std::clamp( into / dissolve, 0.0, 1.0 ) : 1.0;
	const double halfLife = TrailHalfLifeFromParam( params[ PT_TRAIL ] );
	state.decay           = ( fresh || halfLife <= 0.0 ) ? 0.0 : std::pow( 0.5, dt / halfLife );

	const int previous = current;
	current            = 1 - current;
	{
		FFGLShader& shader = program( shaders::Pass::Print );
		ScopedFBOBinding fbo( trail[ current ].GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, width, height );
		ScopedShaderBinding binding( shader.GetGLID() );
		bindUnit( 0, source.Handle );
		bindUnit( 1, held[ newest ].TextureID() );
		bindUnit( 2, held[ 1 - newest ].TextureID() );
		bindUnit( 3, trail[ previous ].TextureID() );
		bindUnit( 4, frozen.TextureID() );
		shader.Set( "InputTexture", 0 );
		shader.Set( "Held", 1 );
		shader.Set( "HeldBefore", 2 );
		shader.Set( "TrailBefore", 3 );
		shader.Set( "Frozen", 4 );
		shader.Set( "Fade", static_cast< float >( state.fade ) );
		shader.Set( "Stutter", std::clamp( params[ PT_STUTTER ], 0.0f, 1.0f ) );
		shader.Set( "FreezeMix", freezing ? 1.0f : 0.0f );
		shader.Set( "Decay", static_cast< float >( state.decay ) );
		shader.Set( "Hooks", hooks );
		quad.Draw();
		unbindTextureUnits( 5 );
	}

	//---------------------------------------------------------------------
	// 2. The geometry. Freeze Push about the frame's centre, then the
	//    enlargement about its target, as one affine map; then the stretch.
	//---------------------------------------------------------------------
	const int generations = std::clamp( static_cast< int >( std::lround( params[ PT_GENERATIONS ] ) ), 1, 8 );
	const auto run        = static_cast< EnlargeRun >( OptionIndex( params[ PT_ENLARGE_RUN ], static_cast< int >( EnlargeRun::Count ) ) );
	const double deepest  = MagnifyFromParam( params[ PT_MAGNIFY ] );
	long long cycle       = 0;
	double depth          = 0.0;
	if( run == EnlargeRun::Cycle )
	{
		cycle = stepCount / ( generations + 1 );
		depth = static_cast< double >( stepCount % ( generations + 1 ) );
	}
	else
		depth = std::clamp( static_cast< double >( params[ PT_ENLARGE ] ), 0.0, 1.0 ) * generations;
	auto magnifyAt = [ & ]( double g ) { return std::pow( deepest, g / generations ); };
	const double m = magnifyAt( depth );
	state.generation = depth;
	state.magnify    = m;

	//Each enlargement is a new print, a hair off the last: the offsets add up,
	//each in the units of the generation that made it.
	double shiftX = 0.0, shiftY = 0.0;
	for( int j = 1; j <= generations; ++j )
	{
		const double w = std::clamp( depth - ( j - 1 ), 0.0, 1.0 );
		if( w <= 0.0 )
			break;
		shiftX += w * 0.004 * signedHash( cycle * 16 + j, 3u ) / magnifyAt( j );
		shiftY += w * 0.004 * signedHash( cycle * 16 + j, 4u ) / magnifyAt( j );
	}
	//And the film shimmers in the gate a little every frame.
	shiftX += 0.12 * weave * signedHash( film, 5u );
	shiftY += 0.12 * weave * signedHash( film, 6u );

	const double targetX = params[ PT_ENLARGE_X ] * aspect;
	const double targetY = params[ PT_ENLARGE_Y ];
	const double push    = state.push;
	state.scale          = 1.0 / ( push * m );
	state.biasX          = 0.5 * aspect * ( 1.0 - 1.0 / push ) / m + targetX * ( 1.0 - 1.0 / m ) + shiftX;
	state.biasY          = 0.5 * ( 1.0 - 1.0 / push ) / m + targetY * ( 1.0 - 1.0 / m ) + shiftY;

	const double stretchGain = StretchFromParam( params[ PT_STRETCH ] ) - 1.0;
	const double clock       = std::fmod( elapsed, 1000.0 );
	{
		FFGLShader& shader = program( shaders::Pass::Frame );
		ScopedFBOBinding fbo( framed.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, width, height );
		ScopedShaderBinding binding( shader.GetGLID() );
		bindUnit( 0, trail[ current ].TextureID() );
		shader.Set( "Picture", 0 );
		shader.Set( "Size", static_cast< float >( width ), static_cast< float >( height ) );
		shader.Set( "Aspect", static_cast< float >( aspect ) );
		shader.Set( "Scale", static_cast< float >( state.scale ) );
		shader.Set( "Bias", static_cast< float >( state.biasX ), static_cast< float >( state.biasY ) );
		const double magnified = 1.0 / state.scale;
		const double smooth    = std::clamp( ( magnified - 1.0 ) / 0.5, 0.0, 1.0 );
		shader.Set( "Smooth", static_cast< float >( smooth * smooth * ( 3.0 - 2.0 * smooth ) ) );
		shader.Set( "StretchCentre", static_cast< float >( params[ PT_STRETCH_X ] * aspect ), params[ PT_STRETCH_Y ] );
		shader.Set( "StretchGain", static_cast< float >( stretchGain ) );
		shader.Set( "StretchWidth", static_cast< float >( StretchWidthFromParam( params[ PT_STRETCH_WIDTH ] ) ) );
		shader.Set( "Writhe", std::clamp( params[ PT_WRITHE ], 0.0f, 1.0f ) );
		shader.Set( "Clock", static_cast< float >( clock ) );
		shader.Set( "Hooks", hooks );
		quad.Draw();
		unbindTextureUnits( 1 );
	}

	//---------------------------------------------------------------------
	// 3. The aura's mask and its blur, at a quarter of the size.
	//---------------------------------------------------------------------
	const float auraGain = 3.0f * std::clamp( params[ PT_AURA ], 0.0f, 1.0f );
	const int keyMode    = OptionIndex( params[ PT_AURA_KEY ], static_cast< int >( AuraKey::Count ) );
	const float threshold = std::clamp( params[ PT_AURA_THRESHOLD ], 0.0f, 1.0f );
	const float sigma     = static_cast< float >( std::max( 0.5, AuraSizeFromParam( params[ PT_AURA_SIZE ] ) * height / 4.0 ) );
	if( auraGain > 0.0f )
	{
		framed.GenerateMipmaps();
		{
			FFGLShader& shader = program( shaders::Pass::Mask );
			ScopedFBOBinding fbo( mask.GetGLID(), ScopedFBOBinding::RB_REVERT );
			glViewport( 0, 0, glowWidth, glowHeight );
			ScopedShaderBinding binding( shader.GetGLID() );
			bindUnit( 0, framed.TextureID() );
			shader.Set( "Picture", 0 );
			shader.Set( "Lod", 2.0f );
			shader.Set( "KeyMode", keyMode );
			shader.Set( "Threshold", threshold );
			shader.Set( "Hooks", hooks );
			quad.Draw();
			unbindTextureUnits( 1 );
		}
		const struct
		{
			PassBuffer* from;
			PassBuffer* to;
			float dx, dy;
		} axes[] = {
			{ &mask, &blurA, 1.0f / glowWidth, 0.0f },
			{ &blurA, &blurB, 0.0f, 1.0f / glowHeight },
		};
		for( const auto& axis : axes )
		{
			FFGLShader& shader = program( shaders::Pass::Blur );
			ScopedFBOBinding fbo( axis.to->GetGLID(), ScopedFBOBinding::RB_REVERT );
			glViewport( 0, 0, glowWidth, glowHeight );
			ScopedShaderBinding binding( shader.GetGLID() );
			bindUnit( 0, axis.from->TextureID() );
			shader.Set( "Mask", 0 );
			shader.Set( "Direction", axis.dx, axis.dy );
			shader.Set( "Sigma", sigma );
			shader.Set( "Hooks", hooks );
			quad.Draw();
			unbindTextureUnits( 1 );
		}
	}

	//---------------------------------------------------------------------
	// The film: flicker per film frame, the torch's path, the grain layers.
	//---------------------------------------------------------------------
	const double flicker = std::clamp( static_cast< double >( params[ PT_FLICKER ] ), 0.0, 1.0 );
	double exposure      = 1.0 + flicker * 0.16 * signedHash( film, 7u );
	if( hash01( static_cast< uint32_t >( film ), 8u ) < 0.05 * flicker )
		exposure -= 0.3 * flicker;// now and then a frame the lamp nearly lost
	state.exposure = exposure;

	const double sweep  = TorchSweepSecondsFromParam( params[ PT_TORCH_SWEEP ] );
	const double radius = TorchSizeFromParam( params[ PT_TORCH_SIZE ] );
	double torchX       = params[ PT_TORCH_X ] * aspect;
	double torchY       = params[ PT_TORCH_Y ];
	if( sweep > 0.0 )
	{
		torchPhase          = std::fmod( torchPhase + dt / sweep, 1.0 );
		const double angle  = TorchAngleFromParam( params[ PT_TORCH_ANGLE ] ) * kPi / 180.0;
		const double reach  = 0.5 * std::sqrt( aspect * aspect + 1.0 ) + 2.0 * radius;
		const double along  = ( 2.0 * torchPhase - 1.0 ) * reach;
		torchX += std::cos( angle ) * along;
		torchY += std::sin( angle ) * along;
	}
	state.torchX = torchX;
	state.torchY = torchY;

	const double grainCell = GrainSizeFromParam( params[ PT_GRAIN_SIZE ] ) * height / 1080.0;
	float layerScale[ shaders::kMaxLayers ]       = {};
	float layerWeight[ shaders::kMaxLayers ]      = {};
	float layerShift[ shaders::kMaxLayers * 2 ]   = {};
	int layerSeed[ shaders::kMaxLayers ]          = {};
	int layers                                    = 0;
	//The grain each earlier generation left in its print, frozen there and
	//enlarged with the picture ever since...
	for( int j = 0; j < generations && layers < shaders::kMaxLayers - 1; ++j )
	{
		const double w = std::clamp( depth - j, 0.0, 1.0 );
		if( w <= 0.0 )
			break;
		layerScale[ layers ]         = static_cast< float >( grainCell * m / magnifyAt( j ) );
		layerWeight[ layers ]        = static_cast< float >( w );
		layerSeed[ layers ]          = static_cast< int >( pcg( static_cast< uint32_t >( cycle * 16 + j ) ) & 0x7fffffff );
		layerShift[ layers * 2 ]     = static_cast< float >( 64.0 * hash01( static_cast< uint32_t >( j ), 9u ) );
		layerShift[ layers * 2 + 1 ] = static_cast< float >( 64.0 * hash01( static_cast< uint32_t >( j ), 10u ) );
		++layers;
	}
	//...and the live camera's, new every film frame. Last, always.
	layerScale[ layers ]  = static_cast< float >( grainCell );
	layerWeight[ layers ] = 1.0f;
	layerSeed[ layers ]   = static_cast< int >( pcg( static_cast< uint32_t >( film ) ^ 0x5bd1e995u ) & 0x7fffffff );
	++layers;
	state.layerCount = layers;
	std::copy( layerScale, layerScale + shaders::kMaxLayers, state.layerScale );
	std::copy( layerWeight, layerWeight + shaders::kMaxLayers, state.layerWeight );

	//The point the whole map holds still, in output pixels: the grain is
	//anchored there, so a frozen layer grows about it with the picture.
	double originX = targetX, originY = targetY;
	if( state.scale < 0.9999 )
	{
		originX = state.biasX / ( 1.0 - state.scale );
		originY = state.biasY / ( 1.0 - state.scale );
	}

	std::string text;
	{
		std::lock_guard< std::mutex > lock( textMutex );
		text = captionText;
	}
	layout = LayOutCaption( width, height, text, stepCount, exposure );
	float glyphs[ shaders::kMaxGlyphs * 4 ] = {};
	for( size_t i = 0; i < layout.glyphs.size(); ++i )
	{
		glyphs[ i * 4 + 0 ] = static_cast< float >( layout.glyphs[ i ].code );
		glyphs[ i * 4 + 1 ] = layout.glyphs[ i ].left;
		glyphs[ i * 4 + 2 ] = layout.glyphs[ i ].top;
		glyphs[ i * 4 + 3 ] = layout.glyphs[ i ].reveal;
	}

	//---------------------------------------------------------------------
	// 4. The composite, into the host's framebuffer and viewport.
	//---------------------------------------------------------------------
	glBindFramebuffer( GL_FRAMEBUFFER, pgl->HostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	{
		FFGLShader& shader = program( shaders::Pass::Composite );
		ScopedShaderBinding binding( shader.GetGLID() );
		bindUnit( 0, source.Handle );
		bindUnit( 1, framed.TextureID() );
		bindUnit( 2, blurB.TextureID() );
		bindUnit( 3, atlasTexture );
		shader.Set( "InputTexture", 0 );
		shader.Set( "Framed", 1 );
		shader.Set( "Glow", 2 );
		shader.Set( "Atlas", 3 );
		shader.Set( "Size", static_cast< float >( width ), static_cast< float >( height ) );
		shader.Set( "Aspect", static_cast< float >( aspect ) );
		shader.Set( "Clock", static_cast< float >( clock ) );

		const auto tint       = static_cast< Tint >( OptionIndex( params[ PT_TINT ], static_cast< int >( Tint::Count ) ) );
		const Duotone& duo    = kTints[ static_cast< int >( tint ) ];
		shader.Set( "ContrastAmount", std::clamp( params[ PT_CONTRAST ], 0.0f, 1.0f ) );
		shader.Set( "TintDark", duo.dark[ 0 ], duo.dark[ 1 ], duo.dark[ 2 ] );
		shader.Set( "TintLight", duo.light[ 0 ], duo.light[ 1 ], duo.light[ 2 ] );
		shader.Set( "TintAmount", tint == Tint::Natural ? 0.0f : std::clamp( params[ PT_TINT_AMOUNT ], 0.0f, 1.0f ) );

		const float* auraColour = kAuraColours[ OptionIndex( params[ PT_AURA_COLOUR ], static_cast< int >( AuraColour::Count ) ) ];
		shader.Set( "KeyMode", keyMode );
		shader.Set( "Threshold", threshold );
		shader.Set( "Silhouette", std::clamp( params[ PT_SILHOUETTE ], 0.0f, 1.0f ) );
		shader.Set( "AuraGain", auraGain );
		shader.Set( "AuraColour", auraColour[ 0 ], auraColour[ 1 ], auraColour[ 2 ] );
		shader.Set( "Corona", std::clamp( params[ PT_CORONA ], 0.0f, 1.0f ) );
		shader.Set( "GlowTexel", 1.0f / glowWidth, 1.0f / glowHeight );
		shader.Set( "GlowSigma", sigma );

		shader.Set( "TorchAmount", std::clamp( params[ PT_TORCH ], 0.0f, 1.0f ) );
		shader.Set( "TorchCentre", static_cast< float >( torchX ), static_cast< float >( torchY ) );
		shader.Set( "TorchRadius", static_cast< float >( radius ) );

		shader.Set( "Exposure", static_cast< float >( exposure ) );
		shader.Set( "Vignette", 0.85f * std::clamp( params[ PT_VIGNETTE ], 0.0f, 1.0f ) );

		shader.Set( "GrainAmount", 0.35f * std::clamp( params[ PT_GRAIN ], 0.0f, 1.0f ) );
		shader.Set( "LayerCount", layers );
		glUniform1fv( shader.FindUniform( "LayerScale" ), shaders::kMaxLayers, layerScale );
		glUniform1fv( shader.FindUniform( "LayerWeight" ), shaders::kMaxLayers, layerWeight );
		glUniform2fv( shader.FindUniform( "LayerShift" ), shaders::kMaxLayers, layerShift );
		glUniform1iv( shader.FindUniform( "LayerSeed" ), shaders::kMaxLayers, layerSeed );
		shader.Set( "GrainOrigin", static_cast< float >( originX / aspect * width ), static_cast< float >( originY * height ) );

		shader.Set( "GreyAmount", static_cast< float >( state.grey ) );
		shader.Set( "Flash", static_cast< float >( state.flash ) );

		shader.Set( "GlyphCount", static_cast< int >( layout.glyphs.size() ) );
		glUniform4fv( shader.FindUniform( "Glyphs" ), shaders::kMaxGlyphs, glyphs );
		shader.Set( "CaptionBox", layout.box[ 0 ], layout.box[ 1 ], layout.box[ 2 ], layout.box[ 3 ] );
		shader.Set( "CaptionScale", layout.scale );
		//Half the field's change across one output pixel: the letter's edge
		//is one pixel wide at any size.
		shader.Set( "CaptionEdge", std::max( 0.002f, 1.0f / ( 4.0f * font::kSpread * layout.scale ) ) );
		shader.Set( "CaptionGlow", std::clamp( params[ PT_CAPTION_GLOW ], 0.0f, 1.0f ) );
		shader.Set( "CaptionLevel", static_cast< float >( 0.97 * ( 0.5 + 0.5 * exposure ) ) );

		shader.Set( "MixAmount", std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) );
		shader.Set( "Hooks", hooks );
		quad.Draw();
		unbindTextureUnits( 4 );
	}

	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult BluebookPlugin::DeInitGL()
{
	for( FFGLShader& shader : programs )
		shader.FreeGLResources();
	quad.Release();
	for( PassBuffer& b : held )
		b.Destroy();
	for( PassBuffer& b : trail )
		b.Destroy();
	frozen.Destroy();
	framed.Destroy();
	mask.Destroy();
	blurA.Destroy();
	blurB.Destroy();
	if( atlasTexture != 0 )
		glDeleteTextures( 1, &atlasTexture );
	atlasTexture = 0;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult BluebookPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

void BluebookPlugin::SetBeatInfo( float newBpm, float newBarPhase )
{
	CFFGLPlugin::SetBeatInfo( newBpm, newBarPhase );
	if( newBpm > 0.0f && std::isfinite( newBpm ) )
		bpm = newBpm;
	if( std::isfinite( newBarPhase ) )
		barPhase = newBarPhase - std::floor( newBarPhase );
}

char* BluebookPlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_FIRST )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	if( index == PT_CAPTION )
	{
		//The host reads this and does not take ownership; it stays valid until
		//the next SetTextParameter, which is on the same thread.
		std::lock_guard< std::mutex > lock( textMutex );
		return const_cast< char* >( captionText.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult BluebookPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_ABOUT_FIRST )
		return FF_SUCCESS;
	if( index == PT_CAPTION )
	{
		std::lock_guard< std::mutex > lock( textMutex );
		captionText = value ? value : "";
		return FF_SUCCESS;
	}
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult BluebookPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing.
	if( index >= PT_ABOUT_FIRST )
		return stoatworks::about::handleParam( index - PT_ABOUT_FIRST, value ) ? FF_SUCCESS : FF_FAIL;

	if( index == PT_CAPTION )
		return FF_SUCCESS;

	//Freeze acts on the press, once: an event arrives as 1 on press and 0 on
	//release, and a host is free to restate either.
	if( index == PT_FREEZE )
	{
		const bool down = value >= 0.5f;
		if( down && !freezeHeld )
			++freezePresses;
		freezeHeld = down;
	}

	params[ index ] = value;
	return FF_SUCCESS;
}

float BluebookPlugin::GetFloatParameter( unsigned int index )
{
	if( index >= PT_COUNT || index == PT_CAPTION )
		return 0.0f;
	return params[ index ];
}

} // namespace bluebook
