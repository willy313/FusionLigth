#include "Common.hpp"
#include <cstdio>
#include <cstring>

namespace DarkEdif { std::vector<Blit> g_blits; int g_negViolations = 0; int g_alphaWriteOnOpaqueSurface = 0; const Surface* g_lastBlitSource = nullptr; }
using DarkEdif::g_blits;
using DarkEdif::BlitOperation;
using DarkEdif::BlitOptions;
static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static RunObject makeObj(int fixed, int x, int y, int w, int h, int sx, int sy) {
	RunObject o; o.fixed = fixed; o.ho.x = x; o.ho.y = y; o.ho.imgW = w; o.ho.imgH = h; o.ho.spotX = sx; o.ho.spotY = sy; return o;
}
static unsigned alphaOf(std::uint32_t c) { return c >> 24; }

int main() {
	Frame frame; RunHeader rh; rh.rhFrame = &frame;
	EDITDATA ed;
	RunObject self; HeaderObject& ho = self.ho;
	Extension e(&rh, &self, &ed);

	// constructed from properties: "Pixel size" = 2
	CHECK(e.lx && e.lx->engine.settings().divisor == 2 && e.lx->winW == 640);
	// manual display: the object is sized to the window (not the editor size) so Fusion calls Display()
	CHECK(ho.sizeW == 640 && ho.sizeH == 480 && ho.setSizeCalls == 1);

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

	// one blit of one per-pixel-alpha surface; alpha is applied (not copied) and edges are smoothed
	CHECK(g_blits.size() == 1);
	const auto& b0 = g_blits[0];
	CHECK(b0.op == BlitOperation::Copy);
	CHECK(b0.flags == BlitOptions::AntiAliasing);
	CHECK(b0.srcW == e.lx->engine.mapWidth() && b0.srcH == e.lx->engine.mapHeight());
	CHECK(b0.dr - b0.dl == e.lx->engine.blitWidth() && b0.db - b0.dt == e.lx->engine.blitHeight());
	CHECK(DarkEdif::g_alphaWriteOnOpaqueSurface == 0);        // the mask surface was created with alpha = true
	const DarkEdif::Surface* mask = DarkEdif::g_lastBlitSource;
	CHECK(mask && mask->hasAlpha && mask->w == (size_t)e.lx->engine.mapWidth());
	// alpha = darkness: bright at the light, fully dark far away, nothing else in the mask is non-black
	const int cx = (210 / 2) - e.lx->engine.originPxX() / 2, cy = (150 / 2) - e.lx->engine.originPxY() / 2;
	CHECK(alphaOf(mask->pix[(size_t)cy * mask->w + cx]) < 20);
	CHECK(alphaOf(mask->pix[0]) == 255);
	bool allBlack = true; for (auto c : mask->pix) if ((c & 0x00FFFFFF) != 0) allBlack = false;
	CHECK(allBlack);
	CHECK(e.LumenHandle() == REFLAG(0));                       // nothing changed

	rh.winX = 3;                                               // camera moved: redraw requested
	CHECK(e.LumenHandle() == REFLAG::DISPLAY);
	CHECK(e.Runtime.posCalls == 2 && e.Runtime.posX == 3);
	g_blits.clear(); e.LumenDisplay();
	CHECK(g_blits.size() == 1 && g_blits[0].dl == e.lx->engine.originPxX() - 3);   // camera read at draw time
	rh.winX = 5;                                               // scrolled again before drawing: no lag
	g_blits.clear(); e.LumenDisplay();
	CHECK(g_blits.size() == 1 && g_blits[0].dl == e.lx->engine.originPxX() - 5);
	rh.winX = 3;
	rh.rh3.rh3WindowSx = 800;                                  // window resized: object follows
	CHECK(e.LumenHandle() == REFLAG::DISPLAY && ho.sizeW == 800 && ho.setSizeCalls == 2);
	rh.rh3.rh3WindowSx = 640; e.LumenHandle();

	// ambient lifts darkness
	e.SetAmbient(1.0f);
	CHECK(e.LumenHandle() == REFLAG::DISPLAY);
	g_blits.clear(); e.LumenDisplay();
	CHECK(alphaOf(DarkEdif::g_lastBlitSource->pix[0]) == 0);   // fully lit: transparent mask
	e.SetAmbient(0.0f); e.LumenHandle();

	// obstacle rectangles and layers
	e.AddObstacleRect(1, 230, 100, 16, 100);
	CHECK(e.LumenHandle() == REFLAG::DISPLAY);
	e.ClearObstacles(1);
	CHECK(e.LumenHandle() == REFLAG::DISPLAY);

	// render options + quality + errors
	e.SetRenderOptions(0, 1); g_blits.clear(); e.LumenDisplay();
	CHECK(g_blits.size() == 1 && g_blits[0].flags == BlitOptions::CopyAlpha);
	e.SetRenderOptions(1, 0); g_blits.clear(); e.LumenDisplay();
	CHECK(g_blits[0].flags == BlitOptions::AntiAliasing);
	e.SetQuality(4); CHECK(e.LumenHandle() == REFLAG::DISPLAY && e.lx->engine.settings().divisor == 4);
	e.LumenDisplay();
	e.SetLightRadius(999, 10);
	CHECK(std::wcsstr(e.LastError(), L"no such light") != nullptr);
	CHECK(std::wcsstr(e.DebugInfo(), L"lights 2") != nullptr);
	e.DeleteLight(1); CHECK(e.LightCount() == 1);
	e.DeleteAllLights(); CHECK(e.LightCount() == 0);

	// ---- objects (Object parameters: the action receives each RunObject*)
	e.SetQuality(2); e.LumenHandle();
	RunObject player = makeObj(0x10001, 300, 200, 32, 32, 16, 16);
	e.Runtime.objects[player.fixed] = &player;
	e.CreateLightOnObject(5, &player, 90, 1.0f, 1, 0, -10);
	CHECK(e.LightExists(5) && e.LightX(5) == 300.0f && e.LightY(5) == 190.0f);
	player.ho.x = 350; e.LumenHandle();
	CHECK(e.LightX(5) == 350.0f);                              // follows the object
	e.CreateLightOnObject(-1, nullptr, 90, 1.0f, 1, 0, 0);     // null object: ignored, no crash
	CHECK(e.LightCount() == 1);

	RunObject wall = makeObj(0x10002, 420, 200, 20, 100, 10, 50);   // frame rect: x 410..430, y 150..250
	e.Runtime.objects[wall.fixed] = &wall;
	e.AddShadowCasterObject(&wall, 1);
	e.LumenHandle();
	CHECK(e.lx->engine.grid().solid(0, 410 / 8 + 1, 200 / 8));  // object bounds became obstacle cells
	e.CreateLight(6, 380, 200, 120, 1.0f, 1);                  // light left of the wall
	e.LumenHandle();
	CHECK(e.lx->engine.darknessAtWorld(480, 200) == 255);       // shadow behind the wall
	CHECK(e.lx->engine.darknessAtWorld(400, 200) < 255);        // lit in front of it
	wall.ho.y = 800; e.LumenHandle();                           // wall moves away: caster follows
	CHECK(e.lx->engine.darknessAtWorld(480, 200) < 255);
	e.RemoveShadowCasterObject(&wall); e.LumenHandle();
	CHECK(!e.lx->engine.grid().solid(0, 410 / 8 + 1, 800 / 8));
	e.Runtime.objects.erase(player.fixed); e.LumenHandle();
	CHECK(!e.LightExists(5));                                  // deleted with the object
	e.AttachLightToObject(6, &player, 0, 0);                   // attach (object registered again below)
	e.Runtime.objects[player.fixed] = &player; player.ho.x = 100; player.ho.y = 100;
	e.AttachLightToObject(6, &player, 5, 5); e.LumenHandle();
	CHECK(e.LightX(6) == 105.0f && e.LightY(6) == 105.0f);
	e.DetachLight(6); player.ho.x = 500; e.LumenHandle();
	CHECK(e.LightX(6) == 105.0f);                              // detached lights stay where they are

	CHECK(DarkEdif::g_negViolations == 0);                     // negative rect origins are always passed with allowNeg
	std::printf("%d checks, %d failed\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
