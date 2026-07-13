#include "Kyty/Core/String.h"

#include "Emulator/Common.h"
#include "Emulator/Kernel/FileSystem.h"
#include "Emulator/Libs/Libs.h"
#include "Emulator/Loader/SymbolDatabase.h"

#ifdef KYTY_EMU_ENABLED

#include <ft2build.h>
#include FT_FREETYPE_H

// PS4 titles link FreeType as a system module. Kyty runs guest code natively in
// the host address space, and FreeType keeps its public structs (FT_FaceRec,
// FT_GlyphSlotRec, FT_Bitmap, ...) binary-compatible across the whole 2.x line,
// so a host FT_Face handed back to the guest can be dereferenced directly. Each
// entry point is a thin pass-through to the host libfreetype; only FT_New_Face
// needs work, to translate the guest mount path (/app0/...) to a real path.

namespace Kyty::Libs {

namespace FreeType {

LIB_VERSION("FreeType", 1, "FreeType", 1, 1);

static KYTY_SYSV_ABI int FreeTypeInit(FT_Library* alibrary)
{
	PRINT_NAME();

	return FT_Init_FreeType(alibrary);
}

static KYTY_SYSV_ABI int FreeTypeNewFace(FT_Library library, const char* filepathname, FT_Long face_index, FT_Face* aface)
{
	PRINT_NAME();

	auto real = LibKernel::FileSystem::GetRealFilename(String::FromUtf8(filepathname));

	printf("\t face  = %s\n", filepathname);
	printf("\t real  = %s\n", real.C_Str());

	return FT_New_Face(library, real.utf8_str().GetData(), face_index, aface);
}

// The per-glyph calls below run thousands of times per frame; no PRINT_NAME.
static KYTY_SYSV_ABI int FreeTypeSetPixelSizes(FT_Face face, FT_UInt pixel_width, FT_UInt pixel_height)
{
	return FT_Set_Pixel_Sizes(face, pixel_width, pixel_height);
}

static KYTY_SYSV_ABI FT_UInt FreeTypeGetCharIndex(FT_Face face, FT_ULong charcode)
{
	return FT_Get_Char_Index(face, charcode);
}

static KYTY_SYSV_ABI int FreeTypeLoadGlyph(FT_Face face, FT_UInt glyph_index, FT_Int32 load_flags)
{
	return FT_Load_Glyph(face, glyph_index, load_flags);
}

static KYTY_SYSV_ABI int FreeTypeRenderGlyph(FT_GlyphSlot slot, FT_Render_Mode render_mode)
{
	return FT_Render_Glyph(slot, render_mode);
}

LIB_DEFINE(InitFreeType_1)
{
	LIB_FUNC("GNsiLzm4SH8", FreeTypeInit);
	LIB_FUNC("j-uMQE+unFY", FreeTypeNewFace);
	LIB_FUNC("lJxMIwj81SQ", FreeTypeSetPixelSizes);
	LIB_FUNC("obnSzeb-KXk", FreeTypeGetCharIndex);
	LIB_FUNC("M156RuYoUbU", FreeTypeLoadGlyph);
	LIB_FUNC("TCRmigbq7lc", FreeTypeRenderGlyph);
}

} // namespace FreeType

} // namespace Kyty::Libs

#endif // KYTY_EMU_ENABLED
