// Lumen: per-object runtime state. Included by Extension.hpp (patched in by ci/assemble.ps1).
// Deliberately free of Fusion/DarkEdif types so it can be included anywhere.
#pragma once
#include "lumen.hpp"
#include <memory>
#include <string>

struct LumenGfx;   // Fusion surfaces; defined in LumenExt.cpp

struct LumenState {
	lumen::Engine engine;
	LumenGfx* gfx = nullptr;
	bool backdropObstacles = true;   // reserved: backdrop import arrives in a later version
	bool bandsDirty = true;          // lightmap changed; rebuild band surfaces before drawing
	bool forceRedraw = true;
	int winW = 640, winH = 480;
	int frameW = 0, frameH = 0;
	int lastCamX = -999999, lastCamY = -999999;
	int shadeLevels = 16;            // darkness is quantised into this many opacity bands
	bool invertBlend = false;        // flips the Blend coefficient direction if Fusion's is reversed
	int lastLightId = 0, nextAutoId = 1;
	std::string lastError;
	std::wstring scratchW;           // backing store for text expressions
	double lastRenderMs = 0;
	int bandBlits = 0;

	LumenState(int fw, int fh, const lumen::Engine::Settings& s) : engine(fw, fh, s), frameW(fw), frameH(fh) {}
	~LumenState();
};
