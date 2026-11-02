// GENERATED from source/Shaders.cpp (and Constants()'s numbers, from
// source/Shaders.h and source/Font.h) by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The version line is the plugin's too; the kit's port() swaps it
// for GLSL ES 3.00's at compile time.
export const VERSION = '#version 410 core\n';

// Constants(), source/Shaders.cpp: the numbers from source/Shaders.h and source/Font.h
export const CONSTANTS = `const int kMaxGlyphs = 64;
const int kMaxLayers = 9;
const int kAtlasWidth = 704;
const int kAtlasHeight = 336;
const int kCellWidth = 44;
const int kCellHeight = 56;
const int kColumns = 16;
const int kFirst = 32;
`;

// kQuadVertex, source/Shaders.cpp
export const QUAD_VERTEX = `
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
`;

// kCommon, source/Shaders.cpp
export const COMMON = `
in vec2 uv;
out vec4 fragColour;

uniform int Hooks;

const int kHookSquash        = 1;
const int kHookFlatGrain     = 2;
const int kHookTorchOffset   = 4;
const int kHookGlowInside    = 8;
const int kHookCaptionBottom = 16;
const int kHookNoGrey        = 32;
const int kHookDissolveSnap  = 64;

bool hooked( int bit )
{
	return ( Hooks & bit ) != 0;
}

//PCG's output mix: exact in 32 bits, the same on every GPU and on the CPU.
uint pcg( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}
float hash01( uint a, uint b )
{
	return float( pcg( a * 2654435761u ^ pcg( b + 0x9e3779b9u ) ) ) * ( 1.0 / 4294967296.0 );
}
//uint( int ) keeps the bit pattern (GLSL 4.10, 5.4.1), so a negative cell
//hashes as well as a positive one.
float hashCell( ivec2 cell, uint seed )
{
	return hash01( uint( cell.x ) * 73856093u ^ uint( cell.y ) * 19349663u, seed );
}

//Value noise: hashes on the integer lattice, joined by smoothstep. 0..1.
float valueNoise( vec2 p, uint seed )
{
	vec2 base  = floor( p );
	vec2 f     = p - base;
	vec2 u     = f * f * ( 3.0 - 2.0 * f );
	ivec2 cell = ivec2( base );
	float a    = hashCell( cell, seed );
	float b    = hashCell( cell + ivec2( 1, 0 ), seed );
	float c    = hashCell( cell + ivec2( 0, 1 ), seed );
	float d    = hashCell( cell + ivec2( 1, 1 ), seed );
	return mix( mix( a, b, u.x ), mix( c, d, u.x ), u.y );
}

float luma( vec3 c )
{
	return dot( c, vec3( 0.2126, 0.7152, 0.0722 ) );
}

//The texel a fragment covers, in a picture the raster's size: a 1:1 read is
//a texelFetch, never a filtered one. Apple's software renderer interpolates
//uv a hair off the texel centres, and a filtered read there blurs a sharp
//edge by a few thousandths -- the clip no longer comes back as itself.
//uv * size is a whole number and a half, give or take that hair, so the
//truncation is the right texel either way.
ivec2 texelOf( vec2 size )
{
	return ivec2( uv * size );
}

//Every other read is of level 0, by name. A picture read 1:1 through texture()
//on a mipmapped buffer is at level 0 only where the rasteriser's LOD comes
//out at exactly 0: Apple's software renderer's comes out a hair above, and
//blended in a level-1 nobody had regenerated this frame -- last frame's,
//or another instance's freed memory, NaNs included. Only the aura's mask
//reads a reduced copy, and it asks for its level.
vec4 level0( sampler2D picture, vec2 at )
{
	return textureLod( picture, at, 0.0 );
}

//sRGB, both ways: the torch adds LIGHT, so it multiplies in linear light.
vec3 toLinear( vec3 code )
{
	vec3 c = max( code, vec3( 0.0 ) );
	return mix( c / 12.92, pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) ), step( vec3( 0.04045 ), c ) );
}
vec3 toCode( vec3 light )
{
	vec3 l = max( light, vec3( 0.0 ) );
	return mix( l * 12.92, 1.055 * pow( l, vec3( 1.0 / 2.4 ) ) - 0.055, step( vec3( 0.0031308 ), l ) );
}

//Straight colour from premultiplied.
vec3 straightOf( vec4 p )
{
	return p.a > 1.0 / 4096.0 ? p.rgb / p.a : vec3( 0.0 );
}

//The Aura's subject, 0..1, of a STRAIGHT colour and its alpha. KeyMode:
//0 bright, 1 dark, 2 alpha. The soft edge is a tenth of the code range:
//wide enough that a subject's edge does not crawl from frame to frame.
uniform int KeyMode;
uniform float Threshold;
float subjectOf( vec3 c, float a )
{
	const float kSoft = 0.05;
	if( KeyMode == 2 )
		return smoothstep( Threshold - kSoft, Threshold + kSoft, a );
	float k = smoothstep( Threshold - kSoft, Threshold + kSoft, luma( c ) );
	return KeyMode == 1 ? 1.0 - k : k;
}
`;

// kCaptureFragment, source/Shaders.cpp
export const CAPTURE_FRAGMENT = `
uniform sampler2D InputTexture;
uniform vec2 MaxUV;
uniform vec2 Texel;   //one source texel, in 0..1
uniform float Overscan;//>= 1: how far in the print is enlarged so its edge never shows
uniform vec2 Offset;  //where this print sits, 0..1

void main()
{
	vec2 q = 0.5 + ( uv - 0.5 ) / Overscan + Offset;
	q      = clamp( q, 0.5 * Texel, 1.0 - 0.5 * Texel );
	fragColour = level0( InputTexture, q * MaxUV );
}
`;

// kPrintFragment, source/Shaders.cpp
export const PRINT_FRAGMENT = `
uniform sampler2D InputTexture;
uniform sampler2D Held;       //the newer print
uniform sampler2D HeldBefore; //the one before it
uniform sampler2D TrailBefore;//last frame's result
uniform sampler2D Frozen;     //what was on screen when Freeze was pressed
uniform float Fade;     //0 shows HeldBefore, 1 Held
uniform float Stutter;  //0 the live clip, 1 the prints
uniform float FreezeMix;//1 while a freeze holds, else 0
uniform float Decay;    //how much of last frame stays: 0.5 ^ ( dt / half-life )

void main()
{
	float fade = Fade;
	if( hooked( kHookDissolveSnap ) )
		fade = step( 0.5, Fade );
	//Every picture here is the raster's size, read 1:1.
	ivec2 px     = texelOf( vec2( textureSize( Held, 0 ) ) );
	vec4 live    = texelFetch( InputTexture, px, 0 );
	vec4 stepped = mix( texelFetch( HeldBefore, px, 0 ), texelFetch( Held, px, 0 ), fade );
	vec4 printed = mix( live, stepped, Stutter );
	//A freeze is on or off, so it is chosen, not mixed: mix( a, b, 1.0 ) is
	//not promised to be b, and on Apple's software renderer it is not.
	if( FreezeMix > 0.5 )
		printed = texelFetch( Frozen, px, 0 );
	fragColour   = mix( printed, texelFetch( TrailBefore, px, 0 ), Decay );
}
`;

// kFrameFragment, source/Shaders.cpp
export const FRAME_FRAGMENT = `
uniform sampler2D Picture;
uniform vec2 Size;         //pixels
uniform float Aspect;      //width / height
uniform float Scale;       //output to source, frame heights per frame height
uniform vec2 Bias;         //   ... and the offset, frame heights
uniform float Smooth;      //0 bilinear, 1 cubic B-spline
uniform vec2 StretchCentre;//frame heights
uniform float StretchGain; //the pull's peak, less one
uniform float StretchWidth;//frame heights
uniform float Writhe;
uniform float Clock;       //seconds, reduced on the CPU

//A cubic B-spline from four bilinear reads: each read lands between two
//texels at the point that weights them as the spline does.
vec4 bspline( vec2 q )
{
	vec2 st = q * Size - 0.5;
	vec2 i  = floor( st );
	vec2 f  = st - i;
	vec2 f2 = f * f;
	vec2 f3 = f2 * f;
	vec2 w0 = ( 1.0 - 3.0 * f + 3.0 * f2 - f3 ) / 6.0;
	vec2 w1 = ( 4.0 - 6.0 * f2 + 3.0 * f3 ) / 6.0;
	vec2 w2 = ( 1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3 ) / 6.0;
	vec2 w3 = f3 / 6.0;
	vec2 g0 = w0 + w1;
	vec2 g1 = w2 + w3;
	vec2 h0 = ( i - 1.0 + w1 / g0 + 0.5 ) / Size;
	vec2 h1 = ( i + 1.0 + w3 / g1 + 0.5 ) / Size;
	return g0.y * ( g0.x * level0( Picture, h0 ) + g1.x * level0( Picture, vec2( h1.x, h0.y ) ) )
	     + g1.y * ( g0.x * level0( Picture, vec2( h0.x, h1.y ) ) + g1.x * level0( Picture, h1 ) );
}

void main()
{
	vec2 P = vec2( uv.x * Aspect, uv.y );
	vec2 Q = Scale * P + Bias;

	if( StretchGain > 0.0 )
	{
		//The pull is strongest down the centre column and falls away across
		//it as a Gaussian. A point StretchGain + 1 times further from the
		//centre shows what was at the centre's distance: the face drawn long,
		//and pinched in at the waist as it goes.
		vec2 d        = Q - StretchCentre;
		float across  = d.x / StretchWidth;
		float w       = exp( -across * across );
		float wobble  = Writhe * ( valueNoise( vec2( d.y * 3.0, Clock * 0.8 ), 7u ) - 0.5 );
		float k       = 1.0 + StretchGain * w * ( 1.0 + 0.8 * wobble );
		if( hooked( kHookSquash ) )
			k = 1.0 / k;
		float along   = d.y / ( StretchWidth * 1.6 );
		float pinch   = 1.0 + 0.12 * StretchGain * w * exp( -along * along );
		float sway    = Writhe * StretchWidth * 0.08 * StretchGain * w * ( valueNoise( vec2( d.y * 4.0, Clock * 0.5 + 11.0 ), 9u ) - 0.5 );
		Q.y           = StretchCentre.y + d.y / k;
		Q.x           = StretchCentre.x + d.x * pinch + sway;
	}

	//An exact identity stays exact: a 1:1 read, and no round trip through
	//Aspect.
	if( Q == P )
	{
		fragColour = texelFetch( Picture, texelOf( Size ), 0 );
		return;
	}
	vec2 q = uv + vec2( ( Q.x - P.x ) / Aspect, Q.y - P.y );
	vec4 plain = level0( Picture, q );
	fragColour = Smooth > 0.0 ? mix( plain, bspline( q ), Smooth ) : plain;
}
`;

// kMaskFragment, source/Shaders.cpp
export const MASK_FRAGMENT = `
uniform sampler2D Picture;
uniform float Lod;

void main()
{
	vec4 f = textureLod( Picture, uv, Lod );
	fragColour = vec4( subjectOf( straightOf( f ), f.a ) * f.a, 0.0, 0.0, 1.0 );
}
`;

// kBlurFragment, source/Shaders.cpp
export const BLUR_FRAGMENT = `
uniform sampler2D Mask;
uniform vec2 Direction;//one texel along the axis, 0..1
uniform float Sigma;   //texels

void main()
{
	//33 taps over +-3 sigma; wider than that the taps spread out and the
	//linear filter fills between them.
	float spacing = max( 1.0, 3.0 * Sigma / 16.0 );
	float total   = 0.0;
	float sum     = 0.0;
	for( int i = -16; i <= 16; ++i )
	{
		float x = float( i ) * spacing;
		float w = exp( -0.5 * x * x / ( Sigma * Sigma ) );
		sum += w * level0( Mask, uv + Direction * x ).r;
		total += w;
	}
	fragColour = vec4( sum / total, 0.0, 0.0, 1.0 );
}
`;

// kCompositeFragment, source/Shaders.cpp
export const COMPOSITE_FRAGMENT = `
uniform sampler2D InputTexture;
uniform sampler2D Framed;
uniform sampler2D Glow;
uniform sampler2D Atlas;
uniform vec2 Size;   //pixels
uniform float Aspect;
uniform float Clock;

uniform float ContrastAmount;
uniform vec3 TintDark;
uniform vec3 TintLight;
uniform float TintAmount;

uniform float Silhouette;
uniform float AuraGain;
uniform vec3 AuraColour;
uniform float Corona;
uniform vec2 GlowTexel;
uniform float GlowSigma;//the blur's, in glow texels

uniform float TorchAmount;
uniform vec2 TorchCentre;//frame heights
uniform float TorchRadius;

uniform float Exposure;
uniform float Vignette;

uniform float GrainAmount;
uniform int LayerCount;
uniform float LayerScale[ kMaxLayers ]; //the layer's grain cell, output pixels
uniform float LayerWeight[ kMaxLayers ];
uniform vec2 LayerShift[ kMaxLayers ];  //a layer's own lattice offset, cells
uniform int LayerSeed[ kMaxLayers ];
uniform vec2 GrainOrigin;               //output pixels: what the enlargement holds still

uniform float GreyAmount;
uniform float Flash;

uniform int GlyphCount;
uniform vec4 Glyphs[ kMaxGlyphs ];//code, cell left, cell top (pixels, y up), reveal
uniform vec4 CaptionBox;          //pixels: x0 y0 x1 y1
uniform float CaptionScale;       //output pixels per atlas pixel
uniform float CaptionEdge;        //half the field's step across one output pixel
uniform float CaptionGlow;
uniform float CaptionLevel;

uniform float MixAmount;

//The caption's coverage and glow at a pixel, each already faded by its
//letter's reveal.
vec2 caption( vec2 px )
{
	vec2 result = vec2( 0.0 );
	if( GlyphCount == 0 || px.x < CaptionBox.x || px.y < CaptionBox.y || px.x > CaptionBox.z || px.y > CaptionBox.w )
		return result;
	const vec2 atlasSize = vec2( float( kAtlasWidth ), float( kAtlasHeight ) );
	for( int i = 0; i < GlyphCount; ++i )
	{
		vec4 g     = Glyphs[ i ];
		vec2 local = vec2( px.x - g.y, g.z - px.y ) / CaptionScale;
		if( hooked( kHookCaptionBottom ) )
			local.y -= float( kCellHeight );
		if( local.x < 0.5 || local.y < 0.5 || local.x > float( kCellWidth ) - 0.5 || local.y > float( kCellHeight ) - 0.5 )
			continue;
		int index  = int( g.x ) - kFirst;
		int column = index % kColumns;
		int row    = index / kColumns;
		vec2 at    = vec2( float( column * kCellWidth ) + local.x, atlasSize.y - ( float( row * kCellHeight ) + local.y ) );
		float d    = level0( Atlas, at / atlasSize ).r;
		float cover = smoothstep( 0.5 - CaptionEdge, 0.5 + CaptionEdge, d );
		float halo  = smoothstep( 0.0, 0.5, d );
		result      = max( result, vec2( cover, halo * halo ) * g.w );
	}
	return result;
}

void main()
{
	ivec2 here = texelOf( Size );
	vec4 dry   = texelFetch( InputTexture, here, 0 );
	vec4 f     = texelFetch( Framed, here, 0 );
	float a  = f.a;
	vec3 c   = straightOf( f );
	vec2 P   = vec2( uv.x * Aspect, uv.y );

	//The subject, at full size, for Silhouette and so the aura falls round it.
	float subject = ( Silhouette > 0.0 || AuraGain > 0.0 ) ? subjectOf( c, a ) : 0.0;
	c *= 1.0 - Silhouette * subject;

	//Contrast: towards smoothstep, which crushes both ends. Then the tint: a
	//duotone from the shadow colour to the light one along the luma.
	c = clamp( c, 0.0, 1.0 );
	c = mix( c, c * c * ( 3.0 - 2.0 * c ), ContrastAmount );
	c = mix( c, mix( TintDark, TintLight, luma( c ) ), TintAmount );

	//The torch, in linear light: a dim room, and a beam with a hot middle.
	if( TorchAmount > 0.0 )
	{
		vec2 centre = TorchCentre;
		if( hooked( kHookTorchOffset ) )
			centre.x += 0.1 * Aspect;
		vec2 d      = ( P - centre ) / TorchRadius;
		float r2    = dot( d, d );
		float beam  = exp( -r2 ) + 0.6 * exp( -4.0 * r2 );
		float light = 1.0 - TorchAmount + TorchAmount * ( 0.03 + 1.25 * beam );
		c = toCode( toLinear( c ) * light );
	}

	//The aura: the subject's blurred mask, where it spills past the subject,
	//in flickering streamers that stream outwards.
	vec3 aura = vec3( 0.0 );
	if( AuraGain > 0.0 )
	{
		float g    = level0( Glow, uv ).r;
		float gx   = level0( Glow, uv + vec2( GlowTexel.x, 0.0 ) ).r - level0( Glow, uv - vec2( GlowTexel.x, 0.0 ) ).r;
		float gy   = level0( Glow, uv + vec2( 0.0, GlowTexel.y ) ).r - level0( Glow, uv - vec2( 0.0, GlowTexel.y ) ).r;
		//Glow texels are square (a quarter of the raster each way), so the
		//two differences are already in the same units.
		vec2 grad    = vec2( gx, gy );
		float len    = length( grad );
		vec2 outward = len > 1e-6 ? -grad / len : vec2( 0.0, 1.0 );
		vec2 along   = vec2( -outward.y, outward.x );
		vec2 s       = vec2( dot( P, along ) * 26.0, dot( P, outward ) * 7.0 - Clock * 2.2 );
		float n      = 0.6 * valueNoise( s, 21u ) + 0.4 * valueNoise( s * 2.3 + 17.0, 22u );
		//Streamers only where the glow has an edge to stream from, and only
		//outside the subject. Across a broad, half-keyed area the glow is
		//flat, its gradient is noise, and streamers along noise are worms.
		//A step blurred by sigma differs by 0.8 / sigma across two texels at
		//its edge, so len * sigma is 0.8 there and falls away either side.
		float edge    = smoothstep( 0.08, 0.3, len * GlowSigma );
		float flicker = mix( 1.0, 0.2 + 1.7 * n, Corona * edge * ( 1.0 - subject ) );
		//A bright subject keeps a little of its glow, which tints it, less
		//as Silhouette darkens it; a dark one keeps none. Either way a
		//silhouette is a black mass against the light, not a blue one.
		float inside = KeyMode == 1 ? 1.0 : mix( 0.7, 1.0, Silhouette );
		float where  = hooked( kHookGlowInside ) ? subject : 1.0 - inside * subject;
		aura = AuraColour * ( AuraGain * g * where * flicker );
	}

	//The camera: exposure flicker and the dark edge, on everything it saw.
	vec2 fromCentre = ( uv - 0.5 ) * vec2( Aspect, 1.0 ) / ( 0.5 * sqrt( Aspect * Aspect + 1.0 ) );
	float edge      = 1.0 - Vignette * smoothstep( 0.25, 1.05, length( fromCentre ) );
	c *= Exposure * edge;
	aura *= Exposure * edge;

	//Grain: the live layer, and the layers each earlier generation left in
	//its print, every one enlarged with the picture since.
	if( GrainAmount > 0.0 )
	{
		vec2 px  = uv * Size - GrainOrigin;
		float n  = 0.0;
		for( int i = 0; i < LayerCount; ++i )
		{
			float cell = hooked( kHookFlatGrain ) ? LayerScale[ LayerCount - 1 ] : LayerScale[ i ];
			n += LayerWeight[ i ] * ( valueNoise( px / cell + LayerShift[ i ], uint( LayerSeed[ i ] ) ) - 0.5 );
		}
		float y = luma( c );
		c += vec3( n * GrainAmount * ( 0.35 + 2.6 * y * ( 1.0 - y ) ) );
	}

	vec3 pm    = clamp( c, 0.0, 1.0 ) * a + aura;
	float outA = a + ( 1.0 - a ) * clamp( max( aura.r, max( aura.g, aura.b ) ), 0.0, 1.0 );

	//The freeze: to a punchy grey, and the flash of the still being taken.
	if( !hooked( kHookNoGrey ) )
	{
		float y = luma( pm );
		y       = mix( y, y * y * ( 3.0 - 2.0 * y ), 0.6 );
		pm      = mix( pm, vec3( y ), GreyAmount );
	}
	pm = mix( pm, vec3( outA ), Flash );

	//The caption, over the lot.
	vec2 text = caption( uv * Size );
	float tA  = clamp( text.x + CaptionGlow * text.y * ( 1.0 - text.x ), 0.0, 1.0 );
	pm        = pm * ( 1.0 - tA ) + CaptionLevel * vec3( text.x + 0.7 * CaptionGlow * text.y * ( 1.0 - text.x ) );
	outA      = outA + ( 1.0 - outA ) * tA;

	//Both ends of Mix are exact, chosen rather than mixed: at 1, the clip's
	//bits must not leak into a frozen frame through mix( a, b, 1.0 ), which
	//is not promised to be b.
	vec4 wet = vec4( clamp( pm, 0.0, outA ), outA );
	fragColour = MixAmount >= 1.0 ? wet : MixAmount <= 0.0 ? dry : mix( dry, wet, MixAmount );
}
`;
