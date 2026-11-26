// Lumen: the Fusion-facing layer. Every SDK call here was checked against the SDK headers uploaded from a CI run
// (SurfaceMultiPlat.hpp, MMFWindowsMasterHeader.hpp, Edif.hpp).
#include "Common.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <fstream>
#include <map>
#include <string>
#include <vector>

static_assert(sizeof(TCHAR) == sizeof(wchar_t), "Lumen is built for the Unicode configurations only");

// Surface, SurfaceFill, ColorRGB, BlitOptions, Point and Rect live in the DarkEdif namespace in this SDK.
using namespace DarkEdif;

// ------------------------------------------------------------------ breadcrumbs and method memory
// A hard crash shows no message, so the first steps of every risky path are appended to %TEMP%\\Lumen-debug.log;
// the LAST line is the step that crashed (bounded to 80 lines per run). A second small file, Lumen-method.txt, remembers
// which drawing method was tried and which was confirmed, so a method that crashed is skipped on the next launch.
static std::string tempDir() {
#ifdef _WIN32
	char b[MAX_PATH + 2] = { 0 };
	const DWORD n = GetTempPathA(MAX_PATH, b);
	return std::string(b, n);
#else
	const char* t = std::getenv("TMPDIR");
	return std::string(t ? t : "/tmp") + "/";
#endif
}
static void crumb(const std::string& msg) {
	static int n = 0;
	if (n >= 80) return;
	std::ofstream f(tempDir() + "Lumen-debug.log", n == 0 ? std::ios::trunc : std::ios::app);
	if (f) { f << msg << "\n"; f.flush(); ++n; }
}

enum { M_ALPHA_LOCK = 0, M_ALPHA_SURFACE = 1, M_RECTS = 2, M_COUNT = 3 };
static const char* methodName(int m) {
	return m == M_ALPHA_LOCK ? "alpha lock" : m == M_ALPHA_SURFACE ? "alpha surface" : m == M_RECTS ? "rectangles" : "unset";
}
static const std::string kBuildId = std::string(__DATE__) + " " + __TIME__;   // a new build forgets old verdicts

static void markMethod(const char* what, int m) {
	std::ofstream f(tempDir() + "Lumen-method.txt", std::ios::app);
	if (f) { f << what << " " << m << "\n"; f.flush(); }
}
// A method is "bad" when it was tried but never confirmed (the app died while using it).
static int chooseMethod() {
	bool tried[M_COUNT] = { false }, ok[M_COUNT] = { false };
	{
		std::ifstream f(tempDir() + "Lumen-method.txt");
		std::string line;
		bool sameBuild = false;
		if (std::getline(f, line) && line == "build " + kBuildId) sameBuild = true;
		while (sameBuild && std::getline(f, line)) {
			char w[16] = { 0 }; int m = -1;
			if (std::sscanf(line.c_str(), "%15s %d", w, &m) == 2 && m >= 0 && m < M_COUNT) {
				if (std::string(w) == "tried") tried[m] = true;
				else if (std::string(w) == "ok") ok[m] = true;
			}
		}
		if (!sameBuild) {
			std::ofstream o(tempDir() + "Lumen-method.txt", std::ios::trunc);
			if (o) o << "build " << kBuildId << "\n";
		}
	}
	// Preference order: rectangles first (needs no alpha plane; both alpha methods crashed on the tested SDK build:
	// GetAlphaBytes() faulted and GetAlphaSurface() raised "Unexpected depth 8. Cannot detect color format"), then the
	// alpha methods, which stay available through the debug action.
	static const int order[M_COUNT] = { M_RECTS, M_ALPHA_LOCK, M_ALPHA_SURFACE };
	int pick = -1;
	for (int i = 0; i < M_COUNT && pick < 0; ++i) if (ok[order[i]]) pick = order[i];       // a confirmed method wins
	for (int i = 0; i < M_COUNT && pick < 0; ++i) if (!tried[order[i]]) pick = order[i];   // else the first not tried yet
	if (pick < 0) pick = M_RECTS;                                                           // everything died: most conservative
	crumb(std::string("method: chose ") + methodName(pick));
	markMethod("tried", pick);
	return pick;
}

// ------------------------------------------------------------------ graphics
// Darkness reaches the screen by one of three methods (see chooseMethod):
//  0 alpha lock     one black CPU surface with an alpha plane; darkness written through GetAlphaBytes()
//  1 alpha surface  the same surface; darkness written as grey pixels of GetAlphaSurface()
//  2 rectangles     no alpha plane: runs of equal darkness are drawn as stretched 1x1 black blits with a Blend
//                   coefficient (a 1x1 surface stretched with CopySection is confirmed to fill a rectangle on the frame)
// SetPixel moves colour only, never alpha, and SetAlpha() crashed in testing, so neither is used.
struct Run { int x0, x1, y0, y1, q; };   // lightmap cells [x0,x1) x [y0,y1), darkness level q (1..levels)

struct LumenGfx {
	std::unique_ptr<Surface> mask;       // methods 0 and 1
	std::unique_ptr<Surface> dot;        // method 2: 1x1 black
	int mw = 0, mh = 0;
	std::vector<Run> runs;               // method 2
};

LumenState::~LumenState() { delete gfx; }

static void ensureMask(Extension& e, LumenState& S, int mw, int mh) {
	if (!S.gfx) S.gfx = new LumenGfx();
	LumenGfx& G = *S.gfx;
	if (G.mask && G.mw == mw && G.mh == mh) return;
	crumb("mask: create " + std::to_string(mw) + "x" + std::to_string(mh) + " alpha surface");
	G.mask = std::make_unique<Surface>(e.rhPtr, true, false, mw, mh, true);   // alpha = true: per-pixel transparency
	crumb("mask: created, filling RGB black");
	G.mask->FillImageWith(SurfaceFill::Solid(ColorRGB(0, 0, 0)));               // RGB is black everywhere, forever
	G.mw = mw; G.mh = mh;
	crumb(std::string("mask: ready, HasAlpha=") + (G.mask->HasAlpha() ? "1" : "0"));
	S.maskDirty = true;
}

static void ensureDot(Extension& e, LumenState& S) {
	if (!S.gfx) S.gfx = new LumenGfx();
	LumenGfx& G = *S.gfx;
	if (G.dot) return;
	crumb("dot: create 1x1 black surface");
	G.dot = std::make_unique<Surface>(e.rhPtr, true, false, 1, 1, false);
	G.dot->FillImageWith(SurfaceFill::Solid(ColorRGB(1, 1, 1)));   // near-black: safe even if black is a transparent colour key
	crumb("dot: ready");
}

static void buildRuns(LumenState& S) {
	LumenGfx& G = *S.gfx;
	const std::vector<uint32_t>& px = S.engine.pixels();
	const int mw = S.engine.mapWidth(), mh = S.engine.mapHeight();
	const int kLevels = S.levels;
	G.runs.clear();
	std::vector<Run> open;                                   // runs extended by the previous row
	std::map<std::array<int, 3>, size_t> openIdx;
	for (int y = 0; y < mh; ++y) {
		std::vector<Run> next; std::map<std::array<int, 3>, size_t> nextIdx;
		int x = 0;
		while (x < mw) {
			const int a = (int)(px[(size_t)y * mw + x] >> 24);
			const int q = (a * kLevels + 127) / 255;
			int x1 = x + 1;
			while (x1 < mw && ((int)(px[(size_t)y * mw + x1] >> 24) * kLevels + 127) / 255 == q) ++x1;
			if (q > 0) {
				auto it = openIdx.find({ x, x1, q });
				if (it != openIdx.end()) { Run r = open[it->second]; r.y1 = y + 1; open[it->second].q = -1; next.push_back(r); }
				else next.push_back({ x, x1, y, y + 1, q });
				nextIdx[{ x, x1, q }] = next.size() - 1;
			}
			x = x1;
		}
		for (const Run& r : open) if (r.q > 0) G.runs.push_back(r);   // not extended: finished
		open.swap(next); openIdx.swap(nextIdx);
	}
	for (const Run& r : open) G.runs.push_back(r);
	S.runCount = (int)G.runs.size();
}

static void writeAlphaLock(Extension& e, LumenState& S) {
	LumenGfx& G = *S.gfx;
	const std::vector<uint32_t>& px = S.engine.pixels();
	const int mw = G.mw, mh = G.mh;
	static int logged = 0;
	if (logged < 2) { ++logged; crumb("alpha lock: GetAlphaBytes begin"); }
	auto lock = G.mask->GetAlphaBytes();              // locks the alpha plane until `lock` goes out of scope
	std::uint8_t* alpha = *lock;
	const int pitch = G.mask->GetAlphaPitch();
	if (logged <= 2) crumb("alpha lock: locked, pitch " + std::to_string(pitch) + (alpha ? "" : " (NULL pointer)"));
	if (!alpha) { S.lastError = "Lumen could not lock the alpha plane"; return; }
	S.alphaPitch = pitch;
	for (int y = 0; y < mh; ++y) {
		std::uint8_t* row = alpha + (std::ptrdiff_t)y * pitch;
		const uint32_t* src = &px[(size_t)y * mw];
		for (int x = 0; x < mw; ++x) row[x] = (std::uint8_t)(src[x] >> 24);   // 0 = lit (transparent), 255 = opaque black
	}
	if (logged <= 2) crumb("alpha lock: written");
	(void)e;
}

static void writeAlphaSurface(LumenState& S) {
	LumenGfx& G = *S.gfx;
	const std::vector<uint32_t>& px = S.engine.pixels();
	static int logged = 0;
	if (logged < 2) { ++logged; crumb("alpha surface: GetAlphaSurface begin"); }
	auto holder = G.mask->GetAlphaSurface();
	Surface* as = *holder;
	if (logged <= 2) crumb(as ? "alpha surface: got it" : "alpha surface: NULL");
	if (!as) { S.lastError = "Lumen could not get the alpha surface"; return; }
	for (int y = 0; y < G.mh; ++y)
		for (int x = 0; x < G.mw; ++x) {
			const std::uint32_t a = px[(size_t)y * G.mw + x] >> 24;
			as->SetPixel(Point(x, y), (a << 16) | (a << 8) | a, false);   // grey value = alpha
		}
	if (logged <= 2) crumb("alpha surface: written");
}

static void rebuildMask(Extension& e, LumenState& S) {
	const auto t0 = std::chrono::steady_clock::now();
	const std::vector<uint32_t>& px = S.engine.pixels();
	const int mw = S.engine.mapWidth(), mh = S.engine.mapHeight();
	if (px.size() != (size_t)mw * mh) return;
	if (S.strategy < 0) S.strategy = S.forcedStrategy >= 0 ? S.forcedStrategy : chooseMethod();
	if (S.strategy == M_RECTS) {
		ensureDot(e, S);
		buildRuns(S);
	} else {
		ensureMask(e, S, mw, mh);
		if (!S.gfx->mask->HasAlpha()) { S.lastError = "Lumen mask surface has no alpha plane"; return; }
		if (S.strategy == M_ALPHA_LOCK) writeAlphaLock(e, S); else writeAlphaSurface(S);
	}
	S.lastMaskMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// ------------------------------------------------------------------ objects (HeaderObject coordinates are frame pixels)

static bool objPos(Extension& e, long fixed, float& x, float& y) {
	RunObject* ro = e.Runtime.RunObjPtrFromFixed((int)fixed);
	if (!ro) return false;
	HeaderObject* h = ro->get_rHo();
	x = (float)h->get_X();
	y = (float)h->get_Y();
	return true;
}

static bool objRect(Extension& e, long fixed, lumen::IRect& r) {
	RunObject* ro = e.Runtime.RunObjPtrFromFixed((int)fixed);
	if (!ro) return false;
	HeaderObject* h = ro->get_rHo();
	const int left = h->get_X() - h->get_ImgXSpot(), top = h->get_Y() - h->get_ImgYSpot();
	r = { left, top, left + h->get_ImgWidth(), top + h->get_ImgHeight() };
	return true;
}

// ------------------------------------------------------------------ frame loop

void Extension::LumenInit(bool backdropObstacles, const std::wstring& pixelSize, int objW, int objH) {
	crumb("init: begin");
	lumen::Engine::Settings st;
	const long ps = std::wcstol(pixelSize.c_str(), nullptr, 10);
	st.divisor = (ps >= 1 && ps <= 8) ? (int)ps : 2;
	int fw = 1024, fh = 768;
#ifdef FUSION_INTERNAL_ACCESS
	fw = rhPtr->rhFrame->hdr.Width;
	fh = rhPtr->rhFrame->hdr.Height;
#endif
	crumb("init: frame " + std::to_string(fw) + "x" + std::to_string(fh) + ", pixel size " + std::to_string(st.divisor));
	lx = std::make_unique<LumenState>(fw, fh, st);
	lx->backdropObstacles = backdropObstacles;
	int ow = objW, oh = objH;
#ifdef FUSION_INTERNAL_ACCESS
	lx->winW = rhPtr->rh3.rh3WindowSx;
	lx->winH = rhPtr->rh3.rh3WindowSy;
	ow = lx->winW; oh = lx->winH;       // the object always covers the window; no need to size it in the editor
#endif
	// Manual display: without a size Fusion never calls Display() (field notes).
	if (ow > 0 && oh > 0) { rdPtr->get_rHo()->SetSize(ow, oh); lx->objW = ow; lx->objH = oh; }
	crumb("init: done, object size " + std::to_string(ow) + "x" + std::to_string(oh));
}

REFLAG Extension::LumenHandle() {
	if (!lx) return (REFLAG)0;
	LumenState& S = *lx;
	static int handleCalls = 0;
	if (handleCalls < 3) crumb("handle #" + std::to_string(++handleCalls));
	const auto t0 = std::chrono::steady_clock::now();
#ifdef FUSION_INTERNAL_ACCESS
	S.winW = rhPtr->rh3.rh3WindowSx;
	S.winH = rhPtr->rh3.rh3WindowSy;
	const int fw = rhPtr->rhFrame->hdr.Width, fh = rhPtr->rhFrame->hdr.Height;
	if (fw != S.frameW || fh != S.frameH) { S.frameW = fw; S.frameH = fh; S.engine.resizeFrame(fw, fh); }
#endif
	if (S.winW != S.objW || S.winH != S.objH) {   // window size changed: keep the object covering it
		rdPtr->get_rHo()->SetSize(S.winW, S.winH);
		S.objW = S.winW; S.objH = S.winH;
		S.forceRedraw = true;
	}
	lumen::View v;
	v.camX = rhPtr->get_WindowX();
	v.camY = rhPtr->get_WindowY();
	v.width = S.winW;
	v.height = S.winH;

	S.engine.updateAttachments([this](long o, float& x, float& y) { return objPos(*this, o, x, y); });
	S.engine.updateCasters([this](long o, lumen::IRect& r) { return objRect(*this, o, r); });

	const bool changed = S.engine.render(v);
	if (changed) S.maskDirty = true;
	const bool camMoved = v.camX != S.lastCamX || v.camY != S.lastCamY;
	if (camMoved) Runtime.SetPosition(v.camX, v.camY);   // keeps Fusion treating the object as on screen
	S.lastCamX = v.camX; S.lastCamY = v.camY;
	S.lastRenderMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	const bool redraw = changed || camMoved || S.forceRedraw;
	S.forceRedraw = false;
	return redraw ? REFLAG::DISPLAY : (REFLAG)0;
}

// Blend test: 4 rows x 9 columns of stretched 1x1 blits over the window, columns with Blend coefficient 0, 16 ... 128.
// By the SDK header coefficient 0 is solid black and 128 is invisible. Each row uses another flag combination, so one
// screenshot shows which combination really blends:
//   row 1: transparent=false, flags None        (the rectangles method's default)
//   row 2: transparent=true,  flags None
//   row 3: transparent=true,  flags CopyAlpha   (the SDK's default flags)
//   row 4: transparent=false, flags CopyAlpha
static void drawBlendTest(Extension& e, LumenState& S, Surface& D) {
	ensureDot(e, S);
	Surface& dot = *S.gfx->dot;
	const Rect one(0, 0, 1, 1);
	for (int row = 0; row < 4; ++row) {
		const bool transparent = (row == 1 || row == 2);
		const BlitOptions flags = (row >= 2) ? BlitOptions::CopyAlpha : BlitOptions::None;
		for (int col = 0; col <= 8; ++col) {
			const int x = 20 + col * 70, y = 20 + row * 60;
			dot.CopySection(one, D, Rect(x, y, x + 60, y + 50), transparent, flags, BlitOperation::Blend, (std::uint8_t)(col * 16));
		}
	}
}

REFLAG Extension::LumenDisplay() {
	if (!lx) return (REFLAG)0;
	LumenState& S = *lx;
	static int displayCalls = 0;
	const bool logIt = displayCalls < 3;
	if (logIt) { ++displayCalls; crumb("display #" + std::to_string(displayCalls) + ": begin"); }
	if (S.engine.pixels().empty()) return (REFLAG)0;
	if (S.maskDirty) { rebuildMask(*this, S); S.maskDirty = false; }
	if (!S.gfx || (S.strategy == M_RECTS ? !S.gfx->dot : !S.gfx->mask)) return (REFLAG)0;
	LumenGfx& G = *S.gfx;

	if (logIt) crumb("display: getting frame surface");
	Surface& D = Surface::CreateFromMainWindow(rhPtr);   // a reference to Fusion's frame surface
	// Camera read at draw time (no one-tick lag); the lightmap itself was built in Handle().
	const int d = S.engine.settings().divisor;
	const int ox = S.engine.originPxX() - rhPtr->get_WindowX(), oy = S.engine.originPxY() - rhPtr->get_WindowY();
	if (S.testPattern) { drawBlendTest(*this, S, D); return (REFLAG)0; }
	if (S.strategy == M_RECTS) {
		if (logIt) crumb("display: rectangles " + std::to_string(G.runs.size()));
		const Rect one(0, 0, 1, 1);
		const BlitOptions rectFlags = S.copyAlpha ? BlitOptions::CopyAlpha : BlitOptions::None;
		for (const Run& r : G.runs) {
			const double opacity = (double)r.q / S.levels;
			int coef = (int)std::lround((S.invertBlend ? opacity : 1.0 - opacity) * 128.0);   // Fusion's convention: 0 = opaque
			coef = (std::max)(0, (std::min)(128, coef));
			const Rect dst(ox + r.x0 * d, oy + r.y0 * d, ox + r.x1 * d, oy + r.y1 * d, true);
			G.dot->CopySection(one, D, dst, S.transparent, rectFlags, BlitOperation::Blend, (std::uint8_t)coef);
		}
	} else {
		// Rect(l, t, r, b) rejects negative coordinates unless allowNeg is true, and our destination can start off screen.
		const Rect src(0, 0, G.mw, G.mh);
		const Rect dst(ox, oy, ox + S.engine.blitWidth(), oy + S.engine.blitHeight(), true);
		BlitOptions flags = BlitOptions::AntiAliasing;
		if (S.copyAlpha) flags = flags | BlitOptions::CopyAlpha;
		if (logIt) crumb("display: blit " + std::to_string(G.mw) + "x" + std::to_string(G.mh) + " -> (" + std::to_string(ox) + "," + std::to_string(oy) + ") flags " + std::to_string((int)flags));
		G.mask->CopySection(src, D, dst, true, flags, BlitOperation::Copy, 0);
	}
	if (logIt) crumb("display: draw done");
	if (!S.strategyConfirmed) {   // the method survived a complete draw: remember it
		S.strategyConfirmed = true;
		if (S.forcedStrategy < 0) markMethod("ok", S.strategy);
		crumb(std::string("method: confirmed ") + methodName(S.strategy));
	}
	return (REFLAG)0;
}

// ------------------------------------------------------------------ helpers

static int lyr(int fusionLayer) { return (std::max)(0, fusionLayer - 1); }   // user layers are 1-based
static void fail(LumenState& S, const char* what, int id) {
	char b[96]; std::snprintf(b, sizeof b, "%s (light %d)", what, id); S.lastError = b;
}
static const TCHAR* text(LumenState& S, const std::string& s) {
	S.scratchW.assign(s.begin(), s.end());   // ASCII only
	return (const TCHAR*)S.scratchW.c_str();
}

// ------------------------------------------------------------------ actions

void Extension::SetAmbient(float p0) { lx->engine.setAmbient(p0); }

void Extension::CreateLight(int p0, float p1, float p2, float p3, float p4, int p5) {
	LumenState& S = *lx;
	int id = p0;
	if (id < 0) { while (S.engine.find(S.nextAutoId)) ++S.nextAutoId; id = S.nextAutoId++; }
	const lumen::Light* ex = S.engine.find(id);
	const bool isNew = ex == nullptr;
	lumen::Light l; if (ex) l = *ex;               // keep falloff / attachment when updating
	l.id = id; l.x = p1; l.y = p2;
	l.radius = (std::max)(1.0f, p3);
	l.brightness = (std::max)(0.0f, p4);
	l.layer = lyr(p5);
	if (!S.engine.setLight(l)) { S.lastError = "Light limit reached"; return; }
	S.lastLightId = id;
	if (isNew) Runtime.GenerateEvent(0);           // condition 0: On light created
}

void Extension::DeleteLight(int p0) { if (!lx->engine.removeLight(p0)) fail(*lx, "Delete: no such light", p0); }
void Extension::DeleteAllLights() { lx->engine.clearLights(); }
void Extension::SetLightPosition(int p0, float p1, float p2) { if (!lx->engine.setPosition(p0, p1, p2)) fail(*lx, "Move: no such light", p0); }
void Extension::SetLightRadius(int p0, float p1) { if (!lx->engine.setRadius(p0, p1)) fail(*lx, "Radius: no such light", p0); }
void Extension::SetLightBrightness(int p0, float p1) { if (!lx->engine.setBrightness(p0, p1)) fail(*lx, "Brightness: no such light", p0); }
void Extension::SetLightFalloff(int p0, float p1) { if (!lx->engine.setFalloff(p0, p1)) fail(*lx, "Falloff: no such light", p0); }
void Extension::SetLightEnabled(int p0, int p1) { if (!lx->engine.setEnabled(p0, p1 != 0)) fail(*lx, "Enable: no such light", p0); }

// Object parameters: Fusion calls these once for every selected instance of the chosen object.
void Extension::CreateLightOnObject(int p0, RunObject* p1, float p2, float p3, int p4, float p5, float p6) {
	if (!p1) return;
	const long fixed = Runtime.FixedFromRunObjPtr(p1);
	float ox = 0, oy = 0;
	if (!objPos(*this, fixed, ox, oy)) { lx->lastError = "Create light on object: object not found"; return; }
	CreateLight(p0, ox + p5, oy + p6, p2, p3, p4);
	lx->engine.attach(lx->lastLightId, fixed, p5, p6, true);   // follows the object; deleted when the object is destroyed
}

void Extension::AttachLightToObject(int p0, RunObject* p1, float p2, float p3) {
	if (!p1) return;
	if (!lx->engine.attach(p0, Runtime.FixedFromRunObjPtr(p1), p2, p3, true)) fail(*lx, "Attach: no such light", p0);
}

void Extension::DetachLight(int p0) { if (!lx->engine.detach(p0)) fail(*lx, "Detach: no such light", p0); }

void Extension::AddObstacleRect(int p0, int p1, int p2, int p3, int p4) {
	lx->engine.addObstacle(lyr(p0), lumen::IRect{ p1, p2, p1 + p3, p2 + p4 });
}
void Extension::ClearObstacles(int p0) { lx->engine.clearObstacles(lyr(p0)); }

void Extension::AddShadowCasterObject(RunObject* p0, int p1) {
	if (p0) lx->engine.addCaster(Runtime.FixedFromRunObjPtr(p0), lyr(p1));
}
void Extension::RemoveShadowCasterObject(RunObject* p0) {
	if (p0) lx->engine.removeCaster(Runtime.FixedFromRunObjPtr(p0));
}

void Extension::SetQuality(int p0) { lx->engine.setDivisor(p0); lx->maskDirty = true; lx->forceRedraw = true; }

void Extension::SetRenderOptions(int p0, int p1, int p2, int p3, int p4) {
	lx->forcedStrategy = (p0 >= 0 && p0 < M_COUNT) ? p0 : -1;
	lx->strategy = lx->forcedStrategy;               // -1 = choose again automatically
	lx->strategyConfirmed = lx->forcedStrategy >= 0; // forced methods are not remembered
	lx->levels = (std::max)(4, (std::min)(64, p1));
	lx->invertBlend = p2 != 0;
	lx->transparent = p3 != 0;
	lx->copyAlpha = p4 != 0;
	lx->maskDirty = true;
	lx->forceRedraw = true;
}

void Extension::ShowBlendTest(int p0) {
	lx->testPattern = p0 != 0;
	lx->forceRedraw = true;
}

// ------------------------------------------------------------------ conditions

bool Extension::OnLightCreated() { return true; }
bool Extension::LightExists(int p0) { return lx->engine.find(p0) != nullptr; }

// ------------------------------------------------------------------ expressions

int Extension::LightCount() { return (int)lx->engine.lightCount(); }
int Extension::LastLightID() { return lx->lastLightId; }
float Extension::LightX(int p0) { auto* l = lx->engine.find(p0); return l ? l->x : 0.0f; }
float Extension::LightY(int p0) { auto* l = lx->engine.find(p0); return l ? l->y : 0.0f; }
float Extension::LightRadius(int p0) { auto* l = lx->engine.find(p0); return l ? l->radius : 0.0f; }
float Extension::LightBrightness(int p0) { auto* l = lx->engine.find(p0); return l ? l->brightness : 0.0f; }
float Extension::Ambient() { return lx->engine.ambient(); }

const TCHAR* Extension::DebugInfo() {
	LumenState& S = *lx;
	const lumen::Stats& st = S.engine.stats();
	char b[520];
	std::snprintf(b, sizeof b,
		"lights %d (visible %d, rebuilt %d), rays %ld, composes %d, lightmap %dx%d at pixel size %d, "
		"window %dx%d, handle %.2f ms, mask write %.2f ms, method %s%s (blits %d, levels %d), alpha pitch %d, transparent %d, copyalpha %d",
		st.lightsTotal, st.lightsVisible, st.lightsRecomputed, st.raysCast, st.composes,
		S.engine.mapWidth(), S.engine.mapHeight(), S.engine.settings().divisor,
		S.winW, S.winH, S.lastRenderMs, S.lastMaskMs, methodName(S.strategy), S.strategyConfirmed ? " (confirmed)" : "",
		S.runCount, S.levels, S.alphaPitch, S.transparent ? 1 : 0, S.copyAlpha ? 1 : 0);
	return text(S, b);
}

const TCHAR* Extension::LastError() { return text(*lx, lx->lastError); }
