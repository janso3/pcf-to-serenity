#include "PCFFile.h"

#include <AK/Types.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/File.h>
#include <LibGfx/Font/BitmapFont.h>
#include <LibGfx/Font/FontStyleMapping.h>
#include <LibMain/Main.h>

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
	StringView path;

    Core::ArgsParser args_parser;
    args_parser.add_positional_argument(path, "Path to PCF file", "path", Core::ArgsParser::Required::Yes);
    if (!args_parser.parse(arguments))
		return -1;

    auto file = TRY(Core::File::open(path, Core::File::OpenMode::Read));
    auto buffer = TRY(file->read_until_eof());

	auto pcf = TRY(PCFFile::create(buffer));

	auto glyph_size = pcf->glyph_size();
	auto bitmap_font = TRY(Gfx::BitmapFont::try_create(glyph_size.height(), glyph_size.width(), pcf->is_fixed_width(), pcf->glyph_count()));
	bitmap_font->set_family(pcf->family());
	bitmap_font->set_name(pcf->name());
	bitmap_font->set_presentation_size(pcf->pixel_size());
	bitmap_font->set_glyph_spacing(0);
	bitmap_font->set_weight(pcf->weight());
	bitmap_font->set_slope(pcf->slope());
	bitmap_font->set_baseline(pcf->baseline());

	// FIXME: Make this less naive.
	for (int16_t i = 0; i < 5000; ++i) {
		auto maybe_glyph = pcf->glyph_index_for(i);
		if (!maybe_glyph.has_value())
			continue;

		auto pcf_index = maybe_glyph.value();
		
		bitmap_font->set_glyph_width(i, pcf->glyph_width(pcf_index));
		auto bitmap = bitmap_font->raw_glyph(i).glyph_bitmap();
		TRY(pcf->draw_glyph(pcf_index, bitmap));
	}

	auto filename = TRY(pcf->construct_filename());
	dbgln("Writing {}", filename);

	auto set = TRY(bitmap_font->masked_character_set());
	TRY(set->write_to_file(filename));

    return 0;
}
