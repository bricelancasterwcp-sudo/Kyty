#include "Emulator/Loader/Nid.h"

#include "Kyty/Core/Vector.h"

#include <cstring>

#ifdef KYTY_EMU_ENABLED

namespace Kyty::Loader {

// NID = custom base64 of the first 8 digest bytes (little-endian) of
// SHA-1(symbol_name + fixed 16-byte suffix)

static constexpr uint8_t NID_SUFFIX[16] = {0x51, 0x8D, 0x64, 0xA6, 0x35, 0xDE, 0xD8, 0xC1,
                                           0xE6, 0xB0, 0x39, 0xB1, 0xC3, 0xE5, 0x52, 0x30};

static constexpr char NID_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";

static uint32_t rol32(uint32_t value, int bits)
{
	return (value << static_cast<uint32_t>(bits)) | (value >> static_cast<uint32_t>(32 - bits));
}

static void sha1(const uint8_t* data, uint32_t len, uint8_t out[20])
{
	uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

	// Symbol names are short; pad into one heap buffer instead of streaming
	uint32_t padded_len = ((len + 1 + 8 + 63) / 64) * 64;

	Vector<uint8_t> buf(padded_len);
	std::memset(buf.GetData(), 0, padded_len);
	std::memcpy(buf.GetData(), data, len);
	buf.GetData()[len] = 0x80;

	uint64_t bits = static_cast<uint64_t>(len) * 8;
	for (int i = 0; i < 8; i++)
	{
		buf.GetData()[padded_len - 8 + i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
	}

	for (uint32_t block = 0; block < padded_len; block += 64)
	{
		const uint8_t* p = buf.GetDataConst() + block;

		uint32_t w[80];
		for (int i = 0; i < 16; i++)
		{
			w[i] = (static_cast<uint32_t>(p[i * 4]) << 24u) | (static_cast<uint32_t>(p[i * 4 + 1]) << 16u) |
			       (static_cast<uint32_t>(p[i * 4 + 2]) << 8u) | static_cast<uint32_t>(p[i * 4 + 3]);
		}
		for (int i = 16; i < 80; i++)
		{
			w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
		}

		uint32_t a = h[0];
		uint32_t b = h[1];
		uint32_t c = h[2];
		uint32_t d = h[3];
		uint32_t e = h[4];

		for (int i = 0; i < 80; i++)
		{
			uint32_t f = 0;
			uint32_t k = 0;

			if (i < 20)
			{
				f = (b & c) | ((~b) & d);
				k = 0x5A827999u;
			} else if (i < 40)
			{
				f = b ^ c ^ d;
				k = 0x6ED9EBA1u;
			} else if (i < 60)
			{
				f = (b & c) | (b & d) | (c & d);
				k = 0x8F1BBCDCu;
			} else
			{
				f = b ^ c ^ d;
				k = 0xCA62C1D6u;
			}

			uint32_t temp = rol32(a, 5) + f + e + k + w[i];

			e = d;
			d = c;
			c = rol32(b, 30);
			b = a;
			a = temp;
		}

		h[0] += a;
		h[1] += b;
		h[2] += c;
		h[3] += d;
		h[4] += e;
	}

	for (int i = 0; i < 5; i++)
	{
		out[i * 4]     = static_cast<uint8_t>(h[i] >> 24u);
		out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16u);
		out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8u);
		out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
	}
}

String NidHash(const char* symbol)
{
	auto name_len = static_cast<uint32_t>(std::strlen(symbol));

	Vector<uint8_t> input(name_len + sizeof(NID_SUFFIX));
	std::memcpy(input.GetData(), symbol, name_len);
	std::memcpy(input.GetData() + name_len, NID_SUFFIX, sizeof(NID_SUFFIX));

	uint8_t digest[20];
	sha1(input.GetDataConst(), input.Size(), digest);

	uint64_t value = 0;
	for (int i = 7; i >= 0; i--)
	{
		value = (value << 8u) | digest[i];
	}

	char nid[12];
	for (int i = 0; i < 11; i++)
	{
		int shift = 64 - 6 * (i + 1);
		auto index =
		    (shift >= 0 ? (value >> static_cast<uint32_t>(shift)) : (value << static_cast<uint32_t>(-shift))) & 0x3Fu;
		nid[i] = NID_ALPHABET[index];
	}
	nid[11] = '\0';

	return String::FromUtf8(nid);
}

} // namespace Kyty::Loader

#endif // KYTY_EMU_ENABLED
