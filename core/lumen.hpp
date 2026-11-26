// Lumen core: portable 2D lighting math. No Fusion / DarkEdif dependencies.
// Lights are rasterised on the CPU into a small, world-aligned "lightmap" that the
// extension layer stretches over the window as one black overlay with per-pixel alpha.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

namespace lumen {

struct IRect {
	int l = 0, t = 0, r = 0, b = 0; // r and b are exclusive
	bool empty() const { return r <= l || b <= t; }
	bool intersects(const IRect& o) const { return l < o.r && o.l < r && t < o.b && o.t < b; }
	bool operator==(const IRect& o) const { return l == o.l && t == o.t && r == o.r && b == o.b; }
};

struct Light {
	int id = 0;
	float x = 0, y = 0;       // world (frame) pixels
	float radius = 100;       // world pixels
	float brightness = 1;     // 0..1+ (scales intensity; sums are clamped)
	float falloff = 2;        // exponent of (1 - d/r); 1 = linear, 2 = soft
	int layer = 0;            // collision layer: only obstacles/casters on this layer block the light
	bool enabled = true;
	// Optional attachment to a Fusion object (Fixed value, 0 = none).
	long attachedObj = 0;
	float offX = 0, offY = 0;
	bool deleteWithObject = true;
};

// Counted occupancy grid per layer. Counting (not a flag) lets overlapping rectangles be
// added and removed independently, which dynamic shadow casters need.
class ObstacleGrid {
public:
	ObstacleGrid(int frameW = 1, int frameH = 1, int cell = 8);
	void reset(int frameW, int frameH);              // drops everything
	int cell() const { return cell_; }
	void addRect(int layer, const IRect& worldRect);
	void removeRect(int layer, const IRect& worldRect);
	void clearLayer(int layer);
	bool hasLayer(int layer) const { return layers_.count(layer) != 0; }
	bool solid(int layer, int cx, int cy) const;
	struct Dirty { int layer; IRect rect; };
	std::vector<Dirty> takeDirty();
private:
	void apply(int layer, const IRect& r, int delta);
	int cell_, w_ = 0, h_ = 0, cw_ = 0, ch_ = 0;
	std::map<int, std::vector<uint16_t>> layers_;
	std::vector<Dirty> dirty_;
};

struct View { int camX = 0, camY = 0, width = 640, height = 480; };

struct Stats {
	int lightsTotal = 0, lightsVisible = 0, lightsRecomputed = 0;
	long raysCast = 0;
	int composes = 0;          // number of frames where the pixel buffer was rebuilt
};

class Engine {
public:
	struct Settings {
		int divisor = 2;       // lightmap pixel = divisor x divisor window pixels
		int cell = 8;          // obstacle grid cell size, world pixels
		int maxLights = 512;
	};
	Engine(int frameW, int frameH, const Settings& s);
	Engine(int frameW, int frameH) : Engine(frameW, frameH, Settings()) {}

	// --- lights ---
	bool setLight(const Light& l);          // upsert; false if the light limit is reached
	bool removeLight(int id);
	void clearLights();
	const Light* find(int id) const;
	size_t lightCount() const { return lights_.size(); }
	bool setPosition(int id, float x, float y);
	bool setRadius(int id, float r);
	bool setBrightness(int id, float b);
	bool setFalloff(int id, float f);
	bool setEnabled(int id, bool on);
	bool attach(int id, long obj, float offX, float offY, bool deleteWithObject);
	bool detach(int id);
	// resolver(obj, x, y): fill the object's centre and return true, or false if it is gone.
	using PosResolver = std::function<bool(long obj, float& x, float& y)>;
	void updateAttachments(const PosResolver& r);

	// --- ambient ---
	void setAmbient(float level01);
	void setDivisor(int d);                    // runtime quality change; drops all cached light buffers
	float ambient() const { return ambient_; }

	// --- obstacles ---
	void resizeFrame(int frameW, int frameH);   // clears obstacles; registered casters are re-applied by updateCasters()
	void addObstacle(int layer, const IRect& worldRect) { grid_.addRect(layer, worldRect); }
	void clearObstacles(int layer);
	// Dynamic casters, keyed by Fusion object id. rectResolver returns the world rect or false if gone.
	void addCaster(long obj, int layer);
	void removeCaster(long obj);
	using RectResolver = std::function<bool(long obj, IRect& rect)>;
	void updateCasters(const RectResolver& r);
	const ObstacleGrid& grid() const { return grid_; }

	// --- per-frame ---
	// Returns true if the pixel buffer content changed (not merely the blit offset).
	bool render(const View& v);
	const std::vector<uint32_t>& pixels() const { return out_; }   // ARGB, rgb = 0, alpha = darkness
	int mapWidth() const { return gw_; }
	int mapHeight() const { return gh_; }
	int blitX() const { return blitX_; }   // screen position to draw the lightmap at
	int blitY() const { return blitY_; }
	int originPxX() const { return ox_ * s_.divisor; }   // frame x of the lightmap's left edge
	int originPxY() const { return oy_ * s_.divisor; }
	int blitWidth() const { return gw_ * s_.divisor; }
	int blitHeight() const { return gh_ * s_.divisor; }
	int darknessAtWorld(float wx, float wy) const;   // 0..255, 255 outside the map (tests/debug)
	const Stats& stats() const { return stats_; }
	const Settings& settings() const { return s_; }

private:
	struct Cache {
		bool valid = false;
		int gx0 = 0, gy0 = 0, w = 0, h = 0;
		std::vector<uint8_t> data;
		float x = 0, y = 0, radius = 0, brightness = 0, falloff = 0;
		int layer = 0;
	};
	struct Caster { int layer; IRect rect; bool has; };
	void computeLight(const Light& L, Cache& c, const IRect& region);
	float castRay(int layer, float ox, float oy, float dx, float dy, float maxDist) const;
	static bool sameShape(const Light& a, const Light& b);

	Settings s_;
	ObstacleGrid grid_;
	std::map<int, Light> lights_;
	std::unordered_map<int, Cache> caches_;
	std::map<long, Caster> casters_;
	std::vector<uint32_t> out_, accum_;
	float ambient_ = 0;
	bool dirty_ = true;
	int ox_ = 0, oy_ = 0, gw_ = 0, gh_ = 0, blitX_ = 0, blitY_ = 0;
	int lastW_ = -1, lastH_ = -1;
	Stats stats_;
};

} // namespace lumen
