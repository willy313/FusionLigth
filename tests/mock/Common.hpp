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
using Colour = uint32_t;
inline Colour ColorRGB(int r, int g, int b) { return 0xFF000000u | (b << 16) | (g << 8) | r; }
struct SurfaceFill { Colour c; static SurfaceFill Solid(Colour c) { return { c }; } };
enum class BlitOptions { None };
enum class BlitOperation { Copy, Blend };
struct RECT { long left, top, right, bottom; };

struct HeaderObject { int x = 0, y = 0; RECT hoRect{ 0, 0, 0, 0 }; int get_X() { return x; } int get_Y() { return y; } };
struct RunObject { HeaderObject ho; HeaderObject* get_rHo() { return &ho; } };
struct FrameHdr { int Width = 2000, Height = 1500; };
struct Frame { FrameHdr hdr; };
struct RunHeader {
	struct { int rh3WindowSx = 640, rh3WindowSy = 480; } rh3;
	Frame* rhFrame = nullptr;
	int winX = 0, winY = 0;
	int get_WindowX() { return winX; }
	int get_WindowY() { return winY; }
};

struct Blit { int srcW, srcH, dl, dt, dr, db; BlitOperation op; int coef; };
extern std::vector<Blit> g_blits;
struct Surface {
	int w, h; long pixelsSet = 0;
	Surface(RunHeader*, bool, bool, int w_, int h_, bool) : w(w_), h(h_) {}
	void FillImageWith(SurfaceFill) {}
	void SetTransparencyToColor(Colour) {}
	void SetPixel(int, int, Colour, bool) { ++pixelsSet; }
	void CopySection(RECT s, Surface&, RECT d, bool, BlitOptions, BlitOperation op, int coef) {
		g_blits.push_back({ (int)(s.right - s.left), (int)(s.bottom - s.top), (int)d.left, (int)d.top, (int)d.right, (int)d.bottom, op, coef });
	}
	static Surface* CreateFromMainWindow(RunHeader*) { static Surface s(nullptr, false, false, 640, 480, false); return &s; }
};

namespace Edif {
struct Runtime {
	int events = 0; std::map<int, RunObject*> objects;
	void GenerateEvent(int) { ++events; }
	RunObject* RunObjPtrFromFixed(int f) { auto it = objects.find(f); return it == objects.end() ? nullptr : it->second; }
};
}

struct PropsMock {
	bool IsPropChecked(std::string_view) const { return true; }
	std::wstring GetPropertyStr(std::string_view) const { return L"Half"; }
};
struct EDITDATA { PropsMock Props; };
#define LinkAction(id, fn) (void)0
#define LinkCondition(id, fn) (void)0
#define LinkExpression(id, fn) (void)0

#include "LumenState.hpp"
class Extension {
public:
	RunHeader* rhPtr;
	Edif::Runtime Runtime;
	#include "LumenMembers.inc"
	Extension(RunHeader* rh, const EDITDATA* edPtr) : rhPtr(rh) {
		#include "LumenCtor.inc"
	}
};
