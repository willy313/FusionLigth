// Lumen: minimal PNG writer for the darkness mask: RGBA, 8 bit, rgb = 0, alpha = darkness.
// Uses "stored" (uncompressed) deflate blocks, so encoding is a memcpy plus two cheap checksums and needs no zlib.
// Header-only, no Fusion dependencies.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lumen {

inline std::uint32_t pngCrc32(const std::uint8_t* p, std::size_t n) {
	static const auto table = [] {
		struct T { std::uint32_t v[256]; } t;
		for (std::uint32_t i = 0; i < 256; ++i) {
			std::uint32_t c = i;
			for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			t.v[i] = c;
		}
		return t;
	}();
	std::uint32_t crc = 0xFFFFFFFFu;
	for (std::size_t i = 0; i < n; ++i) crc = table.v[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFu;
}

inline std::uint32_t pngAdler32(const std::uint8_t* p, std::size_t n) {
	std::uint32_t a = 1, b = 0;
	while (n > 0) {
		const std::size_t chunk = n < 5552 ? n : 5552;     // largest run before the 32-bit sums can overflow
		for (std::size_t i = 0; i < chunk; ++i) { a += p[i]; b += a; }
		a %= 65521u; b %= 65521u;
		p += chunk; n -= chunk;
	}
	return (b << 16) | a;
}

// argb: w*h pixels, alpha in the top byte (only the alpha is used). Returns the PNG file bytes.
inline std::vector<std::uint8_t> encodeAlphaPng(const std::uint32_t* argb, int w, int h) {
	const std::size_t rowBytes = 1 + (std::size_t)w * 4;     // filter byte 0 + RGBA
	std::vector<std::uint8_t> raw(rowBytes * (std::size_t)h, 0);
	for (int y = 0; y < h; ++y) {
		std::uint8_t* row = &raw[(std::size_t)y * rowBytes];
		row[0] = 0;                                          // filter: none
		const std::uint32_t* src = argb + (std::size_t)y * w;
		for (int x = 0; x < w; ++x) row[4 + x * 4] = (std::uint8_t)(src[x] >> 24);   // R,G,B stay 0, A = darkness
	}
	std::vector<std::uint8_t> z;                             // zlib stream made of stored blocks
	z.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
	z.push_back(0x78); z.push_back(0x01);
	std::size_t pos = 0;
	do {
		const std::size_t n = raw.size() - pos < 65535 ? raw.size() - pos : 65535;
		const bool last = pos + n >= raw.size();
		z.push_back(last ? 1 : 0);
		z.push_back((std::uint8_t)(n & 0xFF)); z.push_back((std::uint8_t)(n >> 8));
		z.push_back((std::uint8_t)(~n & 0xFF)); z.push_back((std::uint8_t)((~n >> 8) & 0xFF));
		z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
		pos += n;
	} while (pos < raw.size());
	const std::uint32_t ad = pngAdler32(raw.data(), raw.size());
	for (int s = 24; s >= 0; s -= 8) z.push_back((std::uint8_t)(ad >> s));

	std::vector<std::uint8_t> out;
	out.reserve(z.size() + 80);
	static const std::uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	out.insert(out.end(), sig, sig + 8);
	auto put32 = [&](std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) out.push_back((std::uint8_t)(v >> s)); };
	auto chunk = [&](const char* type, const std::uint8_t* data, std::size_t n) {
		put32((std::uint32_t)n);
		const std::size_t start = out.size();
		out.insert(out.end(), type, type + 4);
		if (n) out.insert(out.end(), data, data + n);
		put32(pngCrc32(&out[start], n + 4));
	};
	std::uint8_t ihdr[13] = { (std::uint8_t)((std::uint32_t)w >> 24), (std::uint8_t)((std::uint32_t)w >> 16), (std::uint8_t)((std::uint32_t)w >> 8), (std::uint8_t)w,
		(std::uint8_t)((std::uint32_t)h >> 24), (std::uint8_t)((std::uint32_t)h >> 16), (std::uint8_t)((std::uint32_t)h >> 8), (std::uint8_t)h,
		8, 6, 0, 0, 0 };                                      // 8 bit, colour type 6 (RGBA), deflate, no filter, no interlace
	chunk("IHDR", ihdr, 13);
	chunk("IDAT", z.data(), z.size());
	chunk("IEND", nullptr, 0);
	return out;
}

} // namespace lumen
