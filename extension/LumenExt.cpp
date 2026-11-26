// Lumen: the Fusion-facing layer. Every SDK call here was checked against the SDK headers uploaded from a CI run
// (SurfaceMultiPlat.hpp, MMFWindowsMasterHeader.hpp, Edif.hpp).
#include "Common.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static_assert(sizeof(TCHAR) == sizeof(wchar_t), "Lumen is built for the Unicode configurations only");

// Surface, SurfaceFill, ColorRGB, BlitOptions, Point and Rect live in the DarkEdif namespace in this SDK.
using namespace DarkEdif;

// ------------------------------------------------------------------ graphics

// The darkness is ONE CPU surface: RGB all black plus a separate ALPHA PLANE holding the darkness. SetPixel only moves
// colour, not alpha (field notes: "GetPixel/SetPixel move colour only, not alpha"), so the alpha plane is written
// directly through GetAlphaBytes(). CopySection applies the source alpha unless BlitOptions::CopyAlpha is set (that
// flag copies the alpha channel to the destination instead of applying it).
struct LumenGfx {
	std::unique_ptr<Surface> mask;
	int mw = 0, mh = 0;
};

LumenState::~LumenState() { delete gfx; }

static void ensureMask(Extension& e, LumenState& S, int mw, int mh) {
	if (!S.gfx) S.gfx = new LumenGfx();
	LumenGfx& G = *S.gfx;
	if (G.mask && G.mw == mw && G.mh == mh) return;
	G.mask = std::make_unique<Surface>(e.rhPtr, true, false, mw, mh, true);   // alpha = true: per-pixel transparency
	G.mask->FillImageWith(SurfaceFill::Solid(ColorRGB(0, 0, 0)));               // RGB is black everywhere, forever
	G.mw = mw; G.mh = mh;
	S.maskHasAlpha = G.mask->HasAlpha();
	S.maskDirty = true;
}

static void rebuildMask(Extension& e, LumenState& S) {
	const auto t0 = std::chrono::steady_clock::now();
	const std::vector<uint32_t>& px = S.engine.pixels();
	const int mw = S.engine.mapWidth(), mh = S.engine.mapHeight();
	if (px.size() != (size_t)mw * mh) return;
	ensureMask(e, S, mw, mh);
	Surface& m = *S.gfx->mask;
	m.SetTransparencyToAlpha();                     // transparency comes from the alpha plane, not from a colour key
	if (!m.HasAlpha()) { S.lastError = "Lumen mask surface has no alpha plane"; return; }
	{
		auto lock = m.GetAlphaBytes();                // locks the alpha plane until `lock` goes out of scope
		std::uint8_t* alpha = *lock;
		const int pitch = m.GetAlphaPitch();          // bytes per row, may include padding
		if (!alpha) { S.lastError = "Lumen could not lock the mask alpha plane"; return; }
		S.alphaPitch = pitch;
		for (int y = 0; y < mh; ++y) {
			std::uint8_t* row = alpha + (std::ptrdiff_t)y * pitch;
			const uint32_t* src = &px[(size_t)y * mw];
			for (int x = 0; x < mw; ++x)
				row[x] = (std::uint8_t)(src[x] >> 24);   // darkness: 0 = transparent (lit), 255 = opaque black
		}
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
	lumen::Engine::Settings st;
	const long ps = std::wcstol(pixelSize.c_str(), nullptr, 10);
	st.divisor = (ps >= 1 && ps <= 8) ? (int)ps : 2;
	int fw = 1024, fh = 768;
#ifdef FUSION_INTERNAL_ACCESS
	fw = rhPtr->rhFrame->hdr.Width;
	fh = rhPtr->rhFrame->hdr.Height;
#endif
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

REFLAG Extension::LumenDisplay() {
	if (!lx) return (REFLAG)0;
	LumenState& S = *lx;
	if (S.engine.pixels().empty()) return (REFLAG)0;
	if (S.maskDirty) { rebuildMask(*this, S); S.maskDirty = false; }
	if (!S.gfx || !S.gfx->mask) return (REFLAG)0;
	LumenGfx& G = *S.gfx;

	Surface& D = Surface::CreateFromMainWindow(rhPtr);   // a reference to Fusion's frame surface
	// Camera read at draw time (no one-tick lag); the lightmap itself was built in Handle().
	const int x0 = S.engine.originPxX() - rhPtr->get_WindowX(), y0 = S.engine.originPxY() - rhPtr->get_WindowY();
	// Rect(l, t, r, b) rejects negative coordinates unless allowNeg is true, and our destination can start off screen.
	const Rect src(0, 0, G.mw, G.mh);
	const Rect dst(x0, y0, x0 + S.engine.blitWidth(), y0 + S.engine.blitHeight(), true);
	BlitOptions flags = S.smooth ? BlitOptions::AntiAliasing : BlitOptions::None;
	if (S.copyAlpha) flags = flags | BlitOptions::CopyAlpha;
	G.mask->CopySection(src, D, dst, true, flags, BlitOperation::Copy, 0);
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

void Extension::SetRenderOptions(int p0, int p1) {
	lx->smooth = p0 != 0;
	lx->copyAlpha = p1 != 0;
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
	char b[480];
	std::snprintf(b, sizeof b,
		"lights %d (visible %d, rebuilt %d), rays %ld, composes %d, lightmap %dx%d at pixel size %d, "
		"window %dx%d, handle %.2f ms, mask write %.2f ms, mask has alpha %d (pitch %d), smooth %d, copyalpha %d",
		st.lightsTotal, st.lightsVisible, st.lightsRecomputed, st.raysCast, st.composes,
		S.engine.mapWidth(), S.engine.mapHeight(), S.engine.settings().divisor,
		S.winW, S.winH, S.lastRenderMs, S.lastMaskMs, S.maskHasAlpha ? 1 : 0, S.alphaPitch,
		S.smooth ? 1 : 0, S.copyAlpha ? 1 : 0);
	return text(S, b);
}

const TCHAR* Extension::LastError() { return text(*lx, lx->lastError); }
