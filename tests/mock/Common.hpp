// Minimal stand-in for the DarkEdif API surface that LumenExt.cpp uses. It proves Lumen's own logic and that
// the generated .inc files agree with the C++; it does NOT prove the real SDK has these signatures.
#pragma once
#include <cstdint>
#include <cwchar>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using TCHAR = wchar_t;
enum class REFLAG { NONE = 0, ONE_SHOT = 1, DISPLAY = 2 };
struct RECT { long left, top, right, bottom; };

struct HeaderObject {
	int x = 0, y = 0; RECT hoRect{ 0, 0, 0, 0 }; int sizeW = 0, sizeH = 0, setSizeCalls = 0;
	int get_X() { return x; } int get_Y() { return y; }
	void SetSize(int w, int h) { sizeW = w; sizeH = h; ++setSizeCalls; }
};
struct RunObject { HeaderObject ho; HeaderObject* get_rHo() { return &ho; } };
struct RunObjectMultiPlatPtr { RunObject* ro; RunObject* operator->() { return ro; } };   // real one is used as rdPtr->get_rHo()
struct FrameHdr { int Width = 2000, Height = 1500; };
struct Frame { FrameHdr hdr; };
struct RunHeader {
	struct { int rh3WindowSx = 640, rh3WindowSy = 480; } rh3;
	Frame* rhFrame = nullptr;
	int winX = 0, winY = 0;
	int get_WindowX() { return winX; }
	int get_WindowY() { return winY; }
};

namespace DarkEdif {
// Mirrors the real SDK (SurfaceMultiPlat.hpp): Point and Rect reject negatives unless allowNeg; SetPixel takes a Point;
// CreateFromMainWindow returns a reference; Surface is not copyable.
extern int g_negViolations;
struct Point {
	std::int32_t x = 0, y = 0;
	Point() = default;
	Point(std::int32_t x_, std::int32_t y_, bool allowNeg = false) : x(x_), y(y_) { if (!allowNeg && (x < 0 || y < 0)) ++g_negViolations; }
};
struct Rect {
	std::int32_t left = 0, top = 0, right = 0, bottom = 0;
	Rect() = default;
	Rect(std::int32_t r, std::int32_t b) : right(r), bottom(b) {}
	Rect(std::int32_t l, std::int32_t t, std::int32_t r, std::int32_t b, bool allowNeg = false) : left(l), top(t), right(r), bottom(b) {
		if (!allowNeg && (l < 0 || t < 0)) ++g_negViolations;
	}
};
constexpr inline std::uint32_t ColorRGB(std::uint8_t r, std::uint8_t g, std::uint8_t b) { return 0xFF000000u | (b << 16) | (g << 8) | r; }
struct SurfaceFill { std::uint32_t c; static SurfaceFill Solid(std::uint32_t c) { return { c }; } };
enum class BlitOptions { None = 0, AntiAliasing = 1, CopyAlpha = 2 };
enum class BlitOperation { Copy, Blend };

struct Blit { int srcW, srcH, dl, dt, dr, db; BlitOperation op; int coef; BlitOptions flags; };
extern std::vector<Blit> g_blits;
extern bool g_lastSetPixelIncludedAlpha;
struct Surface {
	std::size_t w, h; long pixelsSet = 0;
	Surface(RunHeader*, bool, bool, std::size_t w_, std::size_t h_, bool) : w(w_), h(h_) {}
	Surface(const Surface&) = delete;
	Surface(Surface&&) = delete;
	bool FillImageWith(const SurfaceFill&) { return true; }
	bool SetTransparencyToColor(std::uint32_t) { return true; }
	void SetPixel(Point, std::uint32_t, bool includeAlpha = true) { ++pixelsSet; if (includeAlpha) g_lastSetPixelIncludedAlpha = true; }
	bool CopySection(Rect s, Surface&, Rect d, bool = true, BlitOptions f = BlitOptions::CopyAlpha,
		BlitOperation op = BlitOperation::Copy, std::uint8_t coef = 0) const {
		g_blits.push_back({ s.right - s.left, s.bottom - s.top, d.left, d.top, d.right, d.bottom, op, coef, f });
		return true;
	}
	static Surface& CreateFromMainWindow(RunHeader*) { static Surface s(nullptr, false, false, 640, 480, false); return s; }
};
} // namespace DarkEdif

namespace Edif {
struct Runtime {
	int events = 0; std::map<int, RunObject*> objects;
	int posX = -1, posY = -1, posCalls = 0;
	void GenerateEvent(int) { ++events; }
	void SetPosition(int x, int y) { posX = x; posY = y; ++posCalls; }
	RunObject* RunObjPtrFromFixed(int f) { auto it = objects.find(f); return it == objects.end() ? nullptr : it->second; }
};
}

struct PropsMock {
	bool IsPropChecked(std::string_view) const { return true; }
	std::wstring GetPropertyStr(std::string_view) const { return L"2"; }
};
struct EDITDATA { PropsMock Props; struct { int width = 100, height = 50; } objSize; };
#define LinkAction(id, fn) (void)0
#define LinkCondition(id, fn) (void)0
#define LinkExpression(id, fn) (void)0

#include "LumenState.hpp"
class Extension {
public:
	RunHeader* rhPtr;
	RunObjectMultiPlatPtr rdPtr;
	Edif::Runtime Runtime;
	#include "LumenMembers.inc"
	Extension(RunHeader* rh, RunObject* obj, const EDITDATA* edPtr) : rhPtr(rh), rdPtr{ obj } {
		#include "LumenCtor.inc"
	}
};
