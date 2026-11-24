#include "lumen.hpp"
#include <algorithm>
#include <cmath>

namespace lumen {

static const float kPi = 3.14159265358979f;
static const float kInf = 1e30f;

static int floorDiv(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
static int ceilDiv(int a, int b) { return -floorDiv(-a, b); }

// ---------------------------------------------------------------- ObstacleGrid

ObstacleGrid::ObstacleGrid(int frameW, int frameH, int cell) : cell_(cell < 1 ? 1 : cell) { reset(frameW, frameH); }

void ObstacleGrid::reset(int frameW, int frameH) {
	w_ = std::max(1, frameW); h_ = std::max(1, frameH);
	cw_ = ceilDiv(w_, cell_); ch_ = ceilDiv(h_, cell_);
	layers_.clear();
	dirty_.clear();
}

void ObstacleGrid::apply(int layer, const IRect& r, int delta) {
	if (r.empty()) return;
	auto it = layers_.find(layer);
	if (it == layers_.end()) {
		if (delta < 0) return;
		it = layers_.emplace(layer, std::vector<uint16_t>((size_t)cw_ * ch_, 0)).first;
	}
	int cx0 = std::max(0, floorDiv(r.l, cell_)), cx1 = std::min(cw_, ceilDiv(r.r, cell_));
	int cy0 = std::max(0, floorDiv(r.t, cell_)), cy1 = std::min(ch_, ceilDiv(r.b, cell_));
	for (int cy = cy0; cy < cy1; ++cy)
		for (int cx = cx0; cx < cx1; ++cx) {
			uint16_t& v = it->second[(size_t)cy * cw_ + cx];
			if (delta > 0) { if (v < 65535) ++v; } else if (v > 0) --v;
		}
	dirty_.push_back({ layer, r });
}

void ObstacleGrid::addRect(int layer, const IRect& r) { apply(layer, r, +1); }
void ObstacleGrid::removeRect(int layer, const IRect& r) { apply(layer, r, -1); }

void ObstacleGrid::clearLayer(int layer) {
	if (layers_.erase(layer)) dirty_.push_back({ layer, IRect{ -1000000, -1000000, 1000000, 1000000 } });
}

bool ObstacleGrid::solid(int layer, int cx, int cy) const {
	if (cx < 0 || cy < 0 || cx >= cw_ || cy >= ch_) return false;
	auto it = layers_.find(layer);
	return it != layers_.end() && it->second[(size_t)cy * cw_ + cx] != 0;
}

std::vector<ObstacleGrid::Dirty> ObstacleGrid::takeDirty() {
	std::vector<Dirty> d; d.swap(dirty_); return d;
}

// ---------------------------------------------------------------------- Engine

Engine::Engine(int frameW, int frameH, const Settings& s) : s_(s), grid_(frameW, frameH, s.cell) {
	if (s_.divisor < 1) s_.divisor = 1;
	if (s_.maxLights < 1) s_.maxLights = 1;
}

bool Engine::sameShape(const Light& a, const Light& b) {
	return a.x == b.x && a.y == b.y && a.radius == b.radius && a.brightness == b.brightness &&
		a.falloff == b.falloff && a.layer == b.layer && a.enabled == b.enabled;
}

bool Engine::setLight(const Light& l) {
	auto it = lights_.find(l.id);
	if (it == lights_.end()) {
		if ((int)lights_.size() >= s_.maxLights) return false;
		lights_[l.id] = l;
		dirty_ = true;
		return true;
	}
	if (!sameShape(it->second, l)) dirty_ = true;
	it->second = l;
	return true;
}

bool Engine::removeLight(int id) {
	if (!lights_.erase(id)) return false;
	caches_.erase(id);
	dirty_ = true;
	return true;
}

void Engine::clearLights() { if (!lights_.empty()) dirty_ = true; lights_.clear(); caches_.clear(); }

const Light* Engine::find(int id) const { auto it = lights_.find(id); return it == lights_.end() ? nullptr : &it->second; }

#define LUMEN_EDIT(id, stmt) \
	auto it = lights_.find(id); if (it == lights_.end()) return false; \
	Light n = it->second; stmt; return setLight(n);

bool Engine::setPosition(int id, float x, float y) { LUMEN_EDIT(id, n.x = x; n.y = y) }
bool Engine::setRadius(int id, float r) { LUMEN_EDIT(id, n.radius = std::max(1.0f, r)) }
bool Engine::setBrightness(int id, float b) { LUMEN_EDIT(id, n.brightness = std::max(0.0f, b)) }
bool Engine::setFalloff(int id, float f) { LUMEN_EDIT(id, n.falloff = std::max(0.1f, f)) }
bool Engine::setEnabled(int id, bool on) { LUMEN_EDIT(id, n.enabled = on) }
bool Engine::attach(int id, long obj, float ox, float oy, bool del) {
	LUMEN_EDIT(id, n.attachedObj = obj; n.offX = ox; n.offY = oy; n.deleteWithObject = del)
}
bool Engine::detach(int id) { LUMEN_EDIT(id, n.attachedObj = 0) }

void Engine::updateAttachments(const PosResolver& resolve) {
	std::vector<int> gone;
	for (auto& kv : lights_) {
		Light& L = kv.second;
		if (!L.attachedObj) continue;
		float x, y;
		if (resolve(L.attachedObj, x, y)) {
			x += L.offX; y += L.offY;
			if (x != L.x || y != L.y) { L.x = x; L.y = y; dirty_ = true; }
		} else if (L.deleteWithObject) gone.push_back(L.id);
		else L.attachedObj = 0;
	}
	for (int id : gone) removeLight(id);
}

void Engine::setAmbient(float a) {
	a = std::min(1.0f, std::max(0.0f, a));
	if (a != ambient_) { ambient_ = a; dirty_ = true; }
}

void Engine::setDivisor(int d) {
	d = std::max(1, std::min(16, d));
	if (d == s_.divisor) return;
	s_.divisor = d;
	caches_.clear();
	gw_ = gh_ = 0;           // forces the lightmap window to be rebuilt on next render
	dirty_ = true;
}

void Engine::resizeFrame(int w, int h) {
	grid_.reset(w, h);
	for (auto& kv : casters_) kv.second.has = false;
	for (auto& kv : caches_) kv.second.valid = false;
	dirty_ = true;
}

void Engine::clearObstacles(int layer) {
	grid_.clearLayer(layer);
	for (auto& kv : casters_) if (kv.second.layer == layer) kv.second.has = false;
}

void Engine::addCaster(long obj, int layer) {
	auto it = casters_.find(obj);
	if (it != casters_.end()) {
		if (it->second.has) grid_.removeRect(it->second.layer, it->second.rect);
		it->second = { layer, IRect(), false };
	} else casters_[obj] = { layer, IRect(), false };
}

void Engine::removeCaster(long obj) {
	auto it = casters_.find(obj);
	if (it == casters_.end()) return;
	if (it->second.has) grid_.removeRect(it->second.layer, it->second.rect);
	casters_.erase(it);
}

void Engine::updateCasters(const RectResolver& resolve) {
	for (auto it = casters_.begin(); it != casters_.end();) {
		Caster& c = it->second;
		IRect r;
		if (!resolve(it->first, r)) {
			if (c.has) grid_.removeRect(c.layer, c.rect);
			it = casters_.erase(it);
			continue;
		}
		if (!c.has || !(r == c.rect)) {
			if (c.has) grid_.removeRect(c.layer, c.rect);
			grid_.addRect(c.layer, r);
			c.rect = r; c.has = true;
		}
		++it;
	}
}

// Amanatides-Woo grid traversal. Solid cells at the very start (a light inside its own
// caster) are skipped until the ray leaves them. Returns distance to the first solid cell.
float Engine::castRay(int layer, float ox, float oy, float dx, float dy, float maxDist) const {
	const float C = (float)grid_.cell();
	int cx = (int)std::floor(ox / C), cy = (int)std::floor(oy / C);
	const int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
	const float tdx = dx != 0 ? C / std::fabs(dx) : kInf, tdy = dy != 0 ? C / std::fabs(dy) : kInf;
	float tmx = dx > 0 ? ((cx + 1) * C - ox) / dx : dx < 0 ? (cx * C - ox) / dx : kInf;
	float tmy = dy > 0 ? ((cy + 1) * C - oy) / dy : dy < 0 ? (cy * C - oy) / dy : kInf;
	bool leaving = grid_.solid(layer, cx, cy);
	for (;;) {
		float t;
		if (tmx < tmy) { t = tmx; cx += sx; tmx += tdx; } else { t = tmy; cy += sy; tmy += tdy; }
		if (t > maxDist) return kInf;
		const bool s = grid_.solid(layer, cx, cy);
		if (leaving) { if (!s) leaving = false; }
		else if (s) return t;
	}
}

void Engine::computeLight(const Light& L, Cache& c) {
	const int d = s_.divisor;
	const float C = (float)grid_.cell();
	c.x = L.x; c.y = L.y; c.radius = L.radius; c.brightness = L.brightness; c.falloff = L.falloff; c.layer = L.layer;
	c.gx0 = (int)std::floor((L.x - L.radius) / d);
	c.gy0 = (int)std::floor((L.y - L.radius) / d);
	c.w = (int)std::floor((L.x + L.radius) / d) - c.gx0 + 1;
	c.h = (int)std::floor((L.y + L.radius) / d) - c.gy0 + 1;
	c.data.assign((size_t)c.w * c.h, 0);
	c.valid = true;

	int N = 0;
	std::vector<float> hit;
	if (grid_.hasLayer(L.layer)) {
		N = std::min(1024, std::max(32, (int)std::ceil(2 * kPi * L.radius / d)));
		hit.resize(N);
		for (int k = 0; k < N; ++k) {
			const float a = -kPi + (2 * kPi) * k / N;
			hit[k] = castRay(L.layer, L.x, L.y, std::cos(a), std::sin(a), L.radius + C);
		}
		stats_.raysCast += N;
	}
	const float tol = C, soft = C * 0.5f, r = L.radius;
	for (int j = 0; j < c.h; ++j) {
		const float wy = (c.gy0 + j + 0.5f) * d - L.y;
		for (int i = 0; i < c.w; ++i) {
			const float wx = (c.gx0 + i + 0.5f) * d - L.x;
			const float dist = std::sqrt(wx * wx + wy * wy);
			if (dist >= r) continue;
			float vis = 1;
			if (N) {
				const float f = (std::atan2(wy, wx) + kPi) * N / (2 * kPi);
				int i0 = (int)std::floor(f);
				const float fr = f - i0;
				i0 = ((i0 % N) + N) % N;
				const int i1 = (i0 + 1) % N;
				const float v0 = std::min(1.0f, std::max(0.0f, (hit[i0] + tol - dist) / soft));
				const float v1 = std::min(1.0f, std::max(0.0f, (hit[i1] + tol - dist) / soft));
				vis = v0 + (v1 - v0) * fr;
			}
			const float u = 1 - dist / r;
			const float shape = L.falloff == 1 ? u : L.falloff == 2 ? u * u : std::pow(u, L.falloff);
			const float v = L.brightness * shape * vis * 255.0f + 0.5f;
			c.data[(size_t)j * c.w + i] = (uint8_t)std::min(255.0f, std::max(0.0f, v));
		}
	}
}

bool Engine::render(const View& v) {
	const int composes = stats_.composes;
	stats_ = Stats();
	stats_.composes = composes;
	stats_.lightsTotal = (int)lights_.size();

	const int d = s_.divisor;
	const int nox = floorDiv(v.camX, d), noy = floorDiv(v.camY, d);
	const int ngw = ceilDiv(std::max(1, v.width), d) + 2, ngh = ceilDiv(std::max(1, v.height), d) + 2;
	if (nox != ox_ || noy != oy_ || ngw != gw_ || ngh != gh_) {
		ox_ = nox; oy_ = noy; gw_ = ngw; gh_ = ngh;
		dirty_ = true;
	}
	blitX_ = ox_ * d - v.camX;
	blitY_ = oy_ * d - v.camY;

	// Obstacle changes invalidate only the lights whose box they touch (same layer).
	const auto dirtyRects = grid_.takeDirty();
	if (!dirtyRects.empty()) {
		const float C = (float)grid_.cell();
		for (auto& kv : caches_) {
			Cache& c = kv.second;
			if (!c.valid) continue;
			const IRect box{ (int)std::floor(c.x - c.radius - C), (int)std::floor(c.y - c.radius - C),
				(int)std::ceil(c.x + c.radius + C), (int)std::ceil(c.y + c.radius + C) };
			for (auto& dr : dirtyRects)
				if (dr.layer == c.layer && dr.rect.intersects(box)) { c.valid = false; break; }
		}
	}

	std::vector<const Light*> visible;
	for (auto& kv : lights_) {
		const Light& L = kv.second;
		if (!L.enabled) continue;
		const int gx0 = (int)std::floor((L.x - L.radius) / d), gx1 = (int)std::floor((L.x + L.radius) / d);
		const int gy0 = (int)std::floor((L.y - L.radius) / d), gy1 = (int)std::floor((L.y + L.radius) / d);
		if (gx1 < ox_ || gx0 >= ox_ + gw_ || gy1 < oy_ || gy0 >= oy_ + gh_) continue;
		visible.push_back(&L);
		Cache& c = caches_[L.id];
		if (!c.valid || c.x != L.x || c.y != L.y || c.radius != L.radius || c.brightness != L.brightness ||
			c.falloff != L.falloff || c.layer != L.layer) {
			computeLight(L, c);
			++stats_.lightsRecomputed;
			dirty_ = true;
		}
	}
	stats_.lightsVisible = (int)visible.size();
	if (!dirty_) return false;

	accum_.assign((size_t)gw_ * gh_, 0);
	for (const Light* L : visible) {
		const Cache& c = caches_[L->id];
		const int x0 = std::max(c.gx0, ox_), x1 = std::min(c.gx0 + c.w, ox_ + gw_);
		const int y0 = std::max(c.gy0, oy_), y1 = std::min(c.gy0 + c.h, oy_ + gh_);
		for (int gy = y0; gy < y1; ++gy) {
			const uint8_t* src = &c.data[(size_t)(gy - c.gy0) * c.w + (x0 - c.gx0)];
			uint32_t* dst = &accum_[(size_t)(gy - oy_) * gw_ + (x0 - ox_)];
			for (int gx = x0; gx < x1; ++gx) *dst++ += *src++;
		}
	}
	out_.resize(accum_.size());
	const uint32_t amb = (uint32_t)(ambient_ * 255.0f + 0.5f);
	for (size_t i = 0; i < accum_.size(); ++i) {
		const uint32_t lit = std::min<uint32_t>(255, amb + accum_[i]);
		out_[i] = (255 - lit) << 24;
	}
	dirty_ = false;
	++stats_.composes;
	return true;
}

int Engine::darknessAtWorld(float wx, float wy) const {
	const int gx = (int)std::floor(wx / s_.divisor) - ox_, gy = (int)std::floor(wy / s_.divisor) - oy_;
	if (gx < 0 || gy < 0 || gx >= gw_ || gy >= gh_ || out_.size() != (size_t)gw_ * gh_) return 255;
	return (int)(out_[(size_t)gy * gw_ + gx] >> 24);
}

} // namespace lumen
