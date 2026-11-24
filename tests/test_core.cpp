#include "../core/lumen.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
using namespace lumen;

static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static Light mk(int id, float x, float y, float r, int layer = 0, float b = 1) {
	Light l; l.id = id; l.x = x; l.y = y; l.radius = r; l.layer = layer; l.brightness = b; return l;
}
static const View V{ 0, 0, 400, 400 };

static Engine::Settings S(int div = 2) { Engine::Settings s; s.divisor = div; s.cell = 8; return s; }

static void test_grid_counting() {
	ObstacleGrid g(100, 100, 8);
	g.addRect(0, { 0, 0, 16, 16 });
	g.addRect(0, { 8, 8, 24, 24 });
	CHECK(g.solid(0, 0, 0) && g.solid(0, 1, 1) && g.solid(0, 2, 2));
	g.removeRect(0, { 0, 0, 16, 16 });
	CHECK(!g.solid(0, 0, 0));
	CHECK(g.solid(0, 1, 1));            // still covered by the second rect
	CHECK(g.solid(0, 2, 2));
	g.removeRect(0, { 8, 8, 24, 24 });
	CHECK(!g.solid(0, 1, 1));
	CHECK(!g.solid(1, 0, 0));           // other layer
	CHECK(!g.solid(0, -1, 0) && !g.solid(0, 999, 0));
	g.addRect(0, { 95, 95, 500, 500 }); // clipped to frame, must not crash
	CHECK(g.solid(0, 12, 12));
	CHECK(g.takeDirty().size() >= 5);
	CHECK(g.takeDirty().empty());
}

static void test_open_light() {
	Engine e(400, 400, S());
	e.setLight(mk(1, 200, 200, 100));
	CHECK(e.render(V));
	CHECK(e.darknessAtWorld(200, 200) <= 12);                // centre (cell centre is ~1px off the light)
	CHECK(e.darknessAtWorld(200, 150) > e.darknessAtWorld(200, 200));
	CHECK(e.darknessAtWorld(200, 150) < 255);
	CHECK(e.darknessAtWorld(200, 330) == 255);               // outside radius = full darkness
	CHECK(e.darknessAtWorld(10, 10) == 255);
	CHECK(std::abs(e.darknessAtWorld(149, 200) - e.darknessAtWorld(251, 200)) <= 2); // symmetric
	CHECK(e.stats().lightsVisible == 1 && e.stats().lightsRecomputed == 1);
}

static void test_ambient() {
	Engine e(400, 400, S());
	e.setAmbient(0.5f);
	e.render(V);
	CHECK(e.darknessAtWorld(10, 10) >= 126 && e.darknessAtWorld(10, 10) <= 129);
	e.setLight(mk(1, 200, 200, 100));
	e.render(V);
	CHECK(e.darknessAtWorld(200, 200) == 0);                 // ambient + light clamp
	CHECK(e.darknessAtWorld(10, 10) >= 126 && e.darknessAtWorld(10, 10) <= 129);
}

static void test_shadow() {
	Engine e(400, 400, S());
	e.addObstacle(0, { 240, 100, 256, 300 });                // wall right of the light
	e.setLight(mk(1, 200, 200, 150));
	e.render(V);
	CHECK(e.darknessAtWorld(230, 200) < 160);                // in front of wall: lit
	CHECK(e.darknessAtWorld(300, 200) == 255);               // behind wall: shadow
	CHECK(e.darknessAtWorld(300, 130) == 255);               // behind wall, off-axis
	CHECK(e.darknessAtWorld(200, 150) < 200);                // unaffected direction
	CHECK(e.darknessAtWorld(100, 200) < 255);                // opposite side lit
	// No-wall reference is lit at the same spot
	Engine f(400, 400, S());
	f.setLight(mk(1, 200, 200, 150));
	f.render(V);
	CHECK(f.darknessAtWorld(300, 200) < 255);
	CHECK(e.stats().raysCast > 0 && f.stats().raysCast == 0); // no-obstacle fast path
}

static void test_layers() {
	Engine e(400, 400, S());
	e.addObstacle(2, { 240, 100, 256, 300 });
	e.setLight(mk(1, 200, 200, 150, 1));
	e.render(V);
	CHECK(e.darknessAtWorld(300, 200) < 255);                // layer 2 wall ignored by layer 1 light
	Engine g(400, 400, S());
	g.addObstacle(2, { 240, 100, 256, 300 });
	g.setLight(mk(1, 200, 200, 150, 2));
	g.render(V);
	CHECK(g.darknessAtWorld(300, 200) == 255);               // same layer blocks
}

static void test_light_inside_caster() {
	Engine e(400, 400, S());
	e.addObstacle(0, { 190, 190, 210, 210 });                // light sits inside its own caster
	e.setLight(mk(1, 200, 200, 100));
	e.render(V);
	CHECK(e.darknessAtWorld(260, 200) < 255);                // still lights the world
}

static void test_culling_and_cache() {
	Engine e(2000, 2000, S());
	e.setLight(mk(1, 200, 200, 100));
	e.setLight(mk(2, 1500, 1500, 100));                      // far outside the view
	CHECK(e.render(V));
	CHECK(e.stats().lightsTotal == 2 && e.stats().lightsVisible == 1 && e.stats().lightsRecomputed == 1);
	CHECK(!e.render(V));                                     // nothing changed
	CHECK(e.stats().lightsRecomputed == 0);
	// sub-cell camera move: same pixels, new blit offset, no work
	View v1 = V; v1.camX = 1;
	CHECK(!e.render(v1));
	CHECK(e.stats().lightsRecomputed == 0 && e.blitX() == -1);
	// whole-cell scroll: pixel buffer shifts but the light is NOT recomputed
	View v2 = V; v2.camX = 20;
	CHECK(e.render(v2));
	CHECK(e.stats().lightsRecomputed == 0);
	CHECK(e.darknessAtWorld(200, 200) <= 12);
	// scroll the far light into view: only it is computed
	View v3{ 1400, 1400, 400, 400 };
	CHECK(e.render(v3));
	CHECK(e.stats().lightsVisible == 1 && e.stats().lightsRecomputed == 1);
	CHECK(e.darknessAtWorld(1500, 1500) <= 12);
	CHECK(e.darknessAtWorld(200, 200) == 255);
}

static void test_selective_invalidation() {
	Engine e(2000, 400, S());
	e.setLight(mk(1, 200, 200, 100));
	e.setLight(mk(2, 900, 200, 100));
	View wide{ 0, 0, 1200, 400 };
	e.render(wide);
	CHECK(e.stats().lightsRecomputed == 2);
	e.addObstacle(0, { 240, 150, 256, 250 });                // touches light 1 only
	CHECK(e.render(wide));
	CHECK(e.stats().lightsRecomputed == 1);
	CHECK(e.darknessAtWorld(300, 200) == 255);
	e.addObstacle(1, { 240, 150, 256, 250 });                // other layer: no light uses it
	CHECK(!e.render(wide));
	CHECK(e.stats().lightsRecomputed == 0);
}

static void test_upsert_and_limits() {
	Engine::Settings s = S(); s.maxLights = 2;
	Engine e(400, 400, s);
	CHECK(e.setLight(mk(1, 100, 100, 50)));
	CHECK(e.setLight(mk(2, 200, 100, 50)));
	CHECK(!e.setLight(mk(3, 300, 100, 50)));                 // limit
	CHECK(e.setLight(mk(2, 210, 100, 50)));                  // update existing is fine
	e.render(V);
	CHECK(!e.render(V));
	e.setLight(mk(2, 210, 100, 50));                         // identical -> no work
	CHECK(!e.render(V) && e.stats().lightsRecomputed == 0);
	CHECK(e.setBrightness(2, 0.5f));
	CHECK(e.render(V) && e.stats().lightsRecomputed == 1);
	CHECK(e.setEnabled(1, false));
	e.render(V);
	CHECK(e.darknessAtWorld(100, 100) == 255);
	CHECK(e.setEnabled(1, true));
	e.render(V);
	CHECK(e.darknessAtWorld(100, 100) <= 20);
	CHECK(e.removeLight(1) && !e.removeLight(1) && e.lightCount() == 1);
	e.render(V);
	CHECK(e.darknessAtWorld(100, 100) == 255);
	CHECK(!e.setRadius(99, 10));                             // unknown id
	e.clearLights();
	CHECK(e.lightCount() == 0);
}

static void test_brightness_sum_clamps() {
	Engine e(400, 400, S());
	for (int i = 0; i < 5; ++i) e.setLight(mk(i, 200, 200, 100, 0, 1));
	e.render(V);
	CHECK(e.darknessAtWorld(200, 200) == 0);                 // no overflow, clamps to full light
	CHECK(e.darknessAtWorld(10, 10) == 255);
}

static void test_attachment() {
	Engine e(400, 400, S());
	Light l = mk(1, 0, 0, 80);
	e.setLight(l);
	CHECK(e.attach(1, 77, 10, -5, true));
	float ox = 100, oy = 120; bool alive = true;
	auto res = [&](long obj, float& x, float& y) { if (!alive || obj != 77) return false; x = ox; y = oy; return true; };
	e.updateAttachments(res);
	CHECK(e.find(1)->x == 110 && e.find(1)->y == 115);
	e.render(V);
	CHECK(e.darknessAtWorld(110, 115) <= 12);
	ox = 200; oy = 200;
	e.updateAttachments(res);
	CHECK(e.render(V) && e.stats().lightsRecomputed == 1);
	CHECK(e.darknessAtWorld(210, 195) <= 12 && e.darknessAtWorld(110, 115) == 255);
	e.updateAttachments(res);
	CHECK(!e.render(V));                                     // object did not move: no work
	alive = false;
	e.updateAttachments(res);
	CHECK(e.lightCount() == 0);                              // deleteWithObject
	e.setLight(mk(2, 50, 50, 40));
	e.attach(2, 77, 0, 0, false);
	e.updateAttachments(res);
	CHECK(e.lightCount() == 1 && e.find(2)->attachedObj == 0); // kept, detached
	CHECK(e.detach(2));
}

static void test_dynamic_caster() {
	Engine e(400, 400, S());
	e.setLight(mk(1, 100, 200, 200));
	IRect box{ 160, 180, 180, 220 };
	bool alive = true;
	auto res = [&](long, IRect& r) { if (!alive) return false; r = box; return true; };
	e.addCaster(5, 0);
	e.updateCasters(res);
	e.render(V);
	CHECK(e.darknessAtWorld(240, 200) == 255);               // shadowed by the caster
	box = { 80, 300, 120, 340 };                             // caster moves below the light
	e.updateCasters(res);
	CHECK(e.render(V) && e.stats().lightsRecomputed == 1);
	CHECK(e.darknessAtWorld(240, 200) < 255);
	CHECK(e.darknessAtWorld(100, 380) == 255);               // and shades the area behind its new spot
	e.addObstacle(0, { 80, 300, 120, 340 });                 // static wall overlapping the caster
	box = { 0, 0, 8, 8 };
	e.updateCasters(res);
	e.render(V);
	CHECK(e.grid().solid(0, 12, 40));                        // static cell survived caster removal
	alive = false;
	e.updateCasters(res);                                    // object destroyed: caster dropped
	e.removeCaster(5);                                       // idempotent
	e.render(V);
	CHECK(!e.grid().solid(0, 0, 0));
}

static void test_clear_obstacles_and_resize() {
	Engine e(400, 400, S());
	e.addObstacle(0, { 240, 100, 256, 300 });
	e.setLight(mk(1, 200, 200, 150));
	e.render(V);
	CHECK(e.darknessAtWorld(300, 200) == 255);
	e.clearObstacles(0);
	CHECK(e.render(V));
	CHECK(e.darknessAtWorld(300, 200) < 255);
	e.addObstacle(0, { 240, 100, 256, 300 });
	e.render(V);
	e.resizeFrame(800, 600);
	e.render(V);
	CHECK(e.darknessAtWorld(300, 200) < 255);                // obstacles dropped by resize
}

static void test_divisors() {
	for (int d : { 1, 2, 4, 8 }) {
		Engine e(400, 400, S(d));
		e.addObstacle(0, { 240, 100, 256, 300 });
		e.setLight(mk(1, 200, 200, 150));
		e.render(V);
		CHECK(e.darknessAtWorld(200, 200) <= 12 + 4 * d);
		CHECK(e.darknessAtWorld(300, 200) == 255);
		CHECK(e.mapWidth() == (400 + d - 1) / d + 2);
		CHECK(e.blitWidth() == e.mapWidth() * d);
	}
}

static void test_set_divisor() {
	Engine e(400, 400, S(2));
	e.addObstacle(0, { 240, 100, 256, 300 });
	e.setLight(mk(1, 200, 200, 150));
	e.render(V);
	const int w2 = e.mapWidth();
	e.setDivisor(4);
	CHECK(e.render(V));
	CHECK(e.mapWidth() < w2 && e.settings().divisor == 4);
	CHECK(e.stats().lightsRecomputed == 1);
	CHECK(e.darknessAtWorld(300, 200) == 255 && e.darknessAtWorld(200, 200) <= 30);
	e.setDivisor(4);                                         // same value: no-op
	CHECK(!e.render(V));
}

static void bench() {
	Engine::Settings s; s.divisor = 4; s.cell = 8; s.maxLights = 500;
	Engine e(4000, 4000, s);
	srand(1);
	for (int i = 0; i < 300; ++i) {
		int x = rand() % 3800, y = rand() % 3800;
		e.addObstacle(0, { x, y, x + 8 + rand() % 120, y + 8 + rand() % 24 });
	}
	View v{ 0, 0, 1280, 720 };
	for (int i = 0; i < 100; ++i) e.setLight(mk(i, (float)(rand() % 1280), (float)(rand() % 720), 200));
	auto t0 = std::chrono::steady_clock::now();
	e.render(v);
	auto t1 = std::chrono::steady_clock::now();
	const int firstRecomputed = e.stats().lightsRecomputed;
	long rays = e.stats().raysCast;
	for (int i = 0; i < 20; ++i) { e.setPosition(0, 600.0f + i, 300); e.render(v); }   // one moving light
	auto t2 = std::chrono::steady_clock::now();
	for (int i = 0; i < 20; ++i) { View s2 = v; s2.camX = 4 * (i + 1); e.render(s2); } // pure scroll
	auto t3 = std::chrono::steady_clock::now();
	auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
	std::printf("BENCH 1280x720, divisor 4, 100 lights r=200, 300 obstacles\n");
	std::printf("  full rebuild (%d lights, %ld rays): %.1f ms\n", firstRecomputed, rays, ms(t0, t1));
	std::printf("  one moving light: %.2f ms/frame\n", ms(t1, t2) / 20);
	std::printf("  scrolling (all cached): %.2f ms/frame\n", ms(t2, t3) / 20);
}

int main() {
	test_grid_counting(); test_open_light(); test_ambient(); test_shadow(); test_layers();
	test_light_inside_caster(); test_culling_and_cache(); test_selective_invalidation();
	test_upsert_and_limits(); test_brightness_sum_clamps(); test_attachment(); test_dynamic_caster();
	test_clear_obstacles_and_resize(); test_divisors(); test_set_divisor();
	std::printf("%d checks, %d failed\n", g_checks, g_fail);
	bench();
	return g_fail ? 1 : 0;
}
