// Minimal stand-in for the DarkEdif API surface that LumenExt.cpp uses. It proves Lumen's own logic and that
// the generated .inc files agree with the C++; it does NOT prove the real SDK has these signatures.
#pragma once
#include <cstdint>
#include <cwchar>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using TCHAR = wchar_t;
enum class REFLAG { NONE = 0, ONE_SHOT = 1, DISPLAY = 2 };
struct RECT { long left, top, right, bottom; };

struct HeaderObject {
	int x = 0, y = 0; RECT hoRect{ 0, 0, 0, 0 }; int sizeW = 0, sizeH = 0, setSizeCalls = 0;
	int imgW = 0, imgH = 0, spotX = 0, spotY = 0;
	int get_X() const { return x; } int get_Y() const { return y; }
	int get_ImgWidth() const { return imgW; } int get_ImgHeight() const { return imgH; }
	int get_ImgXSpot() const { return spotX; } int get_ImgYSpot() const { return spotY; }
	void SetSize(int w, int h) { sizeW = w; sizeH = h; ++setSizeCalls; }
};
struct RunObject { HeaderObject ho; int fixed = 0; HeaderObject* get_rHo() { return &ho; } };
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
inline BlitOptions operator|(BlitOptions a, BlitOptions b) { return (BlitOptions)((int)a | (int)b); }
enum class BlitOperation { Copy, Blend };

struct Blit { int srcW, srcH, dl, dt, dr, db; BlitOperation op; int coef; BlitOptions flags; bool transparent; };
extern std::vector<Blit> g_blits;
extern bool g_blitWhileLocked;
struct Surface;
extern const Surface* g_lastBlitSource;
template<typename T, typename C>
class Holder {            // same shape as the SDK's: not copyable or movable, operator* returns the pointer
	static_assert(std::is_pointer_v<T>, "T must be pointer");
	C* clazz; T ptr; void (*deleter)(C*, T);
public:
	const T& operator*() const { return ptr; }
	Holder(C* c, T p, void (d)(C*, T)) : clazz(c), ptr(p), deleter(d) {}
	Holder(const Holder&&) = delete;
	Holder(const Holder&) = delete;
	~Holder() { deleter(clazz, ptr); ptr = nullptr; }
};
struct Surface {
	std::size_t w, h; bool hasAlpha; long pixelsSet = 0; std::vector<std::uint32_t> pix;
	std::vector<std::uint8_t> alphaPlane; int alphaPitch = 0; bool lockedAlpha = false; int alphaLocks = 0; bool transparencyFromAlpha = false;
	std::uint32_t lastFill = 0;
	Surface* alphaParent = nullptr; std::unique_ptr<Surface> alphaView;
	Surface(RunHeader*, bool, bool, std::size_t w_, std::size_t h_, bool alpha) : w(w_), h(h_), hasAlpha(alpha), pix(w_ * h_, 0) {
		alphaPitch = (int)((w_ + 3) / 4 * 4);                  // rows are padded, like the real alpha plane
		if (alpha) alphaPlane.assign((std::size_t)alphaPitch * h_, 255);   // Fusion starts opaque
	}
	Surface(const Surface&) = delete;
	Surface(Surface&&) = delete;
	bool FillImageWith(const SurfaceFill& f) { lastFill = f.c; return true; }   // colour only: alpha plane untouched
	bool SetTransparencyToColor(std::uint32_t) { return true; }
	void SetTransparencyToAlpha() { transparencyFromAlpha = true; }
	int setAlphaCalls = 0;
	void SetAlpha(const std::byte* bytes, int pitch) {          // copies from the caller's buffer into our own padded layout
		++setAlphaCalls;
		if (!hasAlpha) return;
		for (std::size_t y = 0; y < h; ++y)
			for (std::size_t x = 0; x < w; ++x) alphaPlane[y * alphaPitch + x] = (std::uint8_t)bytes[y * pitch + x];
	}
	bool HasAlpha() const { return hasAlpha; }
	int GetAlphaPitch() const { return alphaPitch; }
	Holder<std::uint8_t*, Surface> GetAlphaBytes() {
		lockedAlpha = true; ++alphaLocks;
		return Holder<std::uint8_t*, Surface>(this, hasAlpha ? alphaPlane.data() : nullptr, [](Surface* s, std::uint8_t*) { s->lockedAlpha = false; });
	}
	Holder<Surface*, Surface> GetAlphaSurface() {
		alphaView = std::make_unique<Surface>(nullptr, true, false, w, h, false);
		alphaView->alphaParent = this;
		return Holder<Surface*, Surface>(this, alphaView.get(), [](Surface*, Surface*) {});
	}
	void SetPixel(Point p, std::uint32_t c, bool includeAlpha = true) {     // moves COLOUR only, like the real one
		++pixelsSet; (void)includeAlpha;
		if (alphaParent) {                                                  // a view of the alpha plane: grey value = alpha
			if (p.x >= 0 && p.y >= 0 && (std::size_t)p.x < w && (std::size_t)p.y < h)
				alphaParent->alphaPlane[(std::size_t)p.y * alphaParent->alphaPitch + p.x] = (std::uint8_t)(c & 0xFF);
			return;
		}
		if (p.x >= 0 && p.y >= 0 && (std::size_t)p.x < w && (std::size_t)p.y < h) pix[(std::size_t)p.y * w + p.x] = c & 0x00FFFFFF;
	}
	bool CopySection(Rect s, Surface&, Rect d, bool transparent = true, BlitOptions f = BlitOptions::CopyAlpha,
		BlitOperation op = BlitOperation::Copy, std::uint8_t coef = 0) const {
		g_blits.push_back({ s.right - s.left, s.bottom - s.top, d.left, d.top, d.right, d.bottom, op, coef, f, transparent });
		g_lastBlitSource = this;
		g_blitWhileLocked = g_blitWhileLocked || lockedAlpha;
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
	int FixedFromRunObjPtr(RunObject* o) { return o ? o->fixed : 0; }
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
