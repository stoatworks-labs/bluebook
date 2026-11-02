#pragma once

/**
    The caption's typeface: Roboto Condensed Light as a signed distance field,
    baked by tools/make-font.py into FontAtlas.cpp (GENERATED: rerun the script,
    never edit the table).

    The intro set its captions in Helios Condensed Light, a Helvetica clone
    that cannot be shipped; Roboto Condensed (SIL OFL 1.1, fonts/OFL.txt) is
    the nearest open face. One texture holds every printable ASCII glyph, 16
    to a row, each with its pen origin at ( kPenX, kBaseline ) of its cell,
    measured from the cell's TOP-left in atlas pixels at kEm pixels per em.
    The texture's rows run bottom up, as GL's do.

    A texel is 0.5 + d / ( 2 kSpread ), d the distance to the outline in atlas
    pixels, positive inside: 0.5 is the edge, and the field runs kSpread
    pixels either side of it, which is how far the glow can reach.
*/
namespace bluebook::font
{
constexpr int kEm          = 32;
constexpr int kSpread      = 8;
constexpr int kCellWidth   = 44;
constexpr int kCellHeight  = 56;
constexpr int kColumns     = 16;
constexpr int kFirst       = 32;
constexpr int kCount       = 96;///< 32..127; 127 is blank
constexpr int kAtlasWidth  = 704;
constexpr int kAtlasHeight = 336;
constexpr int kBaseline    = 38;///< from the cell's top
constexpr int kPenX        = 8; ///< from the cell's left

/// Cap height in atlas pixels (an H, measured by make-font.py).
constexpr float kCapHeight = 22.75f;

/// Each glyph's advance, in ems. Codes outside 32..126 have none.
extern const float kAdvance[ kCount ];

/// R8, kAtlasWidth x kAtlasHeight, bottom row first.
extern const unsigned char kAtlas[ kAtlasWidth * kAtlasHeight ];

/// The advance of `code` in ems; anything unprintable is a space.
inline float AdvanceOf( int code )
{
	if( code < kFirst || code >= kFirst + kCount - 1 )
		code = ' ';
	return kAdvance[ code - kFirst ];
}

} // namespace bluebook::font
