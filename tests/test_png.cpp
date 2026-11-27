// Writes sample PNGs plus the alpha values they must decode to; tests/check_png.py decodes them with Python's zlib.
#include "../core/lumen_png.hpp"
#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
	const std::string dir = argc > 1 ? argv[1] : "/tmp/lumen_png";
	struct Size { int w, h; } sizes[] = { { 1, 1 }, { 3, 2 }, { 17, 5 }, { 642, 386 }, { 322, 194 }, { 1282, 770 } };
	for (auto s : sizes) {
		std::vector<std::uint32_t> px((size_t)s.w * s.h);
		for (int y = 0; y < s.h; ++y)
			for (int x = 0; x < s.w; ++x) px[(size_t)y * s.w + x] = (std::uint32_t)((x * 7 + y * 13) & 0xFF) << 24 | 0x00123456;   // rgb must be ignored
		auto png = lumen::encodeAlphaPng(px.data(), s.w, s.h);
		const std::string base = dir + "/" + std::to_string(s.w) + "x" + std::to_string(s.h);
		std::ofstream(base + ".png", std::ios::binary).write((const char*)png.data(), (std::streamsize)png.size());
		std::vector<std::uint8_t> a(px.size());
		for (size_t i = 0; i < px.size(); ++i) a[i] = (std::uint8_t)(px[i] >> 24);
		std::ofstream(base + ".alpha", std::ios::binary).write((const char*)a.data(), (std::streamsize)a.size());
	}
	return 0;
}
