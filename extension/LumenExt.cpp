// Lumen: the Fusion-facing layer. Everything here that touches DarkEdif was taken from the
// "what works" list in the field notes; anything not on that list sits behind a build flag
// (LUMEN_OBJECT_ACCESS) and is marked UNVERIFIED.
#include "Common.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static_assert(sizeof(TCHAR) == sizeof(wchar_t), "Lumen is built for the Unicode configurations only");

// Works whether CreateFromMainWindow() hands back a pointer, a smart pointer or a value.
template <class T> static auto deref(T& t, int) -> decltype(*t) { return *t; }
template <class T> static T& deref(T& t, long) { return t; }

// ------------------------------------------------------------------ graphics

struct LumenGfx {
	// Darkness is drawn as `levels` CPU surfaces, one per opacity band. Each holds black pixels where
	// the lightmap darkness falls in that band and a magenta colour key elsewhere, and is blitted with a
	// uniform Blend coefficient. This uses only calls the field notes confirm work (SetPixel without
	// alpha, colour-key transparency, CopySection with Blend); no per-pixel alpha is required.
	std::vector<std::unique_ptr<Surface>> bands;   // index 1..levels
	std::vector<int> count;
	std::vector<char> dirty;
	int mw = 0, mh = 0, levels = 0;
};

LumenState::~LumenState() { delete gfx; }

static void ensureBands(Extension& e, LumenState& S, int mw, int mh) {
	if (!S.gfx) S.gfx = new LumenGfx();
	LumenGfx& G = *S.gfx;
	if (G.mw == mw && G.mh == mh && G.levels == S.shadeLevels && !G.bands.empty()) return;
	G.bands.clear();
	G.bands.resize(S.shadeLevels + 1);
	G.count.assign(S.shadeLevels + 1, 0);
	G.dirty.assign(S.shadeLevels + 1, 0);
	for (int b = 1; b <= S.shadeLevels; ++b) {
		G.bands[b] = std::make_unique<Surface>(e.rhPtr, true, false, mw, mh, false);
		G.bands[b]->FillImageWith(SurfaceFill::Solid(ColorRGB(255, 0, 255)));
		G.bands[b]->SetTransparencyToColor(ColorRGB(255, 0, 255) & 0x00FFFFFF);
	}
	G.mw = mw; G.mh = mh; G.levels = S.shadeLevels;
	S.bandsDirty = true;
}

static void rebuildBands(Extension& e, LumenState& S) {
	const std::vector<uint32_t>& px = S.engine.pixels();
	const int mw = S.engine.mapWidth(), mh = S.engine.mapHeight();
	if (px.size() != (size_t)mw * mh) return;
	ensureBands(e, S, mw, mh);
	LumenGfx& G = *S.gfx;
	const int K = G.levels;
	for (int b = 1; b <= K; ++b) {
		if (G.dirty[b]) {
			G.bands[b]->FillImageWith(SurfaceFill::Solid(ColorRGB(255, 0, 255)));
			G.dirty[b] = 0;
		}
		G.count[b] = 0;
	}
	const auto black = ColorRGB(0, 0, 0);
	for (int y = 0; y < mh; ++y)
		for (int x = 0; x < mw; ++x) {
			const int a = (int)(px[(size_t)y * mw + x] >> 24);
			const int b = (a * K + 127) / 255;
			if (b <= 0) continue;
			G.bands[b]->SetPixel(x, y, black, false);
			++G.count[b];
			G.dirty[b] = 1;
		}
}

// ------------------------------------------------------------------ frame loop

#ifdef LUMEN_OBJECT_ACCESS
// UNVERIFIED: Runtime.RunObjPtrFromFixed and the hoRect / get_X accessors are written from memory of the
// DarkEdif headers and have not been compiled. Enable with the workflow's "object_access" input, then
// read the compile errors if any.
static bool lumenObjPos(Extension& e, long fixed, float& x, float& y) {
	RunObject* ro = e.Runtime.RunObjPtrFromFixed((int)fixed);
	if (!ro) return false;
	x = (float)ro->get_rHo()->get_X();
	y = (float)ro->get_rHo()->get_Y();
	return true;
}
static bool lumenObjRect(Extension& e, long fixed, const lumen::View& v, lumen::IRect& r) {
	RunObject* ro = e.Runtime.RunObjPtrFromFixed((int)fixed);
	if (!ro) return false;
	const RECT rc = ro->get_rHo()->hoRect;   // window coordinates per the field notes; converted to frame coordinates
	r = { (int)rc.left + v.camX, (int)rc.top + v.camY, (int)rc.right + v.camX, (int)rc.bottom + v.camY };
	return true;
}
#endif

void Extension::LumenInit(bool backdropObstacles, const std::wstring& quality) {
	lumen::Engine::Settings st;
	st.divisor = quality.find(L"Full") != std::wstring::npos ? 1
		: quality.find(L"Quarter") != std::wstring::npos ? 4
		: quality.find(L"Eighth") != std::wstring::npos ? 8 : 2;
	int fw = 1024, fh = 768;
#ifdef FUSION_INTERNAL_ACCESS
	fw = rhPtr->rhFrame->hdr.Width;
	fh = rhPtr->rhFrame->hdr.Height;
#endif
	lx = std::make_unique<LumenState>(fw, fh, st);
	lx->backdropObstacles = backdropObstacles;
#ifdef FUSION_INTERNAL_ACCESS
	lx->winW = rhPtr->rh3.rh3WindowSx;
	lx->winH = rhPtr->rh3.rh3WindowSy;
#endif
}

REFLAG Extension::LumenHandle() {
	if (!lx) return (REFLAG)0;
	LumenState& S = *lx;
	const auto t0 = std::chrono::steady_clock::now();
#ifdef FUSION_INTERNAL_ACCESS
	S.winW = rhPtr->rh3.rh3WindowSx;
	S.winH = rhPtr->rh3.rh3WindowSy;
	const int fw = rhPtr->rhFrame->hdr.Width, fh = rhPtr->rhFrame->hdr.Height;
	if (fw != S.frameW || fh != S.frameH) { S.frameW = fw; S.frameH = fh; S.engine.resizeFrame(fw, fh); }
#endif
	lumen::View v;
	v.camX = rhPtr->get_WindowX();
	v.camY = rhPtr->get_WindowY();
	v.width = S.winW;
	v.height = S.winH;
#ifdef LUMEN_OBJECT_ACCESS
	S.engine.updateAttachments([this](long o, float& x, float& y) { return lumenObjPos(*this, o, x, y); });
	S.engine.updateCasters([this, &v](long o, lumen::IRect& r) { return lumenObjRect(*this, o, v, r); });
#endif
	const bool changed = S.engine.render(v);
	if (changed) S.bandsDirty = true;
	const bool camMoved = v.camX != S.lastCamX || v.camY != S.lastCamY;
	S.lastCamX = v.camX; S.lastCamY = v.camY;
	S.lastRenderMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	const bool redraw = changed || camMoved || S.forceRedraw;
	S.forceRedraw = false;
	return redraw ? REFLAG::DISPLAY : (REFLAG)0;
}

REFLAG Extension::LumenDisplay() {
	if (!lx) return (REFLAG)0;
	LumenState& S = *lx;
	if (S.engine.pixels().empty()) return (REFLAG)0;
	if (S.bandsDirty) { rebuildBands(*this, S); S.bandsDirty = false; }
	if (!S.gfx || S.gfx->bands.empty()) return (REFLAG)0;
	LumenGfx& G = *S.gfx;

	auto dest = Surface::CreateFromMainWindow(rhPtr);
	auto& D = deref(dest, 0);
	const int x0 = S.engine.blitX(), y0 = S.engine.blitY();
	const int x1 = x0 + S.engine.blitWidth(), y1 = y0 + S.engine.blitHeight();
	const int K = G.levels;
	S.bandBlits = 0;
	for (int b = 1; b <= K; ++b) {
		if (!G.count[b]) continue;
		++S.bandBlits;
		if (b == K) {   // fully dark: plain colour-keyed copy, independent of blend-coefficient direction
			G.bands[b]->CopySection({ 0, 0, G.mw, G.mh }, D, { x0, y0, x1, y1 }, true, BlitOptions::None, BlitOperation::Copy, 0);
			continue;
		}
		const double opacity = (double)b / K;
		int coef = (int)std::lround((S.invertBlend ? opacity : 1.0 - opacity) * 128.0);
		coef = (std::max)(0, (std::min)(128, coef));
		G.bands[b]->CopySection({ 0, 0, G.mw, G.mh }, D, { x0, y0, x1, y1 }, true, BlitOptions::None, BlitOperation::Blend, coef);
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

void Extension::AttachLightToObject(int p0, int p1, float p2, float p3, int p4) {
#ifdef LUMEN_OBJECT_ACCESS
	if (!lx->engine.attach(p0, p1, p2, p3, p4 != 0)) fail(*lx, "Attach: no such light", p0);
#else
	(void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
	lx->lastError = "Object access is not enabled in this build (see README, stage 2)";
#endif
}

void Extension::DetachLight(int p0) { if (!lx->engine.detach(p0)) fail(*lx, "Detach: no such light", p0); }

void Extension::CreateLightOnObject(int p0, int p1, float p2, float p3, int p4, float p5, float p6) {
#ifdef LUMEN_OBJECT_ACCESS
	float ox = 0, oy = 0;
	if (!lumenObjPos(*this, p1, ox, oy)) { lx->lastError = "CreateLightOnObject: object not found"; return; }
	CreateLight(p0, ox + p5, oy + p6, p2, p3, p4);
	AttachLightToObject(lx->lastLightId, p1, p5, p6, 1);
#else
	(void)p0; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5; (void)p6;
	lx->lastError = "Object access is not enabled in this build (see README, stage 2)";
#endif
}

void Extension::AddObstacleRect(int p0, int p1, int p2, int p3, int p4) {
	lx->engine.addObstacle(lyr(p0), lumen::IRect{ p1, p2, p1 + p3, p2 + p4 });
}
void Extension::ClearObstacles(int p0) { lx->engine.clearObstacles(lyr(p0)); }

void Extension::AddShadowCasterObject(int p0, int p1) {
#ifdef LUMEN_OBJECT_ACCESS
	lx->engine.addCaster(p0, lyr(p1));
#else
	(void)p0; (void)p1;
	lx->lastError = "Object access is not enabled in this build (see README, stage 2)";
#endif
}
void Extension::RemoveShadowCasterObject(int p0) {
#ifdef LUMEN_OBJECT_ACCESS
	lx->engine.removeCaster(p0);
#else
	(void)p0;
#endif
}

void Extension::SetQuality(int p0) { lx->engine.setDivisor(p0); lx->bandsDirty = true; lx->forceRedraw = true; }

void Extension::SetRenderOptions(int p0, int p1) {
	lx->shadeLevels = (std::max)(2, (std::min)(64, p0));
	lx->invertBlend = p1 != 0;
	lx->bandsDirty = true; lx->forceRedraw = true;
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
	char b[400];
	std::snprintf(b, sizeof b,
		"lights %d (visible %d, rebuilt %d), rays %ld, composes %d, map %dx%d div %d, levels %d, band blits %d, "
		"blit %d,%d %dx%d, handle %.2f ms, object access %s",
		st.lightsTotal, st.lightsVisible, st.lightsRecomputed, st.raysCast, st.composes,
		S.engine.mapWidth(), S.engine.mapHeight(), S.engine.settings().divisor, S.shadeLevels, S.bandBlits,
		S.engine.blitX(), S.engine.blitY(), S.engine.blitWidth(), S.engine.blitHeight(), S.lastRenderMs,
#ifdef LUMEN_OBJECT_ACCESS
		"on"
#else
		"off"
#endif
	);
	return text(S, b);
}

const TCHAR* Extension::LastError() { return text(*lx, lx->lastError); }
