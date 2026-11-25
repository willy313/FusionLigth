#include "Common.hpp"
#include <cstdio>
#include <cstring>

namespace DarkEdif { std::vector<Blit> g_blits; int g_negViolations = 0; bool g_lastSetPixelIncludedAlpha = false; }
using DarkEdif::g_blits;
using DarkEdif::BlitOperation;
static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main() {
	Frame frame; RunHeader rh; rh.rhFrame = &frame;
	EDITDATA ed;
	RunObject self; HeaderObject& ho = self.ho;
	Extension e(&rh, &self, &ed);

	// constructed from properties: "Pixel size" = 2
	CHECK(e.lx && e.lx->engine.settings().divisor == 2 && e.lx->winW == 640);
	// manual display: the object is sized to the window (not the editor size) so Fusion calls Display()
	CHECK(ho.sizeW == 640 && ho.sizeH == 480 && ho.setSizeCalls == 1);
	CHECK(e.lx->engine.stats().lightsTotal == 0);

	e.SetAmbient(0.0f);
	e.CreateLight(1, 200, 150, 100, 1.0f, 1);
	CHECK(e.Runtime.events == 1 && e.LastLightID() == 1 && e.LightCount() == 1);
	e.CreateLight(1, 210, 150, 100, 1.0f, 1);                 // update: no second event
	CHECK(e.Runtime.events == 1 && e.LightX(1) == 210.0f);
	e.CreateLight(-1, 400, 300, 60, 0.5f, 1);                  // auto id
	CHECK(e.LastLightID() != 1 && e.LightCount() == 2 && e.Runtime.events == 2);
	CHECK(e.LightExists(1) && !e.LightExists(77));

	CHECK(e.LumenHandle() == REFLAG::DISPLAY);
	CHECK(e.Runtime.posCalls == 1 && e.Runtime.posX == 0 && e.Runtime.posY == 0);   // object moved to the window origin
	g_blits.clear();
	CHECK(e.LumenDisplay() == REFLAG(0));
	CHECK(!g_blits.empty());
	bool sawCopy = false, sawBlend = false;
	for (auto& b : g_blits) {
		(b.op == BlitOperation::Copy ? sawCopy : sawBlend) = true;
		CHECK(b.coef >= 0 && b.coef <= 128);
		CHECK(b.dr - b.dl == e.lx->engine.blitWidth() && b.db - b.dt == e.lx->engine.blitHeight());
		CHECK(b.srcW == e.lx->engine.mapWidth() && b.srcH == e.lx->engine.mapHeight());
	}
	CHECK(sawCopy && sawBlend);                                // fully dark band + partial bands
	CHECK(g_blits[0].flags == DarkEdif::BlitOptions::CopyAlpha);   // SDK default flag unless changed
	CHECK(!DarkEdif::g_lastSetPixelIncludedAlpha);             // SetPixel always called with includeAlpha=false
	CHECK(e.LumenHandle() == REFLAG(0));                       // nothing changed
	rh.winX = 3;                                               // camera moved: redraw requested
	CHECK(e.LumenHandle() == REFLAG::DISPLAY);
	CHECK(e.Runtime.posCalls == 2 && e.Runtime.posX == 3);
	g_blits.clear(); e.LumenDisplay();
	CHECK(!g_blits.empty() && g_blits[0].dl == e.lx->engine.originPxX() - 3);   // camera read at draw time
	rh.winX = 5;                                               // scrolled again before drawing: no lag
	g_blits.clear(); e.LumenDisplay();
	CHECK(!g_blits.empty() && g_blits[0].dl == e.lx->engine.originPxX() - 5);
	rh.winX = 3;
	rh.rh3.rh3WindowSx = 800;                                  // window resized: object follows
	CHECK(e.LumenHandle() == REFLAG::DISPLAY && ho.sizeW == 800 && ho.setSizeCalls == 2);
	rh.rh3.rh3WindowSx = 640; e.LumenHandle();

	// ambient lifts darkness: no fully dark band any more
	e.SetAmbient(1.0f);
	CHECK(e.LumenHandle() == REFLAG::DISPLAY);
	g_blits.clear(); e.LumenDisplay();
	CHECK(g_blits.empty());                                    // fully lit: nothing to draw
	e.SetAmbient(0.0f); e.LumenHandle();

	// obstacles and layers
	e.AddObstacleRect(1, 230, 100, 16, 100);
	CHECK(e.LumenHandle() == REFLAG::DISPLAY);
	e.ClearObstacles(1);
	CHECK(e.LumenHandle() == REFLAG::DISPLAY);

	// debug options, quality, errors
	e.SetRenderOptions(8, 1, 0); CHECK(e.lx->shadeLevels == 8 && e.lx->invertBlend && !e.lx->copyAlpha);
	e.SetRenderOptions(1000, 0, 1); CHECK(e.lx->shadeLevels == 64 && e.lx->copyAlpha);
	e.SetQuality(4); CHECK(e.LumenHandle() == REFLAG::DISPLAY && e.lx->engine.settings().divisor == 4);
	e.LumenDisplay();
	e.SetLightRadius(999, 10);
	CHECK(std::wcsstr(e.LastError(), L"no such light") != nullptr);
	CHECK(std::wcsstr(e.DebugInfo(), L"lights 2") != nullptr);
	e.DeleteLight(1); CHECK(e.LightCount() == 1);
	e.DeleteAllLights(); CHECK(e.LightCount() == 0);

#ifdef LUMEN_OBJECT_ACCESS
	RunObject player; player.ho.x = 300; player.ho.y = 200; e.Runtime.objects[0x10001] = &player;
	e.CreateLightOnObject(5, 0x10001, 90, 1.0f, 1, 0, -10);
	CHECK(e.LightExists(5) && e.LightX(5) == 300.0f && e.LightY(5) == 190.0f);
	player.ho.x = 350; e.LumenHandle();
	CHECK(e.LightX(5) == 350.0f);                              // follows the object
	e.AddShadowCasterObject(0x10002, 1);                       // unknown object: dropped quietly
	e.Runtime.objects.erase(0x10001); e.LumenHandle();
	CHECK(!e.LightExists(5));                                  // deleted with the object
#else
	e.CreateLightOnObject(5, 1, 90, 1.0f, 1, 0, 0);
	CHECK(!e.LightExists(5) && std::wcsstr(e.LastError(), L"not enabled") != nullptr);
#endif
	CHECK(DarkEdif::g_negViolations == 0);                     // negative rect origins are always passed with allowNeg
	std::printf("%d checks, %d failed\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
