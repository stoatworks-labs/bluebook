// The checks. Included into main.cpp after the rig; everything in its
// anonymous namespace is in reach.
//
// Each GL check runs at the raster asked for (640x360 unless --size) AND at
// 320x180, which is CI's. Each takes a Perturb: the shipped model when empty,
// a wrong one under --negative, where the check must FAIL. A check that cannot
// fail is not a check.

namespace
{
/// One half-float ULP at 1.0 (the spacing of RGBA16F in [1, 2)); every
/// buffer between the clip and the host is RGBA16F, so a value that went
/// through one is within half of this of where it started.
constexpr double kHalfUlp = 1.0 / 1024.0;

struct Perturb
{
	int hooks          = 0;
	double rateModel   = 1.0;///< the print check's model of Step Rate, as a factor
	double halfModel   = 1.0;///< the trail check's model of its half-life
	double depthModel  = 1.0;///< the enlargement check's model of its depth
	double grainModel  = 1.0;///< the grain check's model of how variance grows
	double weaveModel  = 1.0;///< the weave check's model of how far a print may sit
	double gradeModel  = 1.0;///< the grade check's model of Contrast and Vignette
	bool any() const
	{
		return hooks != 0 || rateModel != 1.0 || halfModel != 1.0 || depthModel != 1.0 || grainModel != 1.0 || weaveModel != 1.0
		    || gradeModel != 1.0;
	}
};

Perturb weaveModelled( double factor )
{
	Perturb p;
	p.weaveModel = factor;
	return p;
}

Perturb gradeModelled( double factor )
{
	Perturb p;
	p.gradeModel = factor;
	return p;
}

struct Raster
{
	int width, height;
};

/// Every stage off: the clip should come through untouched.
const std::vector< std::string > kQuiet = {
	"Stutter=0", "Weave=0", "Trail=0", "Enlarge=0", "Stretch=0", "Aura=0", "Silhouette=0", "Torch=0", "Tint=Natural",
	"Contrast=0", "Grain=0", "Flicker=0", "Vignette=0", "Caption Show=0",
};

std::unique_ptr< Rig > makeRig( const Raster& r, const std::vector< std::string >& settings, const Floats& picture, int hooks )
{
	auto rig = std::make_unique< Rig >();
	if( !rig->Init( r.width, r.height, &picture ) )
		return nullptr;
	rig->plugin.SetHooksForTest( hooks );
	for( const std::string& s : settings )
	{
		std::string error;
		if( !applySetting( rig->plugin, s, error ) )
		{
			std::fprintf( stderr, "bbtest: %s: %s\n", s.c_str(), error.c_str() );
			return nullptr;
		}
	}
	return rig;
}

std::vector< std::string > with( std::vector< std::string > base, const std::vector< std::string >& more )
{
	base.insert( base.end(), more.begin(), more.end() );
	return base;
}

const float* at( const Floats& p, int width, int x, int y )
{
	return &p[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
}

double lumaOf( const float* p )
{
	return 0.2126 * p[ 0 ] + 0.7152 * p[ 1 ] + 0.0722 * p[ 2 ];
}

double meanLuma( const Floats& p, int width, int height )
{
	double sum = 0.0;
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
			sum += lumaOf( at( p, width, x, y ) );
	return sum / ( static_cast< double >( width ) * height );
}

/// The luma-weighted centroid of everything brighter than `floor` in a box.
bool centroid( const Floats& p, int width, int x0, int y0, int x1, int y1, double floor, double& cx, double& cy )
{
	double sum = 0.0, sx = 0.0, sy = 0.0;
	for( int y = std::max( 0, y0 ); y < y1 && y < static_cast< int >( p.size() / 4 / width ); ++y )
		for( int x = std::max( 0, x0 ); x < x1 && x < width; ++x )
		{
			const double v = lumaOf( at( p, width, x, y ) ) - floor;
			if( v <= 0.0 )
				continue;
			sum += v;
			sx += v * ( x + 0.5 );
			sy += v * ( y + 0.5 );
		}
	if( sum <= 0.0 )
		return false;
	cx = sx / sum;
	cy = sy / sum;
	return true;
}

/// A picture of black with white Gaussian dots, sigma pixels, at the points.
Floats dots( int width, int height, const std::vector< std::pair< double, double > >& points, double sigma )
{
	Floats out = flatCard( width, height, 0.0f, 0.0f, 0.0f );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			double v = 0.0;
			for( const auto& pt : points )
			{
				const double dx = x + 0.5 - pt.first, dy = y + 0.5 - pt.second;
				v += std::exp( -( dx * dx + dy * dy ) / ( 2.0 * sigma * sigma ) );
			}
			float* o = &out[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			o[ 0 ] = o[ 1 ] = o[ 2 ] = static_cast< float >( std::min( 1.0, v ) );
		}
	return out;
}

std::string setting( const char* name, double value )
{
	return fmt( "%s=%.9g", name, value );
}

//===========================================================================
// --identity: every stage off is the clip, and Mix 0 is the clip bit for bit.
//===========================================================================
int runIdentity( const Raster& r, const Perturb& perturb )
{
	const int before = g_failures;
	const Floats card = buildCard( r.width, r.height, 0.7 );
	{
		//The negative: one stage left a hair on.
		const auto settings = perturb.any() ? with( kQuiet, { "Contrast=0.05" } ) : kQuiet;
		auto rig            = makeRig( r, settings, card, 0 );
		if( !rig || !rig->Render( 30 ) )
			return 1;
		const Floats out = rig->Output();
		double worst     = 0.0;
		for( size_t i = 0; i < out.size(); ++i )
			worst = std::max( worst, static_cast< double >( std::fabs( out[ i ] - card[ i ] ) ) );
		Check( worst <= kHalfUlp, fmt( "every stage off: the clip within one half-float ULP (worst %.2e <= %.2e)", worst, kHalfUlp ) );
	}
	{
		//Every stage on, Mix 0: the dry path reads the clip and nothing else.
		auto rig = makeRig( r, { "Aura=0.6", "Stretch=0.5", "Enlarge=0.6", "Torch=0.8", "Caption Show=1", perturb.any() ? "Mix=0.01" : "Mix=0" }, card, 0 );
		if( !rig || !rig->Render( 30 ) )
			return 1;
		const Floats out = rig->Output();
		size_t differ    = 0;
		for( size_t i = 0; i < out.size(); ++i )
			differ += out[ i ] != card[ i ];
		Check( differ == 0, fmt( "everything on, Mix 0: the clip bit for bit (%zu values differ)", differ ) );
	}
	return g_failures - before;
}

//===========================================================================
// --print: a print holds until the next, the next lands when Step Rate says,
// and the dissolve between them is a straight cross-fade over Dissolve of the
// interval. The clip is a flat colour that changes every frame, so the red the
// host gets back says which frame's print it is looking at.
//===========================================================================
double codeOf( int frame )
{
	return ( ( frame * 37 ) % 251 ) / 255.0;
}

int runPrint( const Raster& r, const Perturb& perturb )
{
	const int before   = g_failures;
	const double rate  = 7.0;// prints a second: no print lands on a frame boundary before frame 60
	const double model = rate * perturb.rateModel;
	for( const double dissolve : { 0.0, 0.5 } )
	{
		auto rig = makeRig( r, with( kQuiet, { "Stutter=1", setting( "Step Rate", ParamFromStepRate( rate ) ), setting( "Dissolve", dissolve ) } ),
		                    flatCard( r.width, r.height, static_cast< float >( codeOf( 0 ) ), 0.5f, 0.25f ), perturb.hooks );
		if( !rig )
			return 1;
		rig->beforeFrame = [ & ]( int f ) { rig->Upload( flatCard( r.width, r.height, static_cast< float >( codeOf( f ) ), 0.5f, 0.25f ) ); };
		int newest = 0, older = 0, wrong = 0, worstFrame = -1;
		double worst = 0.0;
		for( int f = 0; f < 59; ++f )
		{
			if( !rig->Render( 1 ) )
				return 1;
			//The model: frame f is at f / 60 s; a print is taken on the frame
			//whose position in prints passes a whole number.
			const double position = f * model / 60.0;
			if( f > 0 && std::floor( position ) != std::floor( ( f - 1 ) * model / 60.0 ) )
			{
				older  = newest;
				newest = f;
			}
			const double into     = position - std::floor( position );
			const double fade     = dissolve > 0.0 ? std::min( 1.0, into / dissolve ) : 1.0;
			const double expected = codeOf( older ) + ( codeOf( newest ) - codeOf( older ) ) * fade;
			const double got      = at( rig->Output(), r.width, r.width / 2, r.height / 2 )[ 0 ];
			const double error    = std::fabs( got - expected );
			if( error > worst )
				worst = error, worstFrame = f;
			wrong += error > kHalfUlp;
		}
		Check( wrong == 0, fmt( "Dissolve %.1f at %g prints a second: every one of 59 frames shows the print and the mix the model says "
		                        "(worst %.1e at frame %d)",
		                        dissolve, rate, worst, worstFrame ) );
	}
	return g_failures - before;
}

//===========================================================================
// --beat: Step On Beat and Bar print on the host's beat, not near it.
//===========================================================================
int runBeat( const Raster& r, const Perturb& perturb )
{
	const int before = g_failures;
	struct Case
	{
		const char* on;
		int every;// frames at 120 BPM and 60 fps
	};
	for( const Case c : { Case { "Beat", 30 }, Case { "Bar", 120 } } )
	{
		auto rig = makeRig( r, with( kQuiet, { "Stutter=1", "Dissolve=0", std::string( "Step On=" ) + c.on } ),
		                    flatCard( r.width, r.height, 0.0f, 0.0f, 0.0f ), perturb.hooks );
		if( !rig )
			return 1;
		rig->bpm         = 120.0;
		rig->beforeFrame = [ & ]( int f ) { rig->Upload( flatCard( r.width, r.height, static_cast< float >( codeOf( f ) ), 0.5f, 0.25f ) ); };
		const int every  = static_cast< int >( c.every * perturb.rateModel );
		int wrong        = 0;
		for( int f = 0; f < 250; ++f )
		{
			if( !rig->Render( 1 ) )
				return 1;
			const int shown = ( f / every ) * every;
			const double got = at( rig->Output(), r.width, r.width / 2, r.height / 2 )[ 0 ];
			wrong += std::fabs( got - codeOf( shown ) ) > kHalfUlp;
		}
		Check( wrong == 0, fmt( "Step On %s at 120 BPM: a print every %d frames, on the beat (%d of 250 frames wrong)", c.on, c.every, wrong ) );
	}
	return g_failures - before;
}

//===========================================================================
// --trail: the smear is a one-pole whose half-life is Trail's, and it gets
// where it is going. Eight half-lives of a step from black to white, at a
// short trail and the longest.
//===========================================================================
int runTrail( const Raster& r, const Perturb& perturb )
{
	const int before = g_failures;
	for( const float trail : { 0.4f, 1.0f } )
	{
		const double halfLife = TrailHalfLifeFromParam( trail );
		auto rig = makeRig( r, with( kQuiet, { setting( "Trail", trail ) } ), flatCard( r.width, r.height, 0, 0, 0 ), perturb.hooks );
		if( !rig )
			return 1;
		rig->beforeFrame = [ & ]( int f ) {
			const float v = f >= 30 ? 1.0f : 0.0f;
			rig->Upload( flatCard( r.width, r.height, v, v, v ) );
		};
		if( !rig->Render( 30 ) )
			return 1;
		const double decay = std::pow( 0.5, ( 1.0 / 60.0 ) / ( halfLife * perturb.halfModel ) );
		//The smear is full float, so all that is left is the one conversion
		//into the half-float framed picture: a ULP in [0.5, 1), not half of
		//one, because nothing promises the conversion rounds to nearest (on
		//an M-series GPU the error reached the whole ULP). The smear's own
		//rounding, 2^-24 a frame over ( 1 - decay ), is two orders under. A
		//half-float smear stalls 6 % short at Trail 1, and fails this.
		const double tolerance = std::ldexp( 1.0, -11 ) + 1e-6;
		const int frames       = static_cast< int >( std::ceil( 8.0 * halfLife * 60.0 ) );
		double worst = 0.0, last = 0.0;
		int crossed = -1;
		for( int k = 0; k < frames; ++k )
		{
			if( !rig->Render( 1 ) )
				return 1;
			const double got      = at( rig->Output(), r.width, r.width / 2, r.height / 2 )[ 1 ];
			const double expected = 1.0 - std::pow( decay, k + 1 );
			worst                 = std::max( worst, std::fabs( got - expected ) );
			last                  = got;
			if( crossed < 0 && got >= 0.5 )
				crossed = k + 1;
		}
		const double halfFrames = halfLife * perturb.halfModel * 60.0;
		Check( worst <= tolerance, fmt( "Trail %.1f: a step from black to white rises as 1 - 0.5^( t / %.2f s ) for %d frames, ending at %.4f "
		                                "(worst %.2e <= %.2e)",
		                                trail, halfLife, frames, last, worst, tolerance ) );
		Check( crossed >= 0 && std::fabs( crossed - halfFrames ) <= 1.0,
		       fmt( "Trail %.1f: half way after %d frames; the half-life is %.1f frames (one frame's slack)", trail, crossed, halfFrames ) );
	}
	return g_failures - before;
}

//===========================================================================
// --weave: each print sits somewhere new, never further off than Weave says
// (1.2 % of the frame height at the top), and never with its edge showing.
//===========================================================================
int runWeave( const Raster& r, const Perturb& perturb )
{
	const int before = g_failures;
	const int w = r.width, h = r.height;
	const double cx = 0.5 * w, cy = 0.5 * h;
	//A dot in the middle, and a red border round the clip, a pixel wide.
	Floats picture = dots( w, h, { { cx, cy } }, 1.5 );
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
			if( x == 0 || y == 0 || x == w - 1 || y == h - 1 )
			{
				float* o = &picture[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
				o[ 0 ] = 1.0f, o[ 1 ] = o[ 2 ] = 0.0f;
			}
	auto rig = makeRig( r, with( kQuiet, { "Stutter=1", "Dissolve=0", "Weave=1", setting( "Step Rate", ParamFromStepRate( 8.0 ) ) } ), picture,
	                    perturb.hooks );
	if( !rig )
		return 1;
	//The print at the centre only moves by its offset, which the overscan
	//enlarges with the rest of the print: Weave's 1.2 % each way, times
	//1 / ( 1 - 2 x 1.2 % ). The slack is the centroid's own quarter pixel.
	const double weave = WeaveFromParam( 1.0f );
	const double bound = weave / ( 1.0 - 2.0 * weave ) * h * perturb.weaveModel + 0.25;
	double worst = 0.0, lowX = 1e9, highX = -1e9, lowY = 1e9, highY = -1e9;
	long long lastStep = -1;
	int prints         = 0;
	size_t edge        = 0;
	for( int f = 0; f < 150; ++f )
	{
		if( !rig->Render( 1 ) )
			return 1;
		if( rig->plugin.LastFrame().steps == lastStep )
			continue;
		lastStep = rig->plugin.LastFrame().steps;
		++prints;
		const Floats out = rig->Output();
		double x, y;
		if( !centroid( out, w, 2, 2, w - 2, h - 2, 0.02, x, y ) )
			continue;
		worst = std::max( worst, std::max( std::fabs( x - cx ), std::fabs( y - cy ) ) );
		lowX = std::min( lowX, x ), highX = std::max( highX, x ), lowY = std::min( lowY, y ), highY = std::max( highY, y );
		//The print's edge. The overscan keeps it out of the frame, so the
		//red border is cropped away, or at a print's furthest reach just
		//touches the outermost pixel. Too little overscan and the capture's
		//clamp smears it inwards by as much as the print is short.
		for( int j = 2; j < h - 2; ++j )
			for( int i = 2; i < w - 2; ++i )
			{
				const float* p = at( out, w, i, j );
				edge += p[ 0 ] - p[ 1 ] > 0.05f;
			}
	}
	Check( worst <= bound, fmt( "over %d prints the dot sits at most %.2f px off; Weave 1 allows %.2f", prints, worst, bound ) );
	Check( highX - lowX > 0.5 * bound && highY - lowY > 0.5 * bound,
	       fmt( "and it does move: %.2f px across, %.2f px up (more than half the allowance)", highX - lowX, highY - lowY ) );
	Check( edge == 0, fmt( "no print's edge shows: the clip's red border never reaches 2 px into the frame (%zu pixels)", edge ) );
	return g_failures - before;
}

//===========================================================================
// --grade: Tint maps colour to a duotone along luma alone; Contrast is a
// mix towards smoothstep; Vignette darkens by its stated curve.
//===========================================================================
int runGrade( const Raster& r, const Perturb& perturb )
{
	const int before = g_failures;
	const int w = r.width, h = r.height;
	{
		//Two colours of the same luma, side by side.
		Floats pair = flatCard( w, h, 0.0f, 0.0f, 0.0f );
		const double yy = 0.2126 * 0.9 + 0.7152 * 0.2 + 0.0722 * 0.3;
		for( int y = 0; y < h; ++y )
			for( int x = 0; x < w; ++x )
			{
				float* o = &pair[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
				if( x < w / 2 )
					o[ 0 ] = 0.9f, o[ 1 ] = 0.2f, o[ 2 ] = 0.3f;
				else
					o[ 0 ] = o[ 1 ] = o[ 2 ] = static_cast< float >( yy );
			}
		auto rig = makeRig( r, with( kQuiet, { "Tint=Teal", "Tint Amount=1" } ), pair, perturb.hooks );
		if( !rig || !rig->Render( 2 ) )
			return 1;
		const Floats out = rig->Output();
		const float* a   = at( out, w, w / 4, h / 2 );
		const float* b   = at( out, w, 3 * w / 4, h / 2 );
		//Float luma of the two differs in the last bits only; through the
		//half-float framed picture that is one ULP at most.
		double worst = 0.0;
		for( int c = 0; c < 3; ++c )
			worst = std::max( worst, static_cast< double >( std::fabs( a[ c ] - b[ c ] ) ) );
		Check( worst <= kHalfUlp && a[ 1 ] > a[ 0 ], fmt( "Tint Teal at 1: a red and a grey of the same luma come out the same teal (%.3f %.3f %.3f; "
		                                                   "differ by %.1e)",
		                                                   a[ 0 ], a[ 1 ], a[ 2 ], worst ) );
	}
	{
		auto rig = makeRig( r, with( kQuiet, { setting( "Contrast", 1.0 * perturb.gradeModel ) } ), flatCard( w, h, 0.25f, 0.25f, 0.25f ),
		                    perturb.hooks );
		if( !rig || !rig->Render( 2 ) )
			return 1;
		const double got = at( rig->Output(), w, w / 2, h / 2 )[ 0 ];
		const double expected = 0.25 * 0.25 * ( 3.0 - 0.5 );
		Check( std::fabs( got - expected ) <= 0.5 * kHalfUlp, fmt( "Contrast 1: a 0.25 grey becomes smoothstep's %.5f (got %.5f)", expected, got ) );
	}
	{
		auto rig = makeRig( r, with( kQuiet, { "Vignette=1" } ), flatCard( w, h, 0.6f, 0.6f, 0.6f ), perturb.hooks );
		if( !rig || !rig->Render( 2 ) )
			return 1;
		const Floats out  = rig->Output();
		const double aspect = static_cast< double >( w ) / h;
		auto expectAt = [ & ]( int x, int y ) {
			const double u = ( x + 0.5 ) / w - 0.5, v = ( y + 0.5 ) / h - 0.5;
			const double L = std::hypot( u * aspect, v ) / ( 0.5 * std::sqrt( aspect * aspect + 1.0 ) );
			const double t = std::clamp( ( L - 0.25 ) / 0.8, 0.0, 1.0 );
			return 0.6 * ( 1.0 - 0.85 * perturb.gradeModel * t * t * ( 3.0 - 2.0 * t ) );
		};
		double worst = 0.0;
		for( const auto& p : { std::make_pair( 0, 0 ), std::make_pair( w - 1, h - 1 ), std::make_pair( w / 2, h / 2 ), std::make_pair( w / 8, h / 3 ) } )
			worst = std::max( worst, std::fabs( at( out, w, p.first, p.second )[ 0 ] - expectAt( p.first, p.second ) ) );
		Check( worst <= 0.5 * kHalfUlp, fmt( "Vignette 1: the corners keep %.4f of a 0.6 grey, the middle all of it (worst %.1e)", expectAt( 0, 0 ), worst ) );
	}
	return g_failures - before;
}

//===========================================================================
// --enlarge: the enlargement magnifies by Magnify ^ ( depth / Generations)
// about its target, measured as the spread of two dots either side of it.
//===========================================================================
int runEnlarge( const Raster& r, const Perturb& perturb )
{
	const int before      = g_failures;
	const double deepest  = 4.0;
	const int generations = 4;
	const double enlarge  = 0.75;
	const double expected = std::pow( deepest, enlarge * perturb.depthModel );
	const double d        = 0.06 * r.height;// pixels each side of the target
	const double cx = 0.5 * r.width, cy = 0.5 * r.height;
	const Floats picture  = dots( r.width, r.height, { { cx - d, cy }, { cx + d, cy }, { cx, cy - d }, { cx, cy + d } }, 1.5 );
	auto rig = makeRig( r,
	                    with( kQuiet, { setting( "Magnify", ParamFromMagnify( deepest ) ), fmt( "Generations=%d", generations ),
	                                    setting( "Enlarge", enlarge ) } ),
	                    picture, perturb.hooks );
	if( !rig || !rig->Render( 10 ) )
		return 1;
	const Floats out = rig->Output();
	const int w = r.width, h = r.height;
	double lx, ly, rx, ry, bx, by, tx, ty;
	const bool found = centroid( out, w, 0, static_cast< int >( cy - d ), static_cast< int >( cx - d ), static_cast< int >( cy + d ), 0.02, lx, ly )
	                && centroid( out, w, static_cast< int >( cx + d ), static_cast< int >( cy - d ), w, static_cast< int >( cy + d ), 0.02, rx, ry )
	                && centroid( out, w, static_cast< int >( cx - d ), 0, static_cast< int >( cx + d ), static_cast< int >( cy - d ), 0.02, bx, by )
	                && centroid( out, w, static_cast< int >( cx - d ), static_cast< int >( cy + d ), static_cast< int >( cx + d ), h, 0.02, tx, ty );
	if( !Check( found, "all four dots are still in the frame" ) )
		return g_failures - before;
	const double across = ( rx - lx ) / ( 2.0 * d );
	const double up     = ( ty - by ) / ( 2.0 * d );
	//The centroid of a blob the B-spline has spread symmetrically moves by
	//nothing; what is left is the blob's own sampling, a quarter of a pixel
	//at each end.
	const double tolerance = 0.5 / ( 2.0 * d );
	Check( std::fabs( across - expected ) <= tolerance * expected && std::fabs( up - expected ) <= tolerance * expected,
	       fmt( "Magnify 4, depth 3 of 4: x%.4f across and x%.4f up, expected x%.4f (+-%.2f%%)", across, up, expected, 100.0 * tolerance ) );
	//About the target: the dots' mean stays within the generations' own
	//mis-registration of where the target was (0.004 frame heights a print).
	const double driftX = std::fabs( 0.5 * ( lx + rx ) - cx ), driftY = std::fabs( 0.5 * ( by + ty ) - cy );
	const double allowed = 0.004 * generations * r.height * expected + 0.5;
	Check( driftX <= allowed && driftY <= allowed, fmt( "about the target: it moved %.2f, %.2f px (allowed %.2f)", driftX, driftY, allowed ) );
	return g_failures - before;
}

//===========================================================================
// --grain: each generation's grain stays in its print and is enlarged with
// it. Deeper in, the grain adds a layer per generation (its variance grows
// by one live layer's) and the old layers are coarse (it correlates over a
// distance the live grain does not).
//===========================================================================
struct GrainStats
{
	double variance;
	double lagged;// correlation at a lag of three pixels at 180 lines
};

GrainStats grainOf( const Floats& p, int width, int height, int lag )
{
	double sum = 0.0, sum2 = 0.0, cross = 0.0;
	size_t n = 0, m = 0;
	std::vector< double > g( static_cast< size_t >( width ) * height );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double v                          = at( p, width, x, y )[ 1 ] - 0.5;
			g[ static_cast< size_t >( y ) * width + x ] = v;
			sum += v;
			sum2 += v * v;
			++n;
		}
	const double mean = sum / n;
	const double var  = sum2 / n - mean * mean;
	for( int y = 0; y < height; ++y )
		for( int x = 0; x + lag < width; ++x )
		{
			cross += ( g[ static_cast< size_t >( y ) * width + x ] - mean ) * ( g[ static_cast< size_t >( y ) * width + x + lag ] - mean );
			++m;
		}
	return { var, cross / m / var };
}

int runGrain( const Raster& r, const Perturb& perturb )
{
	const int before      = g_failures;
	const int generations = 4;
	const int lag         = std::max( 1, static_cast< int >( std::lround( 3.0 * r.height / 180.0 ) ) );
	auto statsAt = [ & ]( double enlarge, int hooks, GrainStats& stats ) {
		auto rig = makeRig( r,
		                    with( kQuiet, { "Grain=1", "Grain Size=1", setting( "Magnify", ParamFromMagnify( 8.0 ) ),
		                                    fmt( "Generations=%d", generations ), setting( "Enlarge", enlarge ) } ),
		                    flatCard( r.width, r.height, 0.5f, 0.5f, 0.5f ), hooks );
		if( !rig || !rig->Render( 5 ) )
			return false;
		stats = grainOf( rig->Output(), r.width, r.height, lag );
		return true;
	};
	GrainStats live, deep;
	if( !statsAt( 0.0, perturb.hooks, live ) || !statsAt( 1.0, perturb.hooks, deep ) )
		return 1;
	//Layers are independent and alike, so variance adds: the live one and
	//one per generation. The slack is the sampling error of a variance over
	//the ~2 000 grains of the coarsest layer at 320x180, and each layer's
	//phase against the pixel grid.
	const double ratio    = deep.variance / live.variance;
	const double expected = ( generations + 1 ) * perturb.grainModel;
	Check( std::fabs( ratio - expected ) <= 0.2 * expected,
	       fmt( "the grain's variance, %d generations in, is %.2fx the live grain's (expected %.0fx +-20%%)", generations, ratio, expected ) );
	Check( live.lagged < 0.05 && deep.lagged > 0.08,
	       fmt( "correlation %d px apart: live %.3f (< 0.05), %d generations in %.3f (> 0.08): the old grain is enlarged", lag, live.lagged,
	            generations, deep.lagged ) );
	return g_failures - before;
}

//===========================================================================
// --stretch: down the centre column, what was d from the centre is now
// Stretch x d from it; three widths across, nothing moved.
//===========================================================================
Floats lines( int width, int height, double cy, double d )
{
	Floats out = flatCard( width, height, 0, 0, 0 );
	for( int y = 0; y < height; ++y )
	{
		const double yy = y + 0.5;
		const double v  = std::max( std::exp( -0.5 * std::pow( ( yy - ( cy + d ) ) / 1.2, 2 ) ), std::exp( -0.5 * std::pow( ( yy - ( cy - d ) ) / 1.2, 2 ) ) );
		for( int x = 0; x < width; ++x )
		{
			float* o = &out[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			o[ 0 ] = o[ 1 ] = o[ 2 ] = static_cast< float >( v );
		}
	}
	return out;
}

/// The centroids of the bright rows above and below `cy` in column x.
bool rowsAt( const Floats& p, int width, int height, int x, double cy, double& above, double& below )
{
	double sa = 0, wa = 0, sb = 0, wb = 0;
	for( int y = 0; y < height; ++y )
	{
		const double v = lumaOf( at( p, width, x, y ) );
		if( v < 0.05 )
			continue;
		if( y + 0.5 > cy )
			sa += v * ( y + 0.5 ), wa += v;
		else
			sb += v * ( y + 0.5 ), wb += v;
	}
	if( wa <= 0 || wb <= 0 )
		return false;
	above = sa / wa;
	below = sb / wb;
	return true;
}

int runStretch( const Raster& r, const Perturb& perturb )
{
	const int before   = g_failures;
	const float amount = 0.25f;// x2 at the centre
	const double k     = StretchFromParam( amount );
	const double cy    = 0.5 * r.height, d = 0.12 * r.height;
	const float widthParam = 0.2f;
	auto rig = makeRig( r, with( kQuiet, { setting( "Stretch", amount ), "Writhe=0", setting( "Stretch Width", widthParam ) } ),
	                    lines( r.width, r.height, cy, d ), perturb.hooks );
	if( !rig || !rig->Render( 5 ) )
		return 1;
	const Floats out = rig->Output();
	double above, below;
	if( !Check( rowsAt( out, r.width, r.height, r.width / 2, cy, above, below ), "both lines are in the centre column" ) )
		return g_failures - before;
	//A line spread over k times the rows stays centred on where it lands:
	//half a pixel for the sampling at each end.
	Check( std::fabs( ( above - cy ) - k * d ) <= 0.5 && std::fabs( ( cy - below ) - k * d ) <= 0.5,
	       fmt( "centre column: the lines %.2f and %.2f px out, expected %.2f (Stretch x%.1f of %.2f)", above - cy, cy - below, k * d, k, d ) );
	const double away = 3.0 * StretchWidthFromParam( widthParam ) * r.height;
	const int column  = static_cast< int >( r.width / 2 + away );
	if( column < r.width && rowsAt( out, r.width, r.height, column, cy, above, below ) )
		Check( std::fabs( ( above - cy ) - d ) <= 0.05 && std::fabs( ( cy - below ) - d ) <= 0.05,
		       fmt( "three widths across: the lines %.3f and %.3f px out, where they were (%.2f)", above - cy, cy - below, d ) );
	else
		Check( false, "three widths across is outside the frame" );
	return g_failures - before;
}

//===========================================================================
// --torch: the beam lands where it is aimed, round, brightest in the middle;
// the room outside it keeps 3 % of its light; a sweep crosses in Sweep's time.
//===========================================================================
int runTorch( const Raster& r, const Perturb& perturb )
{
	const int before = g_failures;
	const double aimX = 0.35, aimY = 0.6;
	const float size  = 0.35f;
	const Floats grey = flatCard( r.width, r.height, 0.2f, 0.2f, 0.2f );
	auto rig = makeRig( r, with( kQuiet, { "Torch=1", "Torch Sweep=0", setting( "Torch X", aimX ), setting( "Torch Y", aimY ), setting( "Torch Size", size ) } ),
	                    grey, perturb.hooks );
	if( !rig || !rig->Render( 3 ) )
		return 1;
	const Floats out = rig->Output();
	const double corner = lumaOf( at( out, r.width, 0, 0 ) );
	double cx, cy;
	centroid( out, r.width, 0, 0, r.width, r.height, corner, cx, cy );
	const double ax = aimX * r.width, ay = aimY * r.height;
	//A symmetric pool's centroid is its centre, less what the frame's edge
	//cuts off: the beam at Size 0.35 is 0.11 frame heights to 1/e, so its
	//tail past the nearest edge is a small fraction of a pixel's pull.
	Check( std::hypot( cx - ax, cy - ay ) <= 1.0, fmt( "the beam is centred at %.2f, %.2f px; aimed at %.2f, %.2f", cx, cy, ax, ay ) );
	const double radius = TorchSizeFromParam( size ) * r.height;
	const int rx = static_cast< int >( std::lround( radius ) );
	const double e = lumaOf( at( out, r.width, static_cast< int >( ax ) + rx, static_cast< int >( ay ) ) );
	const double wv = lumaOf( at( out, r.width, static_cast< int >( ax ) - rx, static_cast< int >( ay ) ) );
	const double n = lumaOf( at( out, r.width, static_cast< int >( ax ), static_cast< int >( ay ) + rx ) );
	const double mid = lumaOf( at( out, r.width, static_cast< int >( ax ), static_cast< int >( ay ) ) );
	Check( std::fabs( e - wv ) < 0.01 && std::fabs( e - n ) < 0.01 && mid > e && e > corner,
	       fmt( "round and brightest in the middle: %.3f at the centre, %.3f / %.3f / %.3f a radius out, %.3f in the corner", mid, e, wv, n, corner ) );
	//The room: 3 % of the light, in linear light, of a 0.2 grey -- which
	//lands on sRGB's linear toe. The corner is 7.6 radii out, where the beam
	//is e^-58; the slack is the half-float framed picture's half ULP at 0.2.
	auto toLinear = []( double v ) { return v <= 0.04045 ? v / 12.92 : std::pow( ( v + 0.055 ) / 1.055, 2.4 ); };
	auto toCode   = []( double l ) { return l <= 0.0031308 ? 12.92 * l : 1.055 * std::pow( l, 1.0 / 2.4 ) - 0.055; };
	auto room            = [ & ]( double v ) { return toCode( 0.03 * toLinear( v ) ); };
	const double ambient = room( 0.2 );
	//0.2 is held in [1/8, 1/4), where a half float's ULP is 2^-13.
	const double slack = std::fabs( room( 0.2001 ) - room( 0.1999 ) ) / 0.0002 * 0.5 * std::ldexp( 1.0, -13 ) + 1e-6;
	Check( std::fabs( corner - ambient ) <= slack,
	       fmt( "outside the beam the room keeps 3%% of its light: %.5f, expected %.5f", corner, ambient ) );

	//The sweep: Torch Angle 90 travels up the frame and keeps its x; one
	//pass takes Torch Sweep's seconds, so it is back where it was after it.
	auto sweeper = makeRig( r, with( kQuiet, { "Torch=1", "Torch Sweep=1", "Torch Angle=0.25", setting( "Torch X", aimX ), setting( "Torch Y", 0.5 ),
	                                         setting( "Torch Size", 0.2 ) } ),
	                        grey, perturb.hooks );
	if( !sweeper )
		return 1;
	const double period = TorchSweepSecondsFromParam( 1.0f );// 0.5 s: 30 frames
	std::vector< std::pair< double, double > > path;
	for( int f = 0; f < 31; ++f )
	{
		if( !sweeper->Render( 1 ) )
			return 1;
		double sx = -1, sy = -1;
		const Floats o = sweeper->Output();
		if( centroid( o, r.width, 0, 0, r.width, r.height, lumaOf( at( o, r.width, 0, 0 ) ) + 0.01, sx, sy ) )
			path.emplace_back( sx, sy );
		else
			path.emplace_back( -1.0, -1.0 );
	}
	const int half = static_cast< int >( std::lround( period * 60.0 / 2.0 ) );
	const auto& midway = path[ static_cast< size_t >( half ) ];
	Check( std::fabs( midway.first - aimX * r.width ) <= 1.0 && std::fabs( midway.second - 0.5 * r.height ) <= 1.0,
	       fmt( "half a sweep in (frame %d) it crosses its aim: %.1f, %.1f px", half, midway.first, midway.second ) );
	const auto& a = path[ static_cast< size_t >( half - 3 ) ];
	const auto& b = path[ static_cast< size_t >( half + 3 ) ];
	Check( b.second > a.second + 2.0 && std::fabs( b.first - a.first ) <= 0.5, fmt( "Torch Angle 90 sweeps up: y %.1f -> %.1f px, x %.1f -> %.1f", a.second, b.second, a.first, b.first ) );
	return g_failures - before;
}

//===========================================================================
// --aura: a glow round the subject and not on it, coloured, falling away
// outwards as a blurred edge does, over the distance Aura Size gives.
//===========================================================================
int runAura( const Raster& r, const Perturb& perturb )
{
	const int before = g_failures;
	const int w = r.width, h = r.height;
	//A half plane: white to the left of the middle, black to the right.
	Floats half = flatCard( w, h, 0, 0, 0 );
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w / 2; ++x )
		{
			float* o = &half[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
			o[ 0 ] = o[ 1 ] = o[ 2 ] = 1.0f;
		}
	for( const double size : { 0.025, 0.05 } )
	{
		auto rig = makeRig( r, with( kQuiet, { "Aura=0.5", "Corona=0", "Aura Colour=Blue", setting( "Aura Size", ParamFromAuraSize( size ) ) } ), half,
		                    perturb.hooks );
		if( !rig || !rig->Render( 3 ) )
			return 1;
		const Floats out = rig->Output();
		//Blue x px past the edge, between pixel centres.
		auto blueAt = [ & ]( double x ) {
			const double p = w / 2 + x - 0.5;
			const int i    = std::clamp( static_cast< int >( std::floor( p ) ), w / 2, w - 2 );
			const double f = std::clamp( p - i, 0.0, 1.0 );
			return ( 1.0 - f ) * at( out, w, i, h / 2 )[ 2 ] + f * at( out, w, i + 1, h / 2 )[ 2 ];
		};
		const float* first = at( out, w, w / 2, h / 2 );
		if( size == 0.025 )
			Check( first[ 2 ] > 0.15 && first[ 2 ] > 1.5 * first[ 0 ], fmt( "just past the edge it glows, blue: b %.3f, r %.3f", first[ 2 ], first[ 0 ] ) );
		//Outside, the glow is the subject's mask blurred by sigma = Aura Size
		//frame heights: 0.5 erfc( x / ( sigma sqrt 2 ) ), times a constant,
		//so its fall from one sigma out to two is a number. The mask is made
		//at a quarter of the raster (a 4 px box) and read back bilinearly (a
		//4 px tent), which adds 4 px^2 of variance; the slack is 15 % of the
		//ratio, against the 3.6x a sigma off by two gives.
		const double sigma    = size * h;
		const double spread   = std::sqrt( sigma * sigma + 4.0 );
		const double expected = std::erfc( 2.0 * sigma / ( spread * std::sqrt( 2.0 ) ) ) / std::erfc( sigma / ( spread * std::sqrt( 2.0 ) ) );
		const double ratio    = blueAt( 2.0 * sigma ) / blueAt( sigma );
		Check( std::fabs( ratio - expected ) <= 0.15 * expected,
		       fmt( "Aura Size %.3f (sigma %.1f px): two sigma out it is %.3f of one sigma out; a blurred edge gives %.3f", size, sigma, ratio, expected ) );
		const double far = blueAt( std::min( 6.0 * sigma, w / 2 - 2.0 ) );
		Check( far < 0.01 * blueAt( 0.5 ), fmt( "and six sigma out it has gone: %.4f", far ) );
	}
	//Silhouette 1 on the Bright key: the subject is a black mass, with none
	//of the glow it keeps when lit, and the glow still falls round it.
	auto dark = makeRig( r, with( kQuiet, { "Aura=0.5", "Corona=0", "Aura Colour=Blue", "Silhouette=1" } ), half, perturb.hooks );
	if( !dark || !dark->Render( 3 ) )
		return 1;
	const Floats shape = dark->Output();
	const float* inner = at( shape, w, w / 4, h / 2 );
	const float* past  = at( shape, w, w / 2, h / 2 );
	const float most   = std::max( { inner[ 0 ], inner[ 1 ], inner[ 2 ] } );
	Check( most < 0.002f && past[ 2 ] > 0.15, fmt( "Silhouette 1: the subject is black (%.4f at most) and the glow still falls past it (b %.3f)", most, past[ 2 ] ) );
	return g_failures - before;
}

//===========================================================================
// --lettering: the capitals are Caption Size tall and centred on Caption Y;
// the reveal runs left to right; Show 0 draws nothing.
//===========================================================================
int runCaption( const Raster& r, const Perturb& perturb )
{
	const int before = g_failures;
	const double size = 0.1;
	const Floats black = flatCard( r.width, r.height, 0, 0, 0 );
	auto rig = makeRig( r, with( kQuiet, { "Caption=HHHHHHHHHH", "Caption Show=1", "Caption Glow=0", "Caption Spacing=0.5",
	                                     setting( "Caption Size", ParamFromCaptionSize( size ) ), "Caption Y=0.55" } ),
	                    black, perturb.hooks );
	if( !rig || !rig->Render( 2 ) )
		return 1;
	Floats out = rig->Output();
	int top = -1, bottom = r.height;
	for( int y = 0; y < r.height; ++y )
		for( int x = 0; x < r.width; ++x )
			if( lumaOf( at( out, r.width, x, y ) ) > 0.45 )
			{
				top    = std::max( top, y );
				bottom = std::min( bottom, y );
			}
	const double capHeight = top - bottom + 1;
	const double centre    = 0.5 * ( top + bottom + 1 );
	Check( std::fabs( capHeight - size * r.height ) <= 1.0, fmt( "an H is %.0f px tall; Caption Size says %.1f", capHeight, size * r.height ) );
	Check( std::fabs( centre - 0.55 * r.height ) <= 1.0, fmt( "its capitals are centred %.1f px up; Caption Y says %.1f", centre, 0.55 * r.height ) );
	//Across: a Title is centred on its advances, and an H's side bearings are
	//equal, so the ink of a row of them is centred on the frame -- to the
	//pixel the outermost stems land in.
	double inkX = 0.0, ink = 0.0;
	for( int y = 0; y < r.height; ++y )
		for( int x = 0; x < r.width; ++x )
		{
			const double v = lumaOf( at( out, r.width, x, y ) );
			inkX += v * ( x + 0.5 );
			ink += v;
		}
	Check( ink > 0.0 && std::fabs( inkX / ink - 0.5 * r.width ) <= 1.0,
	       fmt( "a Title's ink is centred across: %.2f px, the frame's middle %.1f", ink > 0.0 ? inkX / ink : -1.0, 0.5 * r.width ) );

	//Half shown: of ten letters with a three-letter ramp, the first four are
	//in, the last three are not.
	rig->Set( PT_CAPTION_SHOW, 0.5f );
	if( !rig->Render( 1 ) )
		return 1;
	out = rig->Output();
	const CaptionLayout layout = rig->plugin.LastCaption();
	auto stem = [ & ]( int i ) {
		const auto& placed = layout.glyphs;
		if( i >= static_cast< int >( placed.size() ) )
			return 0.0;
		//The letter's middle row, across its cell: its brightest pixel.
		double best = 0.0;
		const int row = static_cast< int >( 0.55 * r.height );
		for( int x = static_cast< int >( placed[ static_cast< size_t >( i ) ].left ); x < placed[ static_cast< size_t >( i ) ].left + font::kCellWidth * layout.scale && x < r.width; ++x )
			if( x >= 0 )
				best = std::max( best, lumaOf( at( out, r.width, x, row ) ) );
		return best;
	};
	Check( layout.glyphs.size() == 7 && stem( 0 ) > 0.9 && stem( 3 ) > 0.9 && stem( 6 ) < stem( 4 ),
	       fmt( "half shown: %zu of 10 letters drawn, the first at %.2f, the fourth %.2f, fading to %.2f", layout.glyphs.size(), stem( 0 ), stem( 3 ),
	            stem( 6 ) ) );
	rig->Set( PT_CAPTION_SHOW, 0.0f );
	if( !rig->Render( 1 ) )
		return 1;
	out = rig->Output();
	size_t lit = 0;
	for( size_t i = 0; i < out.size(); i += 4 )
		lit += out[ i ] != 0.0f;
	Check( lit == 0, fmt( "Show 0 draws nothing: %zu pixels lit", lit ) );
	return g_failures - before;
}

//===========================================================================
// --freeze: a press holds what was on screen, drains it to grey over Freeze
// Fade, flashes, pushes in, and lets go after Freeze Hold -- or, latched,
// at the next press.
//===========================================================================
int runFreeze( const Raster& r, const Perturb& perturb )
{
	const int before = g_failures;
	const int w = r.width, h = r.height;
	auto moving = [ & ]( int f ) {
		//A colour that changes every frame and a dot that moves, so a frozen
		//frame cannot be mistaken for the live one; and green ramped across,
		//so the frame's lumas run from dark to light. A flat frame sits near
		//0.5, where the grey's smoothstep is the identity, and checks nothing.
		Floats p = flatCard( w, h, static_cast< float >( codeOf( f ) ), 0.6f, 0.2f );
		for( int y = 0; y < h; ++y )
			for( int x = 0; x < w; ++x )
				p[ ( static_cast< size_t >( y ) * w + x ) * 4 + 1 ] = static_cast< float >( ( x + 0.5 ) / w );
		const int x = ( f * 3 ) % w;
		for( int y = h / 2 - 2; y < h / 2 + 2; ++y )
			for( int xx = x; xx < x + 4 && xx < w; ++xx )
			{
				float* o = &p[ ( static_cast< size_t >( y ) * w + xx ) * 4 ];
				o[ 0 ] = o[ 1 ] = o[ 2 ] = 1.0f;
			}
		return p;
	};
	const float holdParam = 0.4f;
	const double hold     = FreezeHoldFromParam( holdParam );
	auto rig = makeRig( r, with( kQuiet, { "Freeze Fade=0", "Freeze Flash=0", "Freeze Push=0", setting( "Freeze Hold", holdParam ) } ), moving( 0 ),
	                    perturb.hooks );
	if( !rig )
		return 1;
	rig->beforeFrame = [ & ]( int f ) { rig->Upload( moving( f ) ); };
	if( !rig->Render( 30 ) )
		return 1;
	const Floats before29 = rig->Output();// what was on screen when Freeze was pressed
	if( !rig->Press( PT_FREEZE ) )
		return 1;
	//Held: every frame of the hold is the last frame before the press, in grey.
	int moved = 0, coloured = 0, unlike = 0;
	Floats first;
	const int holdFrames = static_cast< int >( std::ceil( hold * 60.0 - 1e-9 ) );
	for( int k = 1; k < holdFrames; ++k )
	{
		if( !rig->Render( 1 ) )
			return 1;
		const Floats out = rig->Output();
		if( first.empty() )
			first = out;
		for( size_t i = 0; i < out.size(); i += 4 )
		{
			moved += out[ i ] != first[ i ];
			coloured += std::fabs( out[ i ] - out[ i + 2 ] ) > kHalfUlp;
			//Grey: 60 % of the way to smoothstep of the luma of the frozen frame.
			double y = 0.2126 * before29[ i ] + 0.7152 * before29[ i + 1 ] + 0.0722 * before29[ i + 2 ];
			y        = y + 0.6 * ( y * y * ( 3.0 - 2.0 * y ) - y );
			unlike += std::fabs( out[ i + 1 ] - y ) > 2.0 * kHalfUlp;
		}
	}
	Check( moved == 0 && unlike == 0, fmt( "for %d frames it holds the frame before the press, in grey (%d values moved, %d unlike it)", holdFrames - 1, moved, unlike ) );
	Check( coloured == 0, fmt( "with Freeze Fade 0 it is grey at once (%d pixels with colour)", coloured ) );
	//Let go after Freeze Hold, within the frame it lands on.
	int released = -1;
	for( int k = holdFrames; k < holdFrames + 4; ++k )
	{
		if( !rig->Render( 1 ) )
			return 1;
		if( released < 0 && !rig->plugin.LastFrame().frozen )
			released = k;
	}
	Check( released >= 0 && std::abs( released - holdFrames ) <= 1, fmt( "Freeze Hold %.2f s: let go %d frames after the press (%.1f)", hold, released, hold * 60.0 ) );

	//A second press inside the hold hits again on the clip as it is then,
	//not on the freeze already on screen: pressed on frames 30 and 51, it
	//holds what a single press on frame 52 holds (the frame before it).
	const std::vector< std::string > held = with( kQuiet, { "Freeze Fade=0", "Freeze Flash=0", "Freeze Push=0", setting( "Freeze Hold", holdParam ) } );
	auto twice = makeRig( r, held, moving( 0 ), perturb.hooks );
	auto once  = makeRig( r, held, moving( 0 ), perturb.hooks );
	if( !twice || !once )
		return 1;
	twice->beforeFrame = [ & ]( int f ) { twice->Upload( moving( f ) ); };
	once->beforeFrame  = [ & ]( int f ) { once->Upload( moving( f ) ); };
	if( !twice->Render( 30 ) || !twice->Press( PT_FREEZE ) || !twice->Render( 20 ) || !twice->Press( PT_FREEZE ) || !twice->Render( 5 ) )
		return 1;
	if( !once->Render( 52 ) || !once->Press( PT_FREEZE ) || !once->Render( 5 ) )
		return 1;
	const Floats again = twice->Output(), fresh = once->Output();
	int rehitUnlike = 0, rehitStale = 0;
	for( size_t i = 0; i < again.size(); ++i )
	{
		rehitUnlike += std::fabs( again[ i ] - fresh[ i ] ) > kHalfUlp;
		rehitStale += i < first.size() && std::fabs( again[ i ] - first[ i ] ) > kHalfUlp;
	}
	Check( twice->plugin.LastFrame().frozen && rehitUnlike == 0 && rehitStale > 0,
	       fmt( "a second press in the hold freezes the clip as it is then (%d values unlike a fresh press there)", rehitUnlike ) );

	//The flash: white on the press, gone as e^( -t / 0.08 s ); the fade to
	//grey is linear over Freeze Fade.
	auto flashing = makeRig( r, with( kQuiet, { "Freeze Fade=0.4", "Freeze Flash=1", "Freeze Push=0", "Freeze Hold=1" } ), moving( 0 ), perturb.hooks );
	if( !flashing )
		return 1;
	if( !flashing->Render( 10 ) || !flashing->Press( PT_FREEZE ) )
		return 1;
	const double atPress = meanLuma( flashing->Output(), w, h );
	if( !flashing->Render( 6 ) )
		return 1;
	const FrameState s = flashing->plugin.LastFrame();
	Check( atPress > 0.999 && std::fabs( s.flash - std::exp( -0.1 / 0.08 ) ) < 1e-6 && std::fabs( s.grey - 0.1 / FreezeFadeFromParam( 0.4f ) ) < 1e-6,
	       fmt( "the flash is white on the press (%.4f) and %.3f after 0.1 s; the grey %.3f of the way", atPress, s.flash, s.grey ) );
	//Latched: still frozen five seconds on, and the next press lets go.
	//(Freeze Push is measured below, on its own rig.)
	if( !flashing->Render( 300 ) )
		return 1;
	const bool stillFrozen = flashing->plugin.LastFrame().frozen;
	if( !flashing->Press( PT_FREEZE ) )
		return 1;
	Check( stillFrozen && !flashing->plugin.LastFrame().frozen, "Freeze Hold at the top latches: frozen 5 s on, let go by the next press" );

	//Freeze Push: the frozen frame pushes in about the centre, settling at
	//1 + Freeze Push x 20 %. Two dots either side of the centre, three
	//seconds in (the settle's e^( -3 / 0.35 ) is 2e-4 of the way off).
	const double d = 0.15 * h;
	auto pushing   = makeRig( r, with( kQuiet, { "Freeze Fade=0", "Freeze Flash=0", "Freeze Push=1", "Freeze Hold=1" } ),
	                          dots( w, h, { { 0.5 * w - d, 0.5 * h }, { 0.5 * w + d, 0.5 * h } }, 1.5 ), perturb.hooks );
	if( !pushing || !pushing->Render( 5 ) || !pushing->Press( PT_FREEZE ) || !pushing->Render( 180 ) )
		return 1;
	const Floats pushed = pushing->Output();
	double lx, ly, rx, ry;
	const bool both = centroid( pushed, w, 0, 0, w / 2, h, 0.02, lx, ly ) && centroid( pushed, w, w / 2, 0, w, h, 0.02, rx, ry );
	const double push = ( rx - lx ) / ( 2.0 * d );
	const double want = 1.0 + FreezePushFromParam( 1.0f );
	Check( both && std::fabs( push - want ) <= 0.5 / ( 2.0 * d ) * want, fmt( "Freeze Push 1: pushed in x%.4f, expected x%.4f", push, want ) );
	return g_failures - before;
}

//===========================================================================
// --resize: a raster change under a running instance starts clean from the
// new frame -- no black frame, no smear of nothing.
//===========================================================================
int runResize( const Raster& r, const Perturb& perturb )
{
	const int before = g_failures;
	const std::vector< std::string > settings = with( kQuiet, { "Stutter=1", "Trail=0.8" } );
	auto rig = makeRig( r, settings, buildCard( r.width, r.height, 0.0 ), perturb.hooks );
	if( !rig || !rig->Render( 30 ) )
		return 1;
	const int w2 = r.width * 3 / 4, h2 = r.height * 3 / 4;
	const Floats card2 = buildCard( w2, h2, 0.0 );
	if( !rig->Resize( w2, h2, &card2 ) || !rig->Render( 1 ) )
		return 1;
	const double got      = meanLuma( rig->Output(), w2, h2 );
	const double expected = meanLuma( card2, w2, h2 );
	Check( std::fabs( got - expected ) <= kHalfUlp, fmt( "resized mid-run, the first frame is the new clip (luma %.4f, clip %.4f)", got, expected ) );
	return g_failures - before;
}

//===========================================================================
// --transparency: where the clip is transparent it stays so, except where the
// aura's light and the caption fall.
//===========================================================================
int runAlpha( const Raster& r, const Perturb& perturb )
{
	const int before = g_failures;
	Floats half = flatCard( r.width, r.height, 0.9f, 0.9f, 0.9f );
	for( int y = 0; y < r.height; ++y )
		for( int x = 0; x < r.width / 2; ++x )
			for( int c = 0; c < 4; ++c )
				half[ ( static_cast< size_t >( y ) * r.width + x ) * 4 + c ] = 0.0f;
	auto plain = makeRig( r, {}, half, perturb.hooks );// the defaults: graded, grained, flickering
	if( !plain || !plain->Render( 20 ) )
		return 1;
	Floats out = plain->Output();
	const double leftA  = at( out, r.width, r.width / 8, r.height / 2 )[ 3 ];
	const double rightA = at( out, r.width, r.width * 7 / 8, r.height / 2 )[ 3 ];
	Check( leftA == 0.0 && rightA > 0.999, fmt( "the defaults keep the clip's alpha: %.3f where it was clear, %.3f where it was solid", leftA, rightA ) );
	auto glowing = makeRig( r, { "Aura=0.6", "Aura Key=Alpha", "Corona=0" }, half, perturb.hooks );
	if( !glowing || !glowing->Render( 3 ) )
		return 1;
	out = glowing->Output();
	const int edge = r.width / 2 - static_cast< int >( 0.01 * r.height ) - 1;
	Check( at( out, r.width, edge, r.height / 2 )[ 3 ] > 0.1 && at( out, r.width, 2, r.height / 2 )[ 3 ] < 0.01,
	       fmt( "the aura's light is opaque where it falls on clear: alpha %.3f just off the edge, %.4f far off", at( out, r.width, edge, r.height / 2 )[ 3 ],
	            at( out, r.width, 2, r.height / 2 )[ 3 ] ) );
	return g_failures - before;
}

//===========================================================================
// --state: the host's GL state is as it was, whichever passes ran.
//===========================================================================
int runState( const Raster& r, const Perturb& )
{
	const int before = g_failures;
	auto rig = makeRig( r, { "Aura=0.5", "Caption Show=1", "Torch=0.5" }, buildCard( r.width, r.height, 0.0 ), 0 );
	if( !rig )
		return 1;
	if( !rig->Render( 1 ) )
		return 1;
	glBindFramebuffer( GL_FRAMEBUFFER, rig->outputFBO );
	glViewport( 3, 5, r.width - 7, r.height - 9 );
	glEnable( GL_BLEND );
	glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA );
	glClearColor( 0.1f, 0.2f, 0.3f, 0.4f );
	glActiveTexture( GL_TEXTURE5 );
	rig->plugin.SetTime( 1.0 );
	rig->plugin.ProcessOpenGL( &rig->process );
	GLint viewport[ 4 ], src = 0, dst = 0, active = 0, bound = 0;
	GLfloat clear[ 4 ];
	glGetIntegerv( GL_VIEWPORT, viewport );
	glGetIntegerv( GL_BLEND_SRC_RGB, &src );
	glGetIntegerv( GL_BLEND_DST_RGB, &dst );
	glGetIntegerv( GL_ACTIVE_TEXTURE, &active );
	glGetFloatv( GL_COLOR_CLEAR_VALUE, clear );
	bool unitsClear = true;
	for( int unit = 0; unit < 6; ++unit )
	{
		glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + unit ) );
		glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound );
		unitsClear = unitsClear && bound == 0;
	}
	glActiveTexture( GL_TEXTURE0 );
	Check( viewport[ 0 ] == 3 && viewport[ 1 ] == 5 && viewport[ 2 ] == r.width - 7 && viewport[ 3 ] == r.height - 9, "the host's viewport is put back" );
	Check( glIsEnabled( GL_BLEND ) && src == GL_SRC_ALPHA && dst == GL_ONE, "its blending is put back" );
	Check( clear[ 2 ] == 0.3f && active == GL_TEXTURE5, "its clear colour and active texture unit are put back" );
	Check( unitsClear, "no texture is left bound on units 0..5" );
	glDisable( GL_BLEND );
	return g_failures - before;
}

//===========================================================================
// No GL: the names, the mappings and the caption's layout.
//===========================================================================
int runNames()
{
	const int before = g_failures;
	BluebookPlugin plugin;
	std::map< std::string, int > seen;
	int longest = 0;
	for( const NamedParameter& p : listParameters( plugin ) )
	{
		++seen[ p.name ];
		longest = std::max( longest, static_cast< int >( p.name.size() ) );
	}
	int repeats = 0;
	for( const auto& entry : seen )
		repeats += entry.second > 1;
	Check( repeats == 0, fmt( "%zu parameter names, none twice", seen.size() ) );
	Check( longest <= 16, fmt( "the longest name is %d characters (FFGL keeps 16, unterminated)", longest ) );
	for( unsigned int id = 0; id < PT_ABOUT_FIRST; ++id )
		if( InfoOf( id ).kind == Kind::Option && static_cast< int >( plugin.GetNumParamElements( id ) ) != InfoOf( id ).optionCount )
			Check( false, std::string( InfoOf( id ).name ) + ": its element count is not its table's" );
	Check( std::string( plugin.GetTextParameter( PT_CAPTION ) ) == kDefaultCaption, "Caption starts as the default line" );
	Check( plugin.SetTextParameter( PT_ABOUT_FIRST, "x" ) == FF_SUCCESS, "the About line accepts its default (or no host can load the plugin)" );
	return g_failures - before;
}

int runMappings()
{
	const int before = g_failures;
	struct Pair
	{
		const char* name;
		double ( *forward )( float );
		float ( *inverse )( double );
	};
	const Pair pairs[] = {
		{ "Step Rate", StepRateFromParam, ParamFromStepRate },   { "Magnify", MagnifyFromParam, ParamFromMagnify },
		{ "Aura Size", AuraSizeFromParam, ParamFromAuraSize },   { "Torch Size", TorchSizeFromParam, ParamFromTorchSize },
		{ "Caption Size", CaptionSizeFromParam, ParamFromCaptionSize },
	};
	for( const Pair& p : pairs )
	{
		double worst  = 0.0;
		bool monotone = true;
		for( int i = 0; i <= 100; ++i )
		{
			const float v = i / 100.0f;
			worst         = std::max( worst, static_cast< double >( std::fabs( p.inverse( p.forward( v ) ) - v ) ) );
			if( i > 0 )
				monotone = monotone && p.forward( v ) > p.forward( ( i - 1 ) / 100.0f );
		}
		Check( monotone && worst < 1e-5, fmt( "%s rises with its slider and its inverse returns the slider (worst %.1e)", p.name, worst ) );
	}
	Check( TorchSweepSecondsFromParam( 0.0f ) == 0.0 && TorchSweepSecondsFromParam( 1e-6f ) > 19.9, "Torch Sweep 0 stands still; just above it, 20 s a pass" );
	Check( FreezeHoldFromParam( 1.0f ) == 0.0 && std::fabs( FreezeHoldFromParam( 0.0f ) - 0.25 ) < 1e-12, "Freeze Hold's top latches; its bottom is 0.25 s" );
	Check( TrailHalfLifeFromParam( 0.0f ) == 0.0, "Trail 0 is no smear" );
	//The numbers the README gives for each end. A mapping can be monotone,
	//invertible and wrong; these are what an operator was promised.
	struct End
	{
		const char* what;
		double got, want;
	};
	const End ends[] = {
		{ "Step Rate 0 / 1, prints a second", StepRateFromParam( 0.0f ), 1.0 },
		{ "", StepRateFromParam( 1.0f ), 30.0 },
		{ "Magnify 0 / 1, times", MagnifyFromParam( 0.0f ), 1.5 },
		{ "", MagnifyFromParam( 1.0f ), 24.0 },
		{ "Stretch 1, times", StretchFromParam( 1.0f ), 5.0 },
		{ "Weave 1, frame heights", WeaveFromParam( 1.0f ), 0.012 },
		{ "Trail 1, half-life in s", TrailHalfLifeFromParam( 1.0f ), 1.5 },
		{ "Torch Sweep 1, s a pass", TorchSweepSecondsFromParam( 1.0f ), 0.5 },
		{ "Freeze Hold just under 1, s", FreezeHoldFromParam( std::nextafter( 1.0f, 0.0f ) ), 10.0 },
		{ "Freeze Fade 1, s", FreezeFadeFromParam( 1.0f ), 1.5 },
		{ "Freeze Push 1, of the frame", FreezePushFromParam( 1.0f ), 0.2 },
	};
	int wrongEnds = 0;
	for( const End& e : ends )
		if( std::fabs( e.got - e.want ) > 1e-4 * e.want )
		{
			std::printf( "        %s: %.6g, the README says %.6g\n", e.what, e.got, e.want );
			++wrongEnds;
		}
	Check( wrongEnds == 0, fmt( "every range ends where the README says (%d of %zu do not)", wrongEnds, sizeof( ends ) / sizeof( ends[ 0 ] ) ) );
	return g_failures - before;
}

int runLayout()
{
	const int before = g_failures;
	BluebookPlugin plugin;
	plugin.SetFloatParameter( PT_CAPTION_SHOW, 1.0f );
	const CaptionLayout title = plugin.LayOutCaption( 1920, 1080, "TRUTH", 0 );
	const double centre       = 0.5 * ( title.box[ 0 ] + title.box[ 2 ] );
	Check( title.glyphs.size() == 5 && std::fabs( centre - 960.0 ) < 0.5 * font::kCellWidth * title.scale,
	       fmt( "Title: five letters, centred (box centre %.1f px)", centre ) );
	plugin.SetFloatParameter( PT_CAPTION_STYLE, 1.0f );
	const CaptionLayout legend = plugin.LayOutCaption( 1920, 1080, "PHOTO", 41 );
	Check( legend.glyphs.size() == 5 + 10 && legend.box[ 2 ] > 0.94 * 1920 && legend.box[ 2 ] < 0.94 * 1920 + font::kCellWidth * legend.scale,
	       fmt( "Legend: the line and a catalogue number, against 94%% of the width (%zu glyphs, right edge %.0f)", legend.glyphs.size(), legend.box[ 2 ] ) );
	plugin.SetFloatParameter( PT_CAPTION_STYLE, 0.0f );
	//A character the font lacks is one '?', measured as it is drawn: an
	//accent (two bytes), an alien (four) and a stray byte lay out exactly as
	//the ASCII line with a '?' in each place, and the limit counts characters.
	const auto same = [ &plugin ]( const std::string& a, const std::string& b ) {
		const CaptionLayout x = plugin.LayOutCaption( 1920, 1080, a, 0 );
		const CaptionLayout y = plugin.LayOutCaption( 1920, 1080, b, 0 );
		if( x.glyphs.size() != y.glyphs.size() || !std::equal( x.box, x.box + 4, y.box ) )
			return false;
		for( size_t i = 0; i < x.glyphs.size(); ++i )
			if( x.glyphs[ i ].code != y.glyphs[ i ].code || x.glyphs[ i ].left != y.glyphs[ i ].left )
				return false;
		return true;
	};
	std::string accents, asDrawn;
	for( int i = 0; i < kMaxCaption / 2 + 1; ++i )
		accents += "A\xC3\x89";
	for( int i = 0; i < kMaxCaption / 2; ++i )
		asDrawn += "A?";
	Check( same( "CAF\xC3\x89", "CAF?" ) && same( "\xF0\x9F\x91\xBD HERE", "? HERE" ) && same( "A\x89" "B", "A?B" ),
	       "Title: an accent, an alien and a stray byte are each one '?', centred as drawn" );
	Check( same( accents, asDrawn ), fmt( "%d letters, every other one accented, are cut at %d characters, not bytes",
	                                      kMaxCaption + 2, kMaxCaption ) );
	plugin.SetFloatParameter( PT_CAPTION_SHOW, 0.0f );
	Check( plugin.LayOutCaption( 1920, 1080, "TRUTH", 0 ).glyphs.empty(), "Show 0 lays out nothing" );
	return g_failures - before;
}

//===========================================================================
// --bench: ms a frame with everything on.
//===========================================================================
int runBench()
{
	const Raster rasters[] = { { 1280, 720 }, { 1920, 1080 }, { 3840, 2160 } };
	for( const Raster& r : rasters )
	{
		auto rig = makeRig( r, { "Aura=0.6", "Enlarge=0.5", "Stretch=0.4", "Torch=0.6", "Caption Show=1", "Trail=0.3" }, buildCard( r.width, r.height, 0.0 ), 0 );
		if( !rig || !rig->Render( 10 ) )
			return 1;
		glFinish();
		const auto start = std::chrono::steady_clock::now();
		const int frames = 60;
		rig->Render( frames );
		glFinish();
		const double ms = std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() / frames;
		std::printf( "  %4dx%-4d  %.2f ms a frame, everything on\n", r.width, r.height, ms );
	}
	return 0;
}

//===========================================================================
// The table, and --negative.
//===========================================================================
using CheckFn = int ( * )( const Raster&, const Perturb& );

struct CheckEntry
{
	const char* flag;
	CheckFn run;
	Perturb wrong;///< what --negative runs it against
	const char* wrongly;
};

const std::vector< CheckEntry >& glChecks()
{
	static const std::vector< CheckEntry > table = {
		{ "--identity", runIdentity, Perturb { 1 }, "a stage left a hair on; Mix a hair up" },
		{ "--print", runPrint, Perturb { shaders::kHookDissolveSnap, 8.0 / 7.0 }, "a dissolve that snaps, and 8 prints a second for 7" },
		{ "--beat", runBeat, Perturb { 0, 1.1 }, "prints every 33 / 132 frames" },
		{ "--weave", runWeave, weaveModelled( 0.4 ), "a weave held to 40 % of Weave's" },
		{ "--grade", runGrade, gradeModelled( 0.9 ), "Contrast and Vignette at 90 % of their curves" },
		{ "--trail", runTrail, Perturb { 0, 1.0, 1.25 }, "a half-life a quarter longer" },
		{ "--enlarge", runEnlarge, Perturb { 0, 1.0, 1.0, 0.8 }, "a depth of 3 of 5 generations, not 3 of 4" },
		{ "--grain", runGrain, Perturb { shaders::kHookFlatGrain, 1.0, 1.0, 1.0, 0.6 }, "old grain at the live size; variance adding at 0.6 a layer" },
		{ "--stretch", runStretch, Perturb { shaders::kHookSquash }, "the face squashed instead of pulled" },
		{ "--torch", runTorch, Perturb { shaders::kHookTorchOffset }, "the beam a tenth of a frame off its aim" },
		{ "--aura", runAura, Perturb { shaders::kHookGlowInside }, "the glow on the subject, not round it" },
		{ "--lettering", runCaption, Perturb { shaders::kHookCaptionBottom }, "glyphs hung from their cells' bottoms" },
		{ "--freeze", runFreeze, Perturb { shaders::kHookNoGrey }, "a freeze that keeps its colour" },
		{ "--resize", runResize, Perturb {}, nullptr },
		{ "--transparency", runAlpha, Perturb {}, nullptr },
		{ "--state", runState, Perturb {}, nullptr },
	};
	return table;
}

std::vector< Raster > rastersFor( const Request& request, bool explicitSize )
{
	if( explicitSize )
		return { { request.width, request.height } };
	return { { 640, 360 }, { 320, 180 } };
}

int runOneGL( const CheckEntry& entry, const std::vector< Raster >& rasters, const Perturb& perturb )
{
	int failures = 0;
	for( const Raster& r : rasters )
	{
		if( !g_quiet )
			std::printf( "%s at %dx%d%s\n", entry.flag, r.width, r.height, g_software ? " (software)" : "" );
		failures += entry.run( r, perturb );
	}
	return failures;
}

int runNegative( const std::vector< Raster >& rasters )
{
	std::printf( "--negative: every check against a wrong model must FAIL\n" );
	int escaped = 0;
	for( const CheckEntry& entry : glChecks() )
	{
		if( entry.wrongly == nullptr )
			continue;
		const int savedFailures = g_failures, savedChecks = g_checks;
		g_quiet          = true;
		const int caught = runOneGL( entry, rasters, entry.wrong );
		g_quiet          = false;
		g_failures       = savedFailures;
		g_checks         = savedChecks;
		Check( caught > 0, fmt( "%-11s fails against %s (%d checks failed)", entry.flag, entry.wrongly, caught ) );
		escaped += caught == 0;
	}
	return escaped;
}

int runChecks( const std::vector< std::string >& flags, const Request& request )
{
	//--size given on the command line means one raster; main's default is 1280x720.
	const bool explicitSize = !( request.width == 1280 && request.height == 720 );
	const std::vector< Raster > rasters = rastersFor( request, explicitSize );
	for( const std::string& flag : flags )
	{
		if( flag == "--offline" )
		{
			std::printf( "--offline\n" );
			runNames();
			runMappings();
			runLayout();
		}
		else if( flag == "--names" )
			runNames();
		else if( flag == "--mappings" )
			runMappings();
		else if( flag == "--layout" )
			runLayout();
		else if( flag == "--bench" )
			return runBench();
		else if( flag == "--negative" )
			runNegative( rasters );
		else if( flag == "--all" )
		{
			std::printf( "--offline\n" );
			runNames();
			runMappings();
			runLayout();
			for( const CheckEntry& entry : glChecks() )
				runOneGL( entry, rasters, Perturb {} );
		}
		else
		{
			bool found = false;
			for( const CheckEntry& entry : glChecks() )
				if( flag == entry.flag )
				{
					runOneGL( entry, rasters, Perturb {} );
					found = true;
				}
			if( !found )
			{
				std::fprintf( stderr, "bbtest: no check called %s\n", flag.c_str() );
				return 2;
			}
		}
	}
	std::printf( "\n  %d checks, %d failed: %s\n", g_checks, g_failures, g_failures == 0 ? "PASS" : "FAIL" );
	return g_failures == 0 ? 0 : 1;
}
} // namespace
