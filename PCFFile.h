#pragma once

#include <AK/Endian.h>
#include <AK/Forward.h>
#include <AK/HashMap.h>
#include <AK/MemoryStream.h>
#include <AK/Stream.h>
#include <AK/DeprecatedString.h>
#include <AK/StringBuilder.h>
#include <AK/Vector.h>
#include <LibGfx/Bitmap.h>

#define PCF_GLYPH_PAD_MASK      (3<<0)
#define PCF_BYTE_MASK           (1<<2)
#define PCF_BIT_MASK            (1<<3)
#define PCF_SCAN_UNIT_MASK      (3<<4)

class PCFFile : public RefCounted<PCFFile> {
public:
	static ErrorOr<NonnullRefPtr<PCFFile>> create(ReadonlyBytes);

	ErrorOr<DeprecatedString> construct_filename() const;

	Optional<u16> glyph_index_for(u16 code_point) const;
	ErrorOr<void> draw_glyph(u16 index, Gfx::GlyphBitmap&) const;
	u8 glyph_width(u16 index) const { return m_glyphs.at(index).width; }
	u8 baseline() const;
	size_t highest_codepoint() const;

	DeprecatedString family() const;
	DeprecatedString name() const;
	DeprecatedString weight_name() const;

	i32 weight() const;
	i32 relative_weight() const;
	i32 slope() const;
	i32 pixel_size() const;
	i32 x_height() const;
	Gfx::IntSize glyph_size() const;
	bool is_fixed_width() const { return m_acc.constant_width != 0; };
	size_t glyph_count() const { return m_glyphs.size(); }

private:
	PCFFile(ReadonlyBytes);

	ErrorOr<void> populate_tables();
	ErrorOr<void> convert_glyphs();
	
	template<typename T>
	ErrorOr<T> read(i32 format) const
	{
		T value;
		if (format & PCF_BYTE_MASK)
			value = TRY(m_stream.read_value<BigEndian<T>>());
		else
			value = TRY(m_stream.read_value<LittleEndian<T>>());

		// FIXME: Do we have to reverse the bits here?
		if (!(format & PCF_BIT_MASK))
			TODO();

		return value;
	}

	struct Header {
		char magic[4];
		i32 table_count;
		struct TOCEntry {
			i32 type;
			i32 format;
			i32 size;
			i32 offset;
		};
	};

	typedef Variant<DeprecatedString, i32> Property;
	struct PropertiesTable {
		i32 nprops;
		struct Props {
			i32 name_offset;
			i8 is_string_prop;
			i32 value;
		};
	};

	struct Metrics {
		i16 left_side_bearing;
		i16 right_side_bearing;
		i16 character_width;
		i16 character_ascent;
		i16 character_descent;
	};

	struct AcceleratorTable {
		u8 no_overlap;
		u8 constant_metrics;
		u8 terminal_font;
		u8 constant_width;
		u8 ink_inside;
		u8 ink_metrics;
		u8 draw_direction;
		i32 font_ascent;
		i32 font_descent;
		i32 max_overlap;
	};

	struct BitmapData {
		i32 format;
		i32 glyph_count;
		Vector<i32> offsets;
		i32 bitmap_sizes[4];
		ByteBuffer data;
	};

	struct EncodingTable {
		i16 min_char_or_byte2;
		i16 max_char_or_byte2;
		i16 min_byte1;
		i16 max_byte1;
		i16 default_char;
		Vector<i16> indices;
	};

	struct Glyph {
		u8 width;
		Vector<u8> data;
	};

	BitmapData m_bitmap_data;

	EncodingTable m_encoding;

	Vector<Header::TOCEntry> m_tables;
	HashMap<DeprecatedString, Property> m_properties;
	Vector<Metrics> m_metrics;
	Vector<Metrics> m_ink_metrics;
	Vector<Glyph> m_glyphs;
	AcceleratorTable m_acc;

	i16 m_max_ascent { 0 };
	i16 m_max_descent { 0 };

	i16 m_max_width { 0 };

	mutable FixedMemoryStream m_stream;
};

