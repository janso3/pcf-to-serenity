#include "PCFFile.h"
#include <AK/Math.h>
#include <LibGfx/Font/BitmapFont.h>
#include <LibGfx/Font/FontStyleMapping.h>

#define PCF_PROPERTIES          (1<<0)
#define PCF_ACCELERATORS        (1<<1)
#define PCF_METRICS             (1<<2)
#define PCF_BITMAPS             (1<<3)
#define PCF_INK_METRICS         (1<<4)
#define PCF_BDF_ENCODINGS       (1<<5)
#define PCF_SWIDTHS             (1<<6)
#define PCF_GLYPH_NAMES         (1<<7)
#define PCF_BDF_ACCELERATORS    (1<<8)

#define PCF_DEFAULT_FORMAT      0x00000000
#define PCF_INKBOUNDS           0x00000200
#define PCF_ACCEL_W_INKBOUNDS   0x00000100
#define PCF_COMPRESSED_METRICS  0x00000100

PCFFile::PCFFile(ReadonlyBytes bytes)
	: m_stream(bytes)
{
}

ErrorOr<NonnullRefPtr<PCFFile>> PCFFile::create(ReadonlyBytes bytes)
{
	auto pcf = adopt_ref(*new PCFFile(bytes));

	Header header;
	TRY(pcf->m_stream.read_some(Bytes { header.magic, sizeof(header.magic) }));
	if (header.magic[0] != '\1' || header.magic[1] != 'f' || header.magic[2] != 'c' || header.magic[3] != 'p')
		return Error::from_string_literal("Mismatching magic value");
	
	header.table_count = TRY(pcf->m_stream.read_value<LittleEndian<i32>>());
	VERIFY(header.table_count > 0);

	for (i32 i = 0; i < header.table_count; ++i) {
		Header::TOCEntry table;
		table.type = TRY(pcf->m_stream.read_value<LittleEndian<i32>>());
		table.format = TRY(pcf->m_stream.read_value<LittleEndian<i32>>());
		table.size = TRY(pcf->m_stream.read_value<LittleEndian<i32>>());
		table.offset = TRY(pcf->m_stream.read_value<LittleEndian<i32>>());
		TRY(pcf->m_tables.try_append(table));
	}

	TRY(pcf->populate_tables());
	TRY(pcf->convert_glyphs());

	return pcf;
}

ErrorOr<DeprecatedString> PCFFile::construct_filename() const
{
	StringBuilder builder;

	TRY(builder.try_append(family()));

	auto wei = weight();
	auto sl = slope();
	
	// Only name the weight if it's either non-regular, or
	// the slope is non-regular and thus omitted.
	// This results in names like TerminusRegular16, TerminusBoldItalic24,
	// but _not_ TerminusRegularRegular16.
	if (sl == 0 || wei != 400)
		TRY(builder.try_append(Gfx::weight_to_name(weight())));

	if (sl != 0)
		TRY(builder.try_append(Gfx::slope_to_name(sl)));

	if (m_properties.contains("PIXEL_SIZE"sv))	
		TRY(builder.try_append(DeprecatedString::formatted("{}", m_properties.get("PIXEL_SIZE"sv).value().get<i32>())));

	TRY(builder.try_append(".font"sv));

	return builder.to_deprecated_string();
}

Optional<u16> PCFFile::glyph_index_for(u16 code_point) const
{
	i16 table_index = 0;
	if (m_encoding.min_byte1 == 0 && m_encoding.max_byte1 == 0) {
		table_index = code_point - m_encoding.min_char_or_byte2;
	} else {
		u8 hi = code_point >> 8;
		u8 lo = code_point & 0xff;
		table_index = (hi - m_encoding.min_byte1) 
					  * (m_encoding.max_char_or_byte2 - m_encoding.min_char_or_byte2 + 1)
					  + lo - m_encoding.min_char_or_byte2;
	}

	if (table_index < 0)
		return {};

	if (static_cast<size_t>(table_index) >= m_encoding.indices.size())
		return {};

	auto index = m_encoding.indices[table_index];
	if (index < 0)
		return {};

	return static_cast<u16>(index);
}

ErrorOr<void> PCFFile::draw_glyph(u16 index, Gfx::GlyphBitmap& bitmap) const
{
	auto size = glyph_size();
	auto& glyph = m_glyphs.at(index);

	for (int y = 0; y < size.height(); ++y) {
		for (int x = 0; x < glyph.width; ++x) {
			u8 pixel = glyph.data[y * glyph.width + x];
			bitmap.set_bit_at(x, y, pixel != 0);
		}
	}
	return {};
}

u8 PCFFile::baseline() const
{
	return m_acc.font_ascent - 1;
}

DeprecatedString PCFFile::family() const
{
	StringBuilder builder;
	if (m_properties.contains("FAMILY_NAME"sv))	
		builder.append(m_properties.get("FAMILY_NAME"sv).value().get<DeprecatedString>());
	else
		builder.append("Unknown"sv);
	return builder.to_deprecated_string();
}

DeprecatedString PCFFile::name() const
{
	StringBuilder builder;
	builder.append(family());
	builder.append(" "sv);
	builder.append(weight_name());

	return builder.to_deprecated_string();
}

DeprecatedString PCFFile::weight_name() const
{
	StringBuilder builder;
	if (m_properties.contains("WEIGHT_NAME"sv))
		builder.append(m_properties.get("WEIGHT_NAME"sv).value().get<DeprecatedString>());
	else
		builder.append("Regular"sv);

	return builder.to_deprecated_string();
}

i32 PCFFile::weight() const
{
	// HACK: Use some common weight names because some fonts don't include any other weight info.
	auto name = weight_name();
	if (name.equals_ignoring_ascii_case("thin"sv))
		return Gfx::name_to_weight("Thin"sv);
	if (name.equals_ignoring_ascii_case("light"sv))
		return Gfx::name_to_weight("Light"sv);
	if (name.equals_ignoring_ascii_case("medium"sv) || name.equals_ignoring_ascii_case("regular"sv))
		return Gfx::name_to_weight("Regular"sv);
	if (name.equals_ignoring_ascii_case("bold"sv))
		return Gfx::name_to_weight("Bold"sv);

	i32 value = 0;
	if (m_properties.contains("WEIGHT"sv)) {
		value = m_properties.get("WEIGHT"sv).value().get<i32>();
	} else {
		// FIXME: This can be calulated: https://www.x.org/releases/X11R7.6/doc/xorg-docs/specs/XLFD/xlfd.html#weight
		TODO();
	}

	// Convert X11 weight to Serenity weight.
	TODO();

	return value;
}

i32 PCFFile::slope() const
{
	if (m_properties.contains("SLANT"sv)) {
		auto slant = m_properties.get("SLANT"sv).value().get<DeprecatedString>();
		if (slant == "I"sv)
			return Gfx::name_to_slope("Italic"sv);
		if (slant == "O"sv)
			return Gfx::name_to_slope("Oblique"sv);
		// FIXME: Reverse Italic, Reverse Oblique, Other
	}
	return Gfx::name_to_slope("Regular"sv);
}

i32 PCFFile::pixel_size() const
{
	if (m_properties.contains("PIXEL_SIZE"sv))	
		return m_properties.get("PIXEL_SIZE"sv).value().get<i32>();
	return 0;
}

i32 PCFFile::x_height() const
{
	if (m_properties.contains("X_HEIGHT"sv))	
		return m_properties.get("X_HEIGHT"sv).value().get<i32>();
	TODO();
	return 0;
}

Gfx::IntSize PCFFile::glyph_size() const
{
	return { m_max_width, m_max_ascent + m_max_descent };
}

ErrorOr<void> PCFFile::populate_tables()
{
	for (auto& table : m_tables) {
		TRY(m_stream.seek(table.offset));
		auto format = TRY(m_stream.read_value<LittleEndian<i32>>());

		switch(table.type) {
		case PCF_PROPERTIES:
		{
			PropertiesTable table;
			table.nprops = TRY(read<i32>(format));
			VERIFY(table.nprops >= 0);

			Vector<PropertiesTable::Props> props;
			TRY(props.try_resize(table.nprops));

			for (i32 i = 0; i < table.nprops; ++i) {
				auto& prop = props.at(i);
				prop.name_offset = TRY(read<i32>(format));
				prop.is_string_prop = TRY(read<i8>(format)); 
				prop.value = TRY(read<i32>(format));
			}

			// Skip padding.
			TRY(m_stream.seek((table.nprops&3)==0?0:(4-(table.nprops&3)), SeekMode::FromCurrentPosition));
			auto string_size = TRY(read<i32>(format));
			auto strings = TRY(ByteBuffer::create_uninitialized(string_size));
			TRY(m_stream.read_some(strings.bytes()));

			for (i32 prop_index = 0; prop_index < table.nprops; ++prop_index) {
				auto& prop = props.at(prop_index);

				auto index = static_cast<size_t>(prop.name_offset);
				StringBuilder builder;
				for (size_t i = index; i < strings.size(); ++i) {
					char ch = strings.bytes().at(i);
					if (ch == 0)
						break;
					TRY(builder.try_append(ch));
				}

				auto name = builder.to_deprecated_string();

				Property value = [&prop, &strings]() -> Property {
					if (prop.is_string_prop) {
						StringBuilder builder;
						for (size_t i = prop.value; i < strings.size(); ++i) {
							char ch = strings.bytes().at(i);
							if (ch == 0)
								break;
							MUST(builder.try_append(ch));
						}
						return builder.to_deprecated_string();
					}
					return prop.value;
				}();

				TRY(m_properties.try_set(name, value));
			}
		}
			break;
		case PCF_ACCELERATORS:
			m_acc.no_overlap = TRY(read<u8>(format));
			m_acc.constant_metrics = TRY(read<u8>(format));
			m_acc.terminal_font = TRY(read<u8>(format));
			m_acc.constant_width = TRY(read<u8>(format));
			m_acc.ink_inside = TRY(read<u8>(format));
			m_acc.ink_metrics = TRY(read<u8>(format));
			m_acc.draw_direction = TRY(read<u8>(format));
			TRY(read<u8>(format)); // Padding.
			m_acc.font_ascent = TRY(read<i32>(format));
			m_acc.font_descent = TRY(read<i32>(format));
			m_acc.max_overlap = TRY(read<i32>(format));
			break;
		case PCF_METRICS:
		case PCF_INK_METRICS:
		{
			auto metrics_count = TRY(read<i16>(format));
			VERIFY(metrics_count > 0);

			for (i16 i = 0; i < metrics_count; ++i) {
				Metrics m;

				auto read_short = [this](i32 format) -> ErrorOr<i16> {
					if (format & PCF_COMPRESSED_METRICS) {
						u8 compressed = TRY(read<u8>(format));
						return static_cast<i16>(compressed) - 0x80;
					}
					return TRY(read<i16>(format));
				};

				m.left_side_bearing = TRY(read_short(format));
				m.right_side_bearing = TRY(read_short(format));
				m.character_width = TRY(read_short(format));
				m.character_ascent = TRY(read_short(format));
				m.character_descent = TRY(read_short(format));

				if (table.type == PCF_METRICS) {
					// Size of bitmaps
					TRY(m_metrics.try_append(m));
					m_max_ascent = max(m_max_ascent, m.character_ascent);
					m_max_descent = max(m_max_descent, m.character_descent);
					m_max_width = max(m_max_width, m.character_width);
				} else {
					// Minimum bounding box
					TRY(m_ink_metrics.try_append(m));
				}
			}
		}
			break;
		case PCF_BITMAPS:
			m_bitmap_data.glyph_count = TRY(read<i32>(format));
			m_bitmap_data.format = format;

			TRY(m_bitmap_data.offsets.try_resize(m_bitmap_data.glyph_count));
			for (i32 i = 0; i < m_bitmap_data.glyph_count; ++i)
				m_bitmap_data.offsets[i] = TRY(read<i32>(format));

			for (i32 i = 0; i < 4; ++i)
				m_bitmap_data.bitmap_sizes[i] = TRY(read<i32>(format));

			TRY(m_bitmap_data.data.try_resize(m_bitmap_data.bitmap_sizes[format & 3]));
			TRY(m_stream.read_some(m_bitmap_data.data));
				break;
		case PCF_BDF_ENCODINGS:
		{
			m_encoding.min_char_or_byte2 = TRY(read<i16>(format));
			m_encoding.max_char_or_byte2 = TRY(read<i16>(format));
			m_encoding.min_byte1 = TRY(read<i16>(format));
			m_encoding.max_byte1 = TRY(read<i16>(format));
			m_encoding.default_char = TRY(read<i16>(format));

			size_t num = (m_encoding.max_char_or_byte2 - m_encoding.min_char_or_byte2 + 1)
						 *(m_encoding.max_byte1 - m_encoding.min_byte1 + 1);
			TRY(m_encoding.indices.try_resize(num));
			for (size_t i = 0; i < num; ++i)
				m_encoding.indices[i] = TRY(read<i16>(format));
		}
			break;
		default:
			break;
		}
	}

	return {};
}

ErrorOr<void> PCFFile::convert_glyphs()
{
	// Both of these should have been populated by now, hopefully.
	VERIFY(m_metrics.size() == static_cast<size_t>(m_bitmap_data.glyph_count));

	TRY(m_glyphs.try_resize(m_bitmap_data.glyph_count));

	auto data = m_bitmap_data.data.bytes();
	auto size = glyph_size();

	auto format = m_bitmap_data.format;

	auto padding = format & 3;
	auto padding_bytes = padding == 0 ? 1 : padding * 2;

	auto unit = (format >> 4) & 3;
	// auto data_bytes = unit == 0 ? 1 : unit * 2; 

	auto lsb_first = format & 8;

	for (i32 i = 0; i < m_bitmap_data.glyph_count; ++i) {
		auto& glyph = m_glyphs.at(i);

		auto offset = m_bitmap_data.offsets[i];

		i16 w = m_metrics.at(i).character_width + m_acc.max_overlap;
		i32 h = m_metrics[i].character_ascent + m_metrics[i].character_descent;

		i16 bytes_per_row = max(w / 8, 1);
		if (bytes_per_row % padding_bytes != 0)
			bytes_per_row += padding_bytes - (bytes_per_row % padding_bytes);

		glyph.width = w;
		TRY(glyph.data.try_resize(w * size.height()));

		i16 shift = max(0, baseline() - m_metrics[i].character_ascent + 1);

		for (i32 y = 0; y < h; ++y) {
			for (i32 x = 0; x < w; ++x) {
				size_t index = (x / 8) + bytes_per_row * y;
				u8 byte = data[offset + index];
				u8 pixel;
				if (lsb_first)
					pixel = (byte << (x % 8)) & 0x80;
				else
					pixel = (byte >> (x % 8)) & 1;
				glyph.data[x + (y + shift) * w] = pixel;
			}
		}
	}

	return {};
}

