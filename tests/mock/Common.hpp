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
using Colour = uint32_t;
inline Colour ColorRGB(int r, int g, int b) { return 0xFF000000u | (b << 16) | (g << 8) | r; }
struct SurfaceFill { Colour c; static SurfaceFill Solid(Colour c) { return { c }; } };
enum class BlitOptions { None };
enum class BlitOperation { Copy, Blend };
struct Rect { long left, top, right, bottom; };

struct Blit { int srcW, srcH, dl, dt, dr, db; BlitOperation op; int coef; };
extern std::vector<DarkEdif::Blit> g_blits;
struct Surface {
	int w, h; long pixelsSet = 0;
	Surface(RunHeader*, bool, bool, int w_, int h_, bool) : w(w_), h(h_) {}
	void FillImageWith(SurfaceFill) {}
	void SetTransparencyToColor(Colour) {}
	void SetPixel(int, int, Colour) { ++pixelsSet; }   // the real SDK has no 4-argument form
	void CopySection(Rect s, Surface&, Rect d, bool, BlitOptions, BlitOperation op, int coef) {
		g_blits.push_back({ (int)(s.right - s.left), (int)(s.bottom - s.top), (int)d.left, (int)d.top, (int)d.right, (int)d.bottom, op, coef });
	}
	static Surface* CreateFromMainWindow(RunHeader*) { static Surface s(nullptr, false, false, 640, 480, false); return &s; }
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
