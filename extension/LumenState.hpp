// Lumen: per-object runtime state. Included by Extension.hpp (patched in by ci/assemble.py).
// Deliberately free of Fusion/DarkEdif types so it can be included anywhere.
#pragma once
#include "lumen.hpp"
#include <memory>
#include <string>

struct LumenGfx;   // Fusion surfaces; defined in LumenExt.cpp

struct LumenState {
	lumen::Engine engine;
	LumenGfx* gfx = nullptr;
	bool backdropObstacles = true;   // reserved: backdrops cannot be read safely yet
	bool maskDirty = true;           // lightmap changed; rewrite the alpha surface before drawing
	bool forceRedraw = true;
	int winW = 640, winH = 480;
	int objW = 0, objH = 0;          // size last given to the run object (manual display needs one)
	int frameW = 0, frameH = 0;
	int lastCamX = -999999, lastCamY = -999999;
	bool smooth = true;              // BlitOptions::AntiAliasing: smooth stretching of the lightmap
	bool copyAlpha = false;          // BlitOptions::CopyAlpha COPIES alpha instead of applying it, so it is off
	bool invertBlend = false;        // rectangles strategy: flips the Blend coefficient direction
	int strategy = -1;               // how darkness reaches the screen; -1 = not chosen yet (see LumenExt.cpp)
	int forcedStrategy = -1;         // set by the debug action; -1 = automatic
	bool strategyConfirmed = false;  // the first complete draw with this strategy has finished
	int levels = 16;                 // rectangles method: number of darkness steps (fewer = fewer blits)
	int runCount = 0;                // rectangles strategy: number of blits per draw
	int lastLightId = 0, nextAutoId = 1;
	std::string lastError;
	std::wstring scratchW;           // backing store for text expressions
	double lastRenderMs = 0, lastMaskMs = 0;
	int alphaPitch = 0;

	LumenState(int fw, int fh, const lumen::Engine::Settings& s) : engine(fw, fh, s), frameW(fw), frameH(fh) {}
	~LumenState();
};
