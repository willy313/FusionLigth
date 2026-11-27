#include "Common.hpp"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

namespace DarkEdif { std::vector<Blit> g_blits; int g_negViolations = 0; bool g_blitWhileLocked = false; int g_pngLoads = 0; std::wstring g_lastPngPath; const Surface* g_lastBlitSource = nullptr; }
using DarkEdif::g_blits;
using DarkEdif::BlitOperation;
using DarkEdif::BlitOptions;
static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static RunObject makeObj(int fixed, int x, int y, int w, int h, int sx, int sy) {
	RunObject o; o.fixed = fixed; o.ho.x = x; o.ho.y = y; o.ho.imgW = w; o.ho.imgH = h; o.ho.spotX = sx; o.ho.spotY = sy; return o;
}

static std::string slurp(const std::string& p) { std::ifstream f(p); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
static std::string makeDir(const char* name) {
	std::string d = (std::filesystem::temp_directory_path() / name).string();
	std::filesystem::remove_all(d); std::filesystem::create_directories(d); return d;
}

int main() {
	const std::string dir1 = makeDir("lumen_t1");
	setenv("TMPDIR", dir1.c_str(), 1);
	Frame frame; RunHeader rh; rh.rhFrame = &frame;
	EDITDATA ed;
	RunObject self; HeaderObject& ho = self.ho;
	Extension e(&rh, &self, &ed);

	e.SetRenderOptions(0, 16, 0, -1, -1, -1);                  // this flow checks the alpha-lock method; auto-selection is tested below
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
	CHECK(e.lx->strategy == 0 && e.lx->strategyConfirmed);     // forced methods are not written to the method file
	const DarkEdif::Surface* mask = DarkEdif::g_lastBlitSource;
	CHECK(mask && mask->hasAlpha && mask->w == (size_t)e.lx->engine.mapWidth());
	CHECK(!mask->transparencyFromAlpha && mask->setAlphaCalls == 0);   // neither SetTransparencyToAlpha nor SetAlpha (SetAlpha crashed)
	CHECK(mask->alphaLocks >= 1);                              // alpha plane written through the lock
	CHECK(mask->pixelsSet == 0);                               // SetPixel moves colour only, so it is never used
	CHECK(!DarkEdif::g_blitWhileLocked && !mask->lockedAlpha); // alpha plane unlocked before the blit
	CHECK(mask->lastFill == DarkEdif::ColorRGB(0, 0, 0));      // RGB is black
	// the ALPHA PLANE holds the darkness (rows are padded: index with the pitch, not the width)
	CHECK(mask->alphaPitch % 4 == 0);
	auto A = [&](int wx, int wy) {
		const int gx = wx / 2 - e.lx->engine.originPxX() / 2, gy = wy / 2 - e.lx->engine.originPxY() / 2;
		return (unsigned)mask->alphaPlane[(size_t)gy * mask->alphaPitch + gx];
	};
	CHECK(A(210, 150) < 20);                                   // bright at the light
	CHECK(A(0, 0) == 255);                                     // fully dark far away
	CHECK((int)A(210, 150) == e.lx->engine.darknessAtWorld(210, 150));
	for (std::size_t y = 0; y < mask->h; ++y)                  // padding bytes after each row are never written
		for (int x = (int)mask->w; x < mask->alphaPitch; ++x) CHECK(mask->alphaPlane[y * mask->alphaPitch + x] == 255);
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
	CHECK(DarkEdif::g_lastBlitSource->alphaPlane[0] == 0);     // fully lit: transparent mask
	e.SetAmbient(0.0f); e.LumenHandle();

	// obstacle rectangles and layers
	e.AddObstacleRect(1, 230, 100, 16, 100);
	CHECK(e.LumenHandle() == REFLAG::DISPLAY);
	e.ClearObstacles(1);
	CHECK(e.LumenHandle() == REFLAG::DISPLAY);

	// render options + quality + errors
	e.SetRenderOptions(0, 16, 0, -1, 1, -1); g_blits.clear(); e.LumenDisplay();
	CHECK(g_blits.size() == 1 && g_blits[0].flags == (BlitOptions::AntiAliasing | BlitOptions::CopyAlpha));
	e.SetRenderOptions(0, 16, 0, -1, 0, -1); g_blits.clear(); e.LumenDisplay();
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

	// ---- method 3: PNG loaded as a texture with real per-pixel alpha, one blit
	{
		e.CreateLight(40, 200, 150, 100, 1.0f, 1); e.LumenHandle();
		const int loadsBefore = DarkEdif::g_pngLoads;
		e.SetRenderOptions(3, 16, 0, -1, -1, -1); g_blits.clear(); e.LumenHandle(); e.LumenDisplay();
		CHECK(DarkEdif::g_pngLoads == loadsBefore + 1 && !DarkEdif::g_lastPngPath.empty());
		CHECK(g_blits.size() == 1 && g_blits[0].op == BlitOperation::Copy && g_blits[0].transparent);
		CHECK(g_blits[0].flags == (BlitOptions::CopyAlpha | BlitOptions::AntiAliasing));   // defaults: the flag the notes use, plus smoothing
		CHECK(g_blits[0].srcW == e.lx->engine.mapWidth() && g_blits[0].dr - g_blits[0].dl == e.lx->engine.blitWidth());
		const DarkEdif::Surface* tex = DarkEdif::g_lastBlitSource;
		const int gx = 200 / 2 - e.lx->engine.originPxX() / 2, gy = 150 / 2 - e.lx->engine.originPxY() / 2;
		CHECK(tex && tex->hasAlpha && tex->w == (size_t)e.lx->engine.mapWidth());
		CHECK(tex->alphaPlane[(size_t)gy * tex->alphaPitch + gx] < 20 && tex->alphaPlane[0] == 255);      // decoded from the PNG we wrote
		bool same = true;                                                                                  // every pixel survives encode + decode
		for (int y = 0; y < e.lx->engine.mapHeight() && same; ++y)
			for (int x = 0; x < e.lx->engine.mapWidth(); ++x)
				if (tex->alphaPlane[(size_t)y * tex->alphaPitch + x] != (e.lx->engine.pixels()[(size_t)y * e.lx->engine.mapWidth() + x] >> 24)) { same = false; break; }
		CHECK(same);
		e.SetRenderOptions(3, 16, 0, 0, 0, 0); g_blits.clear(); e.LumenDisplay();                         // options override the defaults
		CHECK(g_blits.size() == 1 && !g_blits[0].transparent && g_blits[0].flags == BlitOptions::None);
		e.SetRenderOptions(0, 16, 0, -1, -1, -1);
	}
	// ---- method 1: grey pixels of the alpha surface
	{
		e.CreateLight(30, 200, 150, 100, 1.0f, 1); e.LumenHandle();
		e.SetRenderOptions(1, 16, 0, -1, -1, -1); g_blits.clear(); e.LumenHandle(); e.LumenDisplay();
		const DarkEdif::Surface* m = DarkEdif::g_lastBlitSource;
		CHECK(g_blits.size() == 1 && g_blits[0].op == BlitOperation::Copy && m->alphaView);
		const int gx = 200 / 2 - e.lx->engine.originPxX() / 2, gy = 150 / 2 - e.lx->engine.originPxY() / 2;
		CHECK(m->alphaPlane[(size_t)gy * m->alphaPitch + gx] < 20 && m->alphaPlane[0] == 255);
		CHECK(e.lx->strategy == 1 && e.lx->strategyConfirmed);
	}
	// ---- method 2: rectangles, each lit cell covered exactly once, coefficient follows Fusion's convention
	for (int levels : { 16, 32 }) for (int invert = 0; invert < 2; ++invert) {
		e.SetRenderOptions(2, levels, invert, -1, -1, -1); g_blits.clear(); e.LumenHandle(); e.LumenDisplay();
		CHECK(!g_blits.empty() && (int)g_blits.size() == e.lx->runCount);
		const int d = e.lx->engine.settings().divisor, mw = e.lx->engine.mapWidth(), mh = e.lx->engine.mapHeight();
		const int ox = e.lx->engine.originPxX() - rh.get_WindowX(), oy = e.lx->engine.originPxY() - rh.get_WindowY();
		std::vector<int> cover((size_t)mw * mh, 0), level((size_t)mw * mh, 0);
		bool shape = true;
		for (auto& b : g_blits) {
			if (b.op != BlitOperation::Blend || b.srcW != 1 || b.srcH != 1 || b.flags != BlitOptions::None) shape = false;
			const int q = (int)std::lround(32.0 * (invert ? (b.coef / 128.0) : (1.0 - b.coef / 128.0)) * levels / 32.0);
			for (int cy = (b.dt - oy) / d; cy < (b.db - oy) / d; ++cy)
				for (int cx = (b.dl - ox) / d; cx < (b.dr - ox) / d; ++cx) { ++cover[(size_t)cy * mw + cx]; level[(size_t)cy * mw + cx] = q; }
		}
		CHECK(shape);
		bool exact = true;
		for (int y = 0; y < mh; ++y) for (int x = 0; x < mw; ++x) {
			const int a = (int)(e.lx->engine.pixels()[(size_t)y * mw + x] >> 24);
			const int q = (a * levels + 127) / 255;
			if (cover[(size_t)y * mw + x] != (q > 0 ? 1 : 0) || (q > 0 && level[(size_t)y * mw + x] != q)) exact = false;
		}
		CHECK(exact);
	}
	e.SetRenderOptions(0, 16, 0, -1, -1, -1);

	// ---- self-healing method choice: a method that was tried but never confirmed (the app died) is skipped next launch
	{
		auto launch = [&](const char* name, const std::string& content) {
			const std::string d = makeDir(name);
			std::ofstream(d + "/Lumen-method.txt") << content;
			setenv("TMPDIR", d.c_str(), 1);
			Frame fr; RunHeader r2; r2.rhFrame = &fr; EDITDATA ed2; RunObject so;
			Extension x(&r2, &so, &ed2);
			x.CreateLight(1, 100, 100, 60, 1.0f, 1); x.LumenHandle(); x.LumenDisplay();
			const int m = x.lx->strategy;
			return std::make_pair(m, slurp(d + "/Lumen-method.txt"));
		};
		auto first = launch("lumen_t0", "");                                           // fresh launch creates the file
		CHECK(first.first == 3);                                                       // PNG texture first
		const std::string header = first.second.substr(0, first.second.find('\n') + 1);   // "build <id>\n"
		CHECK(header.rfind("build ", 0) == 0 && first.second.find("tried 3\n") != std::string::npos && first.second.find("ok 3\n") != std::string::npos);
		CHECK(launch("lumen_t2", header + "tried 3\n").first == 2);                    // PNG died -> rectangles
		CHECK(launch("lumen_t3", header + "tried 3\ntried 2\n").first == 0);           // ... then alpha lock
		CHECK(launch("lumen_t3b", header + "tried 3\ntried 2\ntried 0\n").first == 1); // ... then alpha surface
		CHECK(launch("lumen_t4", header + "tried 3\ntried 2\ntried 0\ntried 1\n").first == 2); // all died: most conservative
		CHECK(launch("lumen_t5", header + "tried 3\ntried 2\ntried 0\nok 0\n").first == 0);    // a confirmed method wins
		CHECK(launch("lumen_t6", header + "tried 3\nok 2\n").first == 2);
		CHECK(launch("lumen_t7", "build some other build\ntried 3\ntried 2\n").first == 3);     // old build's verdicts are forgotten
		auto r = launch("lumen_t8", header + "tried 3\n");
		CHECK(r.second.find("tried 2\n") != std::string::npos && r.second.find("ok 2\n") != std::string::npos);
		setenv("TMPDIR", dir1.c_str(), 1);
	}
	{   // breadcrumb log written for diagnosing hard crashes
		std::string all = slurp(dir1 + "/Lumen-debug.log");
		CHECK(all.find("init: begin") != std::string::npos && all.find("alpha lock: written") != std::string::npos && all.find("display: draw done") != std::string::npos);
		CHECK(std::count(all.begin(), all.end(), '\n') <= 80);
	}
	CHECK(DarkEdif::g_negViolations == 0);                     // negative rect origins are always passed with allowNeg
	std::printf("%d checks, %d failed\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
