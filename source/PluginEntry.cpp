#include "Bluebook.h"

/**
    The one registration.

    This file is listed directly in the Bluebook MODULE target, not in
    bluebook_core: `CFFGLPluginInfo` registers itself from a file-scope
    constructor and nothing ever references it by name, so in a STATIC
    archive the linker is entitled to drop the whole translation unit --
    giving a bundle that loads, exports `plugMain`, and reports that it
    contains no plugins. The core stays an OBJECT library for the same reason.

    The name is `SW Bluebook`, eleven characters. The FFGL name field is
    `char[ 16 ]` and is **not** null-terminated, so the host truncates without
    saying so. `oxbow probe` is what reads it back the way a host does.
*/
namespace
{
class BluebookEffect : public bluebook::BluebookPlugin
{
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< BluebookEffect >,// Create method
	"BK01",                         // Plugin unique ID of maximum length 4
	"SW Bluebook",                  // Plugin name
	2,                              // API major version number
	1,                              // API minor version number
	0,                              // Plugin major version number
	1,                              // Plugin minor version number
	FF_EFFECT,                      // Plugin type
	"The look of a 1990s paranormal TV title sequence, and a procedural's freeze-frame hit.\n\nThe clip is step-printed "
	"into stuttering, dissolving stills, enlarged generation by generation until the grain swallows it, pulled long like "
	"a screaming face, wreathed in a flickering glow, swept by a torch, graded cold and grainy, and captioned in thin, "
	"loosely spaced capitals. Freeze stops the picture with a flash and drains it to black and white.",
	"Bluebook FFGL effect"// About
);

extern "C" const char* BluebookBuildStamp()
{
	return "bluebook " BLUEBOOK_VERSION ", built " __DATE__ " " __TIME__;
}
