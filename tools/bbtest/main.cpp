/**
    bbtest -- render Bluebook offline, and measure what it does.

    It drives the REAL plugin class, through the same ProcessOpenGL a host
    calls, on a synthetic clock, in a headless CGL context, and reads back the
    host's output (and the plugin's own framed picture and glow) as floats.

        bbtest --out /tmp/bluebook.png     the defaults on the harness's card, 2 s in
        bbtest --in still.png --out o.png  on a picture of your own (8-bit PNG)
        bbtest --list                      every parameter and its default
        bbtest --film N                    N frames, raw RGBA (top row first) on stdout
        bbtest --pipe                      raw RGBA frames in on stdin, out on stdout
        bbtest --shaders DIR               every assembled fragment shader, as the plugin compiles it
        bbtest --offline                   the checks that need no GL context (CI)
        bbtest --all                       every check

    `--set "Name=Value"` sets a control by name (an option by its element's
    name or index); `--at FRAME "Name=Value"` sets it on that frame, which is
    how a press is made (`--at 60 Freeze=1`). `--script FILE` is the fleet's
    cue format: `frame  Parameter Name  value` lines, held before the first key
    and after the last; a standard control ramps between keys, an option, an
    event or an integer steps. `--caption TEXT` sets the caption.

    BBTEST_RENDERER=software asks for Apple's software renderer by id, on a
    Mac with a GPU: what a GPU-less CI runner falls back to.
*/

#include "Bluebook.h"
#include "Controls.h"
#include "Font.h"
#include "Shaders.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace bluebook;

namespace
{
using Floats = std::vector< float >;
using Bytes  = std::vector< unsigned char >;

//---------------------------------------------------------------------------
// Reporting.
//---------------------------------------------------------------------------
int g_failures = 0;
int g_checks   = 0;
bool g_quiet   = false;

std::string fmt( const char* format, ... )
{
	char buffer[ 4096 ];
	va_list args;
	va_start( args, format );
	std::vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	return buffer;
}

bool Check( bool condition, const std::string& message )
{
	++g_checks;
	if( !g_quiet )
		std::printf( "  %s  %s\n", condition ? "ok  " : "FAIL", message.c_str() );
	if( !condition )
		++g_failures;
	return condition;
}

//---------------------------------------------------------------------------
// PNG. zlib ships with the OS.
//---------------------------------------------------------------------------
void putU32( Bytes& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( Bytes& out, const char* type, const Bytes& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

/// `rgba` is premultiplied floats, row 0 at the BOTTOM (GL's order); the file
/// is written top row first, straight alpha (PNG's), opaque unless `alpha`.
bool writePng( const std::string& path, int width, int height, const Floats& rgba, bool alpha = false )
{
	Bytes raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = height - 1; y >= 0; --y )
	{
		raw.push_back( 0 );
		for( int x = 0; x < width; ++x )
		{
			const float* p = &rgba[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			const float a  = std::clamp( p[ 3 ], 0.0f, 1.0f );
			for( int c = 0; c < 4; ++c )
			{
				float v = c == 3 ? ( alpha ? a : 1.0f ) : p[ c ];
				if( alpha && c < 3 )
					v = a > 0.0f ? v / a : 0.0f;
				raw.push_back( static_cast< unsigned char >( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) ) );
			}
		}
	}
	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	Bytes compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );

	Bytes png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	Bytes ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.insert( ihdr.end(), { 8, 6, 0, 0, 0 } );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );
	FILE* file = std::fopen( path.c_str(), "wb" );
	if( !file )
		return false;
	const size_t written = std::fwrite( png.data(), 1, png.size(), file );
	std::fclose( file );
	return written == png.size();
}

uint32_t getU32( const unsigned char* p )
{
	return ( static_cast< uint32_t >( p[ 0 ] ) << 24 ) | ( static_cast< uint32_t >( p[ 1 ] ) << 16 ) | ( static_cast< uint32_t >( p[ 2 ] ) << 8 ) | p[ 3 ];
}

/// An 8-bit, non-interlaced grey, RGB or RGBA PNG, as premultiplied floats,
/// bottom row first. Anything else is refused with a reason.
bool readPng( const std::string& path, int& width, int& height, Floats& out, std::string& error )
{
	std::ifstream file( path, std::ios::binary );
	if( !file )
	{
		error = "cannot open " + path;
		return false;
	}
	const Bytes data( ( std::istreambuf_iterator< char >( file ) ), std::istreambuf_iterator< char >() );
	if( data.size() < 8 || std::memcmp( data.data(), "\x89PNG\r\n\x1a\n", 8 ) != 0 )
	{
		error = path + " is not a PNG";
		return false;
	}
	size_t pos   = 8;
	int depth    = 0, colour = 0, interlace = 0;
	Bytes idat;
	while( pos + 12 <= data.size() )
	{
		const uint32_t length = getU32( &data[ pos ] );
		const std::string kind( reinterpret_cast< const char* >( &data[ pos + 4 ] ), 4 );
		const unsigned char* body = &data[ pos + 8 ];
		if( pos + 12 + length > data.size() )
			break;
		if( kind == "IHDR" )
		{
			width     = static_cast< int >( getU32( body ) );
			height    = static_cast< int >( getU32( body + 4 ) );
			depth     = body[ 8 ];
			colour    = body[ 9 ];
			interlace = body[ 12 ];
		}
		else if( kind == "IDAT" )
			idat.insert( idat.end(), body, body + length );
		pos += 12 + length;
	}
	const int channels = colour == 0 ? 1 : colour == 2 ? 3 : colour == 6 ? 4 : colour == 4 ? 2 : 0;
	if( depth != 8 || channels == 0 || interlace != 0 || width <= 0 || height <= 0 )
	{
		error = path + ": only 8-bit, non-interlaced grey, grey+alpha, RGB or RGBA PNGs";
		return false;
	}
	const size_t stride = static_cast< size_t >( width ) * channels;
	Bytes raw( ( stride + 1 ) * height );
	uLongf rawSize = static_cast< uLongf >( raw.size() );
	if( uncompress( raw.data(), &rawSize, idat.data(), static_cast< uLong >( idat.size() ) ) != Z_OK || rawSize != raw.size() )
	{
		error = path + ": the image data would not inflate";
		return false;
	}
	Bytes rows( stride * height );
	for( int y = 0; y < height; ++y )
	{
		const unsigned char filter = raw[ y * ( stride + 1 ) ];
		const unsigned char* line  = &raw[ y * ( stride + 1 ) + 1 ];
		unsigned char* row         = &rows[ y * stride ];
		const unsigned char* up    = y > 0 ? &rows[ ( y - 1 ) * stride ] : nullptr;
		for( size_t i = 0; i < stride; ++i )
		{
			const int a = i >= static_cast< size_t >( channels ) ? row[ i - channels ] : 0;
			const int b = up ? up[ i ] : 0;
			const int c = ( up && i >= static_cast< size_t >( channels ) ) ? up[ i - channels ] : 0;
			int predicted = 0;
			switch( filter )
			{
			case 0: predicted = 0; break;
			case 1: predicted = a; break;
			case 2: predicted = b; break;
			case 3: predicted = ( a + b ) / 2; break;
			case 4:
			{
				const int p = a + b - c, pa = std::abs( p - a ), pb = std::abs( p - b ), pc = std::abs( p - c );
				predicted   = ( pa <= pb && pa <= pc ) ? a : ( pb <= pc ? b : c );
				break;
			}
			default: error = path + ": bad filter"; return false;
			}
			row[ i ] = static_cast< unsigned char >( line[ i ] + predicted );
		}
	}
	out.assign( static_cast< size_t >( width ) * height * 4, 0.0f );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const unsigned char* p = &rows[ y * stride + x * channels ];
			float rgba[ 4 ];
			if( channels <= 2 )
				rgba[ 0 ] = rgba[ 1 ] = rgba[ 2 ] = p[ 0 ] / 255.0f;
			else
				for( int c = 0; c < 3; ++c )
					rgba[ c ] = p[ c ] / 255.0f;
			rgba[ 3 ]  = channels == 4 ? p[ 3 ] / 255.0f : channels == 2 ? p[ 1 ] / 255.0f : 1.0f;
			float* o   = &out[ ( static_cast< size_t >( height - 1 - y ) * width + x ) * 4 ];
			for( int c = 0; c < 3; ++c )
				o[ c ] = rgba[ c ] * rgba[ 3 ];
			o[ 3 ] = rgba[ 3 ];
		}
	return true;
}

/// A picture resized to w x h by area-weighted box filtering (or bilinear
/// when enlarging): enough to put a photo of any size on the harness's raster.
Floats resample( const Floats& in, int iw, int ih, int w, int h )
{
	Floats out( static_cast< size_t >( w ) * h * 4, 0.0f );
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			const double sx0 = static_cast< double >( x ) * iw / w, sx1 = static_cast< double >( x + 1 ) * iw / w;
			const double sy0 = static_cast< double >( y ) * ih / h, sy1 = static_cast< double >( y + 1 ) * ih / h;
			double acc[ 4 ] = { 0, 0, 0, 0 }, total = 0.0;
			for( int j = static_cast< int >( sy0 ); j < std::min( ih, static_cast< int >( std::ceil( sy1 ) ) ); ++j )
				for( int i = static_cast< int >( sx0 ); i < std::min( iw, static_cast< int >( std::ceil( sx1 ) ) ); ++i )
				{
					const double wx = std::min( sx1, i + 1.0 ) - std::max( sx0, static_cast< double >( i ) );
					const double wy = std::min( sy1, j + 1.0 ) - std::max( sy0, static_cast< double >( j ) );
					const double wgt = std::max( 0.0, wx ) * std::max( 0.0, wy );
					for( int c = 0; c < 4; ++c )
						acc[ c ] += wgt * in[ ( static_cast< size_t >( j ) * iw + i ) * 4 + c ];
					total += wgt;
				}
			for( int c = 0; c < 4; ++c )
				out[ ( static_cast< size_t >( y ) * w + x ) * 4 + c ] = static_cast< float >( total > 0.0 ? acc[ c ] / total : 0.0 );
		}
	return out;
}

//---------------------------------------------------------------------------
// The context.
//---------------------------------------------------------------------------
bool g_software = false;

CGLContextObj createContext()
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute fallback[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute generic[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFARendererID, static_cast< CGLPixelFormatAttribute >( kCGLRendererGenericFloatID ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};

	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	const char* renderer     = std::getenv( "BBTEST_RENDERER" );
	if( renderer != nullptr && std::strcmp( renderer, "software" ) == 0 )
	{
		if( CGLChoosePixelFormat( generic, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
		g_software = true;
		std::fprintf( stderr, "bbtest: BBTEST_RENDERER=software, Apple's software renderer\n" );
	}
	else if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
	{
		if( CGLChoosePixelFormat( fallback, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	}
	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( context );
	return context;
}

const char* kindName( unsigned int type )
{
	switch( type )
	{
	case FF_TYPE_BOOLEAN: return "bool";
	case FF_TYPE_EVENT: return "event";
	case FF_TYPE_OPTION: return "option";
	case FF_TYPE_STANDARD: return "standard";
	case FF_TYPE_TEXT: return "text";
	case FF_TYPE_XPOS: return "x";
	case FF_TYPE_YPOS: return "y";
	case FF_TYPE_INTEGER: return "integer";
	default: return "other";
	}
}

/// A control whose value is a choice, a switch, a press or a count: cues STEP
/// between keys for these, and everything else ramps.
bool stepsBetweenCues( unsigned int type )
{
	return type == FF_TYPE_OPTION || type == FF_TYPE_BOOLEAN || type == FF_TYPE_EVENT || type == FF_TYPE_INTEGER;
}

//---------------------------------------------------------------------------
// The cue sheet.
//---------------------------------------------------------------------------
using Track = std::vector< std::pair< int, float > >;

std::map< std::string, Track > loadScript( std::istream& in, const std::string& path, std::string& error )
{
	std::map< std::string, Track > tracks;
	std::string line;
	int lineNumber = 0;
	while( std::getline( in, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream words( line );
		int frame = 0;
		if( !( words >> frame ) )
			continue;
		std::vector< std::string > parts;
		std::string word;
		while( words >> word )
			parts.push_back( word );
		if( parts.size() < 2 )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": expected `frame Parameter Name value`";
			return {};
		}
		char* end         = nullptr;
		const float value = std::strtof( parts.back().c_str(), &end );
		if( end == parts.back().c_str() || *end != '\0' )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": '" + parts.back() + "' is not a number";
			return {};
		}
		parts.pop_back();
		std::string name = parts.front();
		for( size_t i = 1; i < parts.size(); ++i )
			name += " " + parts[ i ];
		tracks[ name ].emplace_back( frame, value );
	}
	for( auto& entry : tracks )
		std::stable_sort( entry.second.begin(), entry.second.end(),
		                  []( const std::pair< int, float >& a, const std::pair< int, float >& b ) { return a.first < b.first; } );
	return tracks;
}

float valueAt( const Track& track, int frame, bool ramp )
{
	if( track.empty() )
		return 0.0f;
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 0; i + 1 < track.size(); ++i )
	{
		const auto& a = track[ i ];
		const auto& b = track[ i + 1 ];
		if( frame >= a.first && frame < b.first )
		{
			if( !ramp )
				return a.second;
			const float t = static_cast< float >( frame - a.first ) / static_cast< float >( b.first - a.first );
			return a.second + ( b.second - a.second ) * t;
		}
	}
	return track.back().second;
}

//---------------------------------------------------------------------------
// Pictures. Every one is premultiplied, rows BOTTOM first (GL's order).
//---------------------------------------------------------------------------
uint32_t pcg( uint32_t v )
{
	const uint32_t state = v * 747796405u + 2891336453u;
	const uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}
double hash01( uint32_t a, uint32_t b = 0 )
{
	return pcg( a * 2654435761u ^ pcg( b + 0x9e3779b9u ) ) * ( 1.0 / 4294967296.0 );
}

double smoothstep( double a, double b, double x )
{
	const double t = std::clamp( ( x - a ) / ( b - a ), 0.0, 1.0 );
	return t * t * ( 3.0 - 2.0 * t );
}

/// The harness's card at time t: a night sky over a ridge, a moving light (a
/// craft, or a lamp) and a figure with a raised hand, colour bars along the
/// bottom and a fine grid in the corner for the enlargement to find. Every
/// stage has something to act on: the light moves, so a print shows; the
/// figure and the light are bright on dark, so the aura has a subject.
Floats buildCard( int width, int height, double t )
{
	Floats out( static_cast< size_t >( width ) * height * 4 );
	const double aspect = static_cast< double >( width ) / height;
	const double lx     = aspect * ( 0.25 + 0.5 * ( 0.5 + 0.5 * std::sin( t * 0.9 ) ) );
	const double ly     = 0.72 + 0.06 * std::sin( t * 1.7 );
	const double bars[ 7 ][ 3 ] = { { 0.75, 0.75, 0.75 }, { 0.75, 0.75, 0 }, { 0, 0.75, 0.75 }, { 0, 0.75, 0 },
		                            { 0.75, 0, 0.75 },    { 0.75, 0, 0 },    { 0, 0, 0.75 } };
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 ) / height;// frame heights
			const double v = ( y + 0.5 ) / height;
			// sky: deep blue at the top to a dusky grey-green at the ridge
			double r = 0.05 + 0.18 * ( 1.0 - v ), g = 0.08 + 0.20 * ( 1.0 - v ), b = 0.16 + 0.16 * ( 1.0 - v );
			// stars
			if( hash01( static_cast< uint32_t >( x * 7919 + y * 104729 ), 3 ) > 0.9985 && v > 0.45 )
				r = g = b = 0.8;
			// the ridge
			const double ridge = 0.32 + 0.05 * std::sin( u * 5.0 ) + 0.03 * std::sin( u * 13.0 + 1.0 );
			if( v < ridge )
				r = 0.03, g = 0.04, b = 0.03;
			// the light: a hot core and a halo
			const double dl = std::hypot( u - lx, ( v - ly ) * 2.2 );
			const double core = smoothstep( 0.045, 0.035, dl );
			const double halo = 0.5 * std::exp( -dl * dl / 0.01 );
			r += core * 0.95 + halo * 0.55;
			g += core * 0.95 + halo * 0.6;
			b += core * 0.9 + halo * 0.5;
			// the figure: head, body, a raised arm and an open hand
			const double fx = aspect * 0.5, fy = 0.36;
			const double head  = std::hypot( u - fx, v - ( fy + 0.25 ) ) < 0.045 ? 1.0 : 0.0;
			const double body  = ( std::fabs( u - fx ) < 0.06 - 0.25 * std::max( 0.0, v - fy - 0.15 ) && v > fy - 0.12 && v < fy + 0.2 ) ? 1.0 : 0.0;
			const double armT  = std::clamp( ( ( u - fx ) * 0.6 + ( v - fy - 0.15 ) * 0.8 ) / 0.25, 0.0, 1.0 );
			const double armD  = std::hypot( u - ( fx + 0.6 * 0.25 * armT ), v - ( fy + 0.15 + 0.8 * 0.25 * armT ) );
			const double arm   = armD < 0.022 ? 1.0 : 0.0;
			const double hand  = std::hypot( u - ( fx + 0.17 ), ( v - ( fy + 0.37 ) ) * 0.8 ) < 0.04 ? 1.0 : 0.0;
			double fingers     = 0.0;
			for( int k = 0; k < 4; ++k )
			{
				const double ax = fx + 0.15 + 0.018 * k, ay = fy + 0.4;
				if( std::fabs( u - ax ) < 0.007 && v > ay && v < ay + 0.06 - 0.01 * std::fabs( k - 1.5 ) )
					fingers = 1.0;
			}
			const double figure = std::max( std::max( head, body ), std::max( std::max( arm, hand ), fingers ) );
			if( figure > 0.0 )
				r = 0.86, g = 0.82, b = 0.74;
			// colour bars along the bottom
			if( v < 0.1 )
			{
				const int bar = std::clamp( static_cast< int >( u / aspect * 7.0 ), 0, 6 );
				r = bars[ bar ][ 0 ], g = bars[ bar ][ 1 ], b = bars[ bar ][ 2 ];
			}
			// a fine grid, top left
			if( u > 0.05 && u < 0.35 && v > 0.62 && v < 0.92 )
			{
				const int gx = static_cast< int >( std::floor( ( u - 0.05 ) / 0.03 ) );
				const int gy = static_cast< int >( std::floor( ( v - 0.62 ) / 0.03 ) );
				const double c = ( ( gx + gy ) & 1 ) ? 0.85 : 0.15;
				r = g = b = c;
			}
			float* o = &out[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			o[ 0 ]   = static_cast< float >( std::clamp( r, 0.0, 1.0 ) );
			o[ 1 ]   = static_cast< float >( std::clamp( g, 0.0, 1.0 ) );
			o[ 2 ]   = static_cast< float >( std::clamp( b, 0.0, 1.0 ) );
			o[ 3 ]   = 1.0f;
		}
	return out;
}

Floats flatCard( int width, int height, float r, float g, float b, float a = 1.0f )
{
	Floats out( static_cast< size_t >( width ) * height * 4 );
	for( size_t i = 0; i < out.size(); i += 4 )
	{
		out[ i ]     = r * a;
		out[ i + 1 ] = g * a;
		out[ i + 2 ] = b * a;
		out[ i + 3 ] = a;
	}
	return out;
}

GLuint makeTexture( int width, int height, const float* pixels, GLint format = GL_RGBA32F )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_FLOAT, pixels );
	//Linear, as a host's clip texture is.
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

//---------------------------------------------------------------------------
// A rig: the real plugin, a float output framebuffer, a synthetic clock and,
// when asked for, a synthetic beat.
//---------------------------------------------------------------------------
struct Rig
{
	BluebookPlugin plugin;
	int width = 0, height = 0;
	GLuint sourceTexture = 0, outputTexture = 0, outputFBO = 0;
	int frame  = 0;
	double fps = 60.0;
	double bpm = 0.0;///< 0: no beat sent
	/// Runs before each frame with its number: a moving card, a cue sheet.
	std::function< void( int ) > beforeFrame;

	ProcessOpenGLStruct process    = {};
	FFGLTextureStruct inputStruct  = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };

	~Rig()
	{
		plugin.DeInitGL();
		release();
	}

	void release()
	{
		if( outputFBO )
			glDeleteFramebuffers( 1, &outputFBO );
		if( outputTexture )
			glDeleteTextures( 1, &outputTexture );
		if( sourceTexture )
			glDeleteTextures( 1, &sourceTexture );
		outputFBO = outputTexture = sourceTexture = 0;
	}

	bool attach( int w, int h, const Floats* picture )
	{
		width         = w;
		height        = h;
		outputTexture = makeTexture( width, height, nullptr );
		glGenFramebuffers( 1, &outputFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0 );
		if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
			return false;
		process.HostFBO   = outputFBO;
		const Floats card = picture ? *picture : buildCard( width, height, 0.0 );
		sourceTexture     = makeTexture( width, height, card.data() );
		inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( width );
		inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( height );
		inputStruct.Handle                              = sourceTexture;
		inputs[ 0 ]                                     = &inputStruct;
		process.numInputTextures                        = 1;
		process.inputTextures                           = inputs;
		return true;
	}

	bool Init( int w, int h, const Floats* picture = nullptr )
	{
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( w );
		viewport.height             = static_cast< FFUInt32 >( h );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see ~/Library/Logs/bluebook for which shader\n" );
			return false;
		}
		plugin.SetClockScaleForTest( 1.0 );
		return attach( w, h, picture );
	}

	/// The host's raster changes under a running instance: no InitGL.
	bool Resize( int w, int h, const Floats* picture = nullptr )
	{
		release();
		return attach( w, h, picture );
	}

	void Upload( const Floats& picture )
	{
		glBindTexture( GL_TEXTURE_2D, sourceTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, picture.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	void Set( unsigned int id, float value )
	{
		plugin.SetFloatParameter( id, value );
	}

	double Seconds( int f ) const
	{
		return static_cast< double >( f ) / fps;
	}

	bool Render( int frames = 1 )
	{
		for( int i = 0; i < frames; ++i )
		{
			if( beforeFrame )
				beforeFrame( frame );
			plugin.SetTime( Seconds( frame ) );
			if( bpm > 0.0 )
				plugin.SetBeatInfo( static_cast< float >( bpm ), static_cast< float >( std::fmod( Seconds( frame ) * bpm / 240.0, 1.0 ) ) );
			++frame;
			glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
			glViewport( 0, 0, width, height );
			glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			if( plugin.ProcessOpenGL( &process ) != FF_SUCCESS )
			{
				std::fprintf( stderr, "ProcessOpenGL failed\n" );
				return false;
			}
		}
		return true;
	}

	/// Press Freeze on the next frame and render it.
	bool Press( unsigned int id )
	{
		Set( id, 1.0f );
		const bool ok = Render( 1 );
		Set( id, 0.0f );
		return ok;
	}

	Floats Output() const
	{
		Floats pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return pixels;
	}
};

//---------------------------------------------------------------------------
// Parameters by display name.
//---------------------------------------------------------------------------
struct NamedParameter
{
	std::string name;
	unsigned int index;
	float value;
	unsigned int type;
};

std::vector< NamedParameter > listParameters( BluebookPlugin& plugin )
{
	std::vector< NamedParameter > list;
	for( unsigned int i = 0; i < plugin.GetNumParams(); ++i )
	{
		const char* const name = plugin.GetParamName( i );
		list.push_back( NamedParameter { name ? name : "?", i, plugin.GetFloatParameter( i ), plugin.GetParamType( i ) } );
	}
	return list;
}

/// `Name=Value`. The value is a number, or for an option the name of one of
/// its elements; anything else is refused, never read as 0 (polyhedral's
/// trap: strtof( "Fixed" ) is 0, silently). Caption takes any text.
bool applySetting( BluebookPlugin& plugin, const std::string& assignment, std::string& error )
{
	const size_t equals = assignment.find( '=' );
	if( equals == std::string::npos )
	{
		error = "expected Name=Value";
		return false;
	}
	const std::string name  = assignment.substr( 0, equals );
	const std::string value = assignment.substr( equals + 1 );
	for( const NamedParameter& parameter : listParameters( plugin ) )
		if( parameter.name == name )
		{
			if( parameter.type == FF_TYPE_TEXT )
			{
				if( parameter.index != PT_CAPTION )
				{
					error = name + " cannot be set from here";
					return false;
				}
				plugin.SetTextParameter( parameter.index, value.c_str() );
				return true;
			}
			if( parameter.type == FF_TYPE_OPTION )
				for( unsigned int e = 0; e < plugin.GetNumParamElements( parameter.index ); ++e )
					if( value == plugin.GetParamElementName( parameter.index, e ) )
					{
						plugin.SetFloatParameter( parameter.index, static_cast< float >( e ) );
						return true;
					}
			char* end       = nullptr;
			const float got = std::strtof( value.c_str(), &end );
			if( value.empty() || end == value.c_str() || *end != '\0' )
			{
				error = "'" + value + "' is not a number" + ( parameter.type == FF_TYPE_OPTION ? " or an option of " + name : "" );
				return false;
			}
			plugin.SetFloatParameter( parameter.index, got );
			return true;
		}
	error = "no parameter called '" + name + "'";
	return false;
}

struct Cue
{
	Track track;
	bool ramp;
};

bool bindScript( BluebookPlugin& plugin, const std::string& path, std::map< unsigned int, Cue >& out, std::string& error )
{
	std::ifstream file( path );
	if( !file )
	{
		error = "cannot open " + path;
		return false;
	}
	const std::map< std::string, Track > tracks = loadScript( file, path, error );
	if( !error.empty() )
		return false;
	const std::vector< NamedParameter > known = listParameters( plugin );
	for( const auto& entry : tracks )
	{
		bool found = false;
		for( const NamedParameter& parameter : known )
			if( parameter.name == entry.first && parameter.type != FF_TYPE_TEXT )
			{
				out[ parameter.index ] = Cue { entry.second, !stepsBetweenCues( parameter.type ) };
				found                  = true;
			}
		if( !found )
		{
			error = "script names '" + entry.first + "', which is not a parameter it can cue (try --list)";
			return false;
		}
	}
	return true;
}

//---------------------------------------------------------------------------
// Rendering on request.
//---------------------------------------------------------------------------
struct Request
{
	int width = 1280, height = 720;
	int frames = 120;
	double fps = 60.0;
	double bpm = 120.0;
	std::string in;      ///< a still to use instead of the card
	bool still = false;  ///< hold the card at t = 0
	std::vector< std::string > settings;
	std::vector< std::pair< int, std::string > > timed;
	std::string script;
	int hooks = 0;
	bool alpha = false;
};

/// Builds the rig a request describes, with its picture and its cues.
bool prepare( Rig& rig, const Request& request, std::map< unsigned int, Cue >& cues, Floats& still, bool& animated )
{
	rig.fps = request.fps;
	rig.bpm = request.bpm;
	animated = request.in.empty() && !request.still;
	if( !request.in.empty() )
	{
		int iw = 0, ih = 0;
		Floats picture;
		std::string error;
		if( !readPng( request.in, iw, ih, picture, error ) )
		{
			std::fprintf( stderr, "bbtest: %s\n", error.c_str() );
			return false;
		}
		still = ( iw == request.width && ih == request.height ) ? picture : resample( picture, iw, ih, request.width, request.height );
	}
	else
		still = buildCard( request.width, request.height, 0.0 );
	if( !rig.Init( request.width, request.height, &still ) )
		return false;
	rig.plugin.SetHooksForTest( request.hooks );
	for( const std::string& setting : request.settings )
	{
		std::string error;
		if( !applySetting( rig.plugin, setting, error ) )
		{
			std::fprintf( stderr, "bbtest: --set %s: %s\n", setting.c_str(), error.c_str() );
			return false;
		}
	}
	for( const auto& entry : request.timed )
	{
		const size_t equals = entry.second.find( '=' );
		bool known          = false;
		for( const NamedParameter& p : listParameters( rig.plugin ) )
			if( equals != std::string::npos && p.name == entry.second.substr( 0, equals ) )
				known = true;
		if( !known )
		{
			std::fprintf( stderr, "bbtest: --at %d %s: no such parameter\n", entry.first, entry.second.c_str() );
			return false;
		}
	}
	if( !request.script.empty() )
	{
		std::string error;
		if( !bindScript( rig.plugin, request.script, cues, error ) )
		{
			std::fprintf( stderr, "bbtest: %s\n", error.c_str() );
			return false;
		}
	}
	const int w = request.width, h = request.height;
	const auto timed = request.timed;
	rig.beforeFrame = [ &rig, &cues, timed, animated, w, h ]( int frame ) {
		if( animated )
			rig.Upload( buildCard( w, h, rig.Seconds( frame ) ) );
		for( const auto& entry : cues )
			rig.Set( entry.first, valueAt( entry.second.track, frame, entry.second.ramp ) );
		for( const auto& entry : timed )
		{
			std::string error;
			if( entry.first == frame )
				applySetting( rig.plugin, entry.second, error );
			//A press is let go on the frame after it.
			else if( entry.first + 1 == frame && entry.second.find( "=1" ) != std::string::npos )
			{
				const std::string name = entry.second.substr( 0, entry.second.find( '=' ) );
				for( const NamedParameter& p : listParameters( rig.plugin ) )
					if( p.name == name && p.type == FF_TYPE_EVENT )
						rig.Set( p.index, 0.0f );
			}
		}
	};
	return true;
}

int runOut( const Request& request, const std::string& path )
{
	Rig rig;
	std::map< unsigned int, Cue > cues;
	Floats still;
	bool animated = false;
	if( !prepare( rig, request, cues, still, animated ) )
		return 2;
	if( !rig.Render( request.frames ) )
		return 1;
	if( !writePng( path, rig.width, rig.height, rig.Output(), request.alpha ) )
	{
		std::fprintf( stderr, "bbtest: could not write %s\n", path.c_str() );
		return 1;
	}
	return 0;
}

/// One frame of output as raw 8-bit RGBA, top row first, to stdout. False
/// when the reader has gone: SIGPIPE is ignored in main(), so that is EPIPE
/// and not a silent 141.
bool writeFrame( const Rig& rig, int index )
{
	const Floats out = rig.Output();
	Bytes bytes( static_cast< size_t >( rig.width ) * rig.height * 4 );
	for( int y = 0; y < rig.height; ++y )
		for( int x = 0; x < rig.width * 4; ++x )
			bytes[ static_cast< size_t >( rig.height - 1 - y ) * rig.width * 4 + x ]
			    = static_cast< unsigned char >( std::lround( std::clamp( out[ static_cast< size_t >( y ) * rig.width * 4 + x ], 0.0f, 1.0f ) * 255.0f ) );
	size_t written = 0;
	while( written < bytes.size() )
	{
		const ssize_t put = write( STDOUT_FILENO, bytes.data() + written, bytes.size() - written );
		if( put <= 0 )
		{
			std::fprintf( stderr, "bbtest: stdout closed at frame %d\n", index );
			return false;
		}
		written += static_cast< size_t >( put );
	}
	return true;
}

int runFilm( const Request& request )
{
	Rig rig;
	std::map< unsigned int, Cue > cues;
	Floats still;
	bool animated = false;
	if( !prepare( rig, request, cues, still, animated ) )
		return 2;
	for( int f = 0; f < request.frames; ++f )
		if( !rig.Render( 1 ) || !writeFrame( rig, f ) )
			return 1;
	return 0;
}

/// The fleet's pipe: raw RGBA frames of --size, top row first, in on stdin
/// and out on stdout, on the synthetic clock, cues and --at as for --film.
int runPipe( Request request )
{
	request.still = true;// the picture is stdin's, not the card
	Rig rig;
	std::map< unsigned int, Cue > cues;
	Floats still;
	bool animated = false;
	if( !prepare( rig, request, cues, still, animated ) )
		return 2;
	Bytes in( static_cast< size_t >( rig.width ) * rig.height * 4 );
	Floats picture( in.size() );
	for( int index = 0;; ++index )
	{
		size_t filled = 0;
		while( filled < in.size() )
		{
			const ssize_t got = read( STDIN_FILENO, in.data() + filled, in.size() - filled );
			if( got <= 0 )
				break;
			filled += static_cast< size_t >( got );
		}
		//A partial frame is the end of the stream, never a frame.
		if( filled < in.size() )
		{
			if( filled > 0 )
				std::fprintf( stderr, "bbtest: a partial frame at the end (%zu of %zu bytes), dropped\n", filled, in.size() );
			return 0;
		}
		//Straight 8-bit in, premultiplied floats bottom row first for GL.
		for( int y = 0; y < rig.height; ++y )
			for( int x = 0; x < rig.width; ++x )
			{
				const unsigned char* p = &in[ ( static_cast< size_t >( y ) * rig.width + x ) * 4 ];
				float* o               = &picture[ ( static_cast< size_t >( rig.height - 1 - y ) * rig.width + x ) * 4 ];
				const float a          = p[ 3 ] / 255.0f;
				for( int c = 0; c < 3; ++c )
					o[ c ] = p[ c ] / 255.0f * a;
				o[ 3 ] = a;
			}
		rig.Upload( picture );
		if( !rig.Render( 1 ) || !writeFrame( rig, index ) )
			return 1;
	}
}

int runList()
{
	Rig rig;
	if( !rig.Init( 64, 36 ) )
		return 1;
	for( const NamedParameter& p : listParameters( rig.plugin ) )
	{
		std::printf( "%2u  %-16s  %-8s  ", p.index, p.name.c_str(), kindName( p.type ) );
		if( p.type == FF_TYPE_TEXT )
			std::printf( "\"%s\"", rig.plugin.GetTextParameter( p.index ) );
		else
			std::printf( "%g", p.value );
		if( p.type == FF_TYPE_OPTION )
		{
			std::printf( "  [" );
			for( unsigned int e = 0; e < rig.plugin.GetNumParamElements( p.index ); ++e )
				std::printf( "%s%s", e ? ", " : "", rig.plugin.GetParamElementName( p.index, e ) );
			std::printf( "]" );
		}
		std::printf( "\n" );
	}
	return 0;
}

int runShaders( const std::string& directory )
{
	const std::string vertex = std::string( shaders::kVersion ) + shaders::kQuadVertex;
	auto write = [ & ]( const std::string& name, const std::string& text ) {
		std::ofstream file( directory + "/" + name );
		file << text;
		return static_cast< bool >( file );
	};
	if( !write( "quad.vert", vertex ) )
	{
		std::fprintf( stderr, "bbtest: cannot write into %s\n", directory.c_str() );
		return 1;
	}
	for( int p = 0; p < static_cast< int >( shaders::Pass::Count ); ++p )
	{
		const auto pass = static_cast< shaders::Pass >( p );
		if( !write( std::string( shaders::NameOf( pass ) ) + ".frag", shaders::Assemble( pass ) ) )
			return 1;
	}
	return 0;
}

} // namespace

#include "checks.inc"

int main( int argc, char** argv )
{
	Request request;
	std::string out;
	std::string shaderDirectory;
	std::string caption;
	bool haveCaption = false;
	bool list = false, film = false, pipe = false;
	std::vector< std::string > flags;

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;
		if( argument == "--help" || argument == "-h" )
		{
			std::printf( "bbtest: see the comment at the top of tools/bbtest/main.cpp, and CLAUDE.md\n" );
			return 0;
		}
		else if( argument == "--out" && hasNext )
			out = argv[ ++i ];
		else if( argument == "--in" && hasNext )
			request.in = argv[ ++i ];
		else if( argument == "--set" && hasNext )
			request.settings.push_back( argv[ ++i ] );
		else if( argument == "--caption" && hasNext )
		{
			caption     = argv[ ++i ];
			haveCaption = true;
		}
		else if( argument == "--at" && i + 2 < argc )
		{
			char* end       = nullptr;
			const long when = std::strtol( argv[ i + 1 ], &end, 10 );
			if( end == argv[ i + 1 ] || *end != '\0' )
			{
				std::fprintf( stderr, "bbtest: --at wants a frame number, not '%s'\n", argv[ i + 1 ] );
				return 2;
			}
			request.timed.emplace_back( static_cast< int >( when ), argv[ i + 2 ] );
			i += 2;
		}
		else if( argument == "--frames" && hasNext )
			request.frames = std::max( 1, std::atoi( argv[ ++i ] ) );
		else if( argument == "--fps" && hasNext )
			request.fps = std::atof( argv[ ++i ] );
		else if( argument == "--bpm" && hasNext )
			request.bpm = std::atof( argv[ ++i ] );
		else if( argument == "--script" && hasNext )
			request.script = argv[ ++i ];
		else if( argument == "--hooks" && hasNext )
			request.hooks = std::atoi( argv[ ++i ] );
		else if( argument == "--still" )
			request.still = true;
		else if( argument == "--alpha" )
			request.alpha = true;
		else if( argument == "--list" )
			list = true;
		else if( argument == "--pipe" )
			pipe = true;
		else if( argument == "--film" && hasNext )
		{
			film           = true;
			request.frames = std::max( 1, std::atoi( argv[ ++i ] ) );
		}
		else if( argument == "--shaders" && hasNext )
			shaderDirectory = argv[ ++i ];
		else if( argument == "--size" && hasNext )
		{
			const std::string size = argv[ ++i ];
			if( std::sscanf( size.c_str(), "%dx%d", &request.width, &request.height ) != 2 || request.width < 8 || request.height < 8 )
			{
				std::fprintf( stderr, "bbtest: --size wants WxH, not '%s'\n", size.c_str() );
				return 2;
			}
		}
		else if( argument.rfind( "--", 0 ) == 0 )
			flags.push_back( argument );
		else
		{
			std::fprintf( stderr, "bbtest: what is '%s'?\n", argument.c_str() );
			return 2;
		}
	}
	if( haveCaption )
		request.settings.push_back( "Caption=" + caption );

	if( !shaderDirectory.empty() )
		return runShaders( shaderDirectory );

	//A reader that hangs up must end --pipe/--film with exit 1 and a
	//message, not SIGPIPE's silent 141: ignored here, the write fails.
	std::signal( SIGPIPE, SIG_IGN );

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "bbtest: no OpenGL 4.1 core context\n" );
		return 1;
	}

	int result = 0;
	if( list )
		result = runList();
	else if( pipe )
		result = runPipe( request );
	else if( film )
		result = runFilm( request );
	else if( !out.empty() )
		result = runOut( request, out );
	else if( !flags.empty() )
		result = runChecks( flags, request );
	else
		result = runOut( request, "/tmp/bluebook.png" );

	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return result;
}
