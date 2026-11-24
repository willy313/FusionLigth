# Lumen: a lighting engine for Clickteam Fusion 2.5

Ambient darkness, many lights, soft falloff, shadows from obstacles. Built on DarkEdif, Windows only, Direct3D 11
with a software fallback. The overlay object sits on a high layer; each light has a collision layer, and only
obstacles on that layer block it.

## How it works

Lights are rasterised on the CPU into a small **lightmap** (1, 1/2, 1/4 or 1/8 of window resolution), then the
result is drawn over the window in a few GPU blits. DarkEdif's Surface API has no way to draw into a GPU texture or to
run shaders, so this is the efficient design it allows. Cost control:

* lights outside the window are skipped; each light's lightmap is cached and only rebuilt if it or an obstacle in
  its range changed; scrolling reuses the caches (the lightmap is aligned to the world, not the screen)
* shadows come from angular ray casting over a coarse obstacle grid, so cost does not grow with obstacle count

Benchmark (core only, 1280x720, quality 1/4, 100 lights of radius 200, 300 obstacles): full rebuild about 35 ms,
one moving light about 0.8 ms per frame, scrolling about 0.5 ms per frame.

## Repository layout

| Path | Contents |
| --- | --- |
| `core/` | portable C++17 lighting math, no Fusion dependencies (`lumen.hpp/.cpp`) |
| `extension/` | the Fusion layer: `LumenExt.cpp`, `LumenState.hpp`, and `gen_ext_json.py`, the single source of truth for actions, conditions, expressions, menus and properties |
| `ci/assemble.py` | copies the DarkEdif template, overlays our code and patches what the template needs |
| `.github/workflows/build.yml` | tests on Linux, builds the `.mfx` files on `windows-2022`, uploads them as an artifact |
| `tests/` | core tests and a mock of the DarkEdif API used to test the extension layer |
| `docs/` | `API.md` (generated reference) and `INSTALL.txt` (ships inside the artifact) |

## Getting a build

1. Create a GitHub repository and push this folder to it.
2. Open the **Actions** tab. A push to `main` or `master` starts **Build Lumen** automatically; **Run workflow**
   starts it by hand and offers two options (object access, SDK version).
3. When the run is green, open it and download the artifact **Lumen-extension**. It contains both `.mfx` files and
   `INSTALL.txt`, which also has the first Fusion test.
4. If the run is red, download **Lumen-build-diagnostics** (always uploaded) and send me the `out/logs` folder or the
   error text. It also contains `template-reference/`, the template's own files exactly as the runner saw them.
5. Push a tag such as `v0.1.0` to also publish a GitHub release with a zip.

`sh run_tests.sh` runs everything that does not need Windows or Fusion.

## What is verified and what is not

Verified here: the core (108 checks), the extension logic against a mock of the DarkEdif API (72 checks, 74 with
object access), that the generated declarations match the C++ signatures, and the assembly script's mechanics against
a mock template.

**Not verified, because neither the SDK nor Fusion could be reached from where this was written.** Expect the first
CI run or the first test in Fusion to need a round of fixes:

1. `ci/assemble.py` anchors against the real template (names such as `Edif::Runtime Runtime;`, the `OEFLAGS` line,
   `LinkAction` lines, the `Handle()`/`Display()` bodies). A miss stops with a message naming the missing anchor.
2. `DarkExt.json`: entries are cloned from the template's own examples so unknown fields are inherited, but the
   result has not been through the SDK's pre-build tool.
3. Surface calls: the constructor argument list, `SetPixel` parameter order, passing brace-initialised rectangles and a
   `Surface&` destination to `CopySection`, **stretching** a small surface onto a larger rectangle, the **direction of the
   Blend coefficient** (the `invert` debug option exists for this), and whether `CreateFromMainWindow` returns something
   the caller must free.
4. Manual-display requirements. The object must be sized to cover the window in the editor; the template may need more
   members implemented (`GetZoneInfos`, `GetDisplaySurface`, `GetCollisionMask` per your notes).
5. Property API: `IsPropChecked` and `GetPropertyStr` as in your notes; the property schema comes from the template.
6. Object access (`LUMEN_OBJECT_ACCESS`): `RunObjPtrFromFixed`, `get_X`/`get_Y` and `hoRect` are written from memory.
   It is off by default and enabled with the workflow option once the basic build is green.
7. Scroll actions apply at the end of the tick, so lights may lag one tick behind a scroll.

## Stages

1. **Now:** overlay, ambient, lights, obstacle rectangles, quality and render debug options. Objects: off.
2. Object access: attach lights to objects, object shadow casters. Then backdrop obstacle import (needs the real SDK
   headers); the `Backdrop obstacles` property is reserved for it.
3. Performance pass from real numbers (`DebugInfo$(`), then colour and spot lights if wanted.

Editor properties (`Backdrop obstacles`, `Quality`) are fixed now: adding one after release breaks existing objects.
Identifier `LUMN` and all action, condition and expression IDs are stable from the first release; only append.
