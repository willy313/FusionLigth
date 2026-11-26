# Lumen: a lighting engine for Clickteam Fusion 2.5
NOTE: This is an AI-coded extension made with claude

Ambient darkness, many lights, soft falloff, shadows from obstacles. Built on DarkEdif, Windows only, Direct3D 11
with a software fallback. The overlay object sits on a high layer; each light has a collision layer, and only
obstacles on that layer block it.

## How it works

Lights are rasterised on the CPU into a small **lightmap** (1, 1/2, 1/4 or 1/8 of window resolution) stored as a
single black surface with per-pixel alpha, which is drawn over the window in one GPU blit with smoothed stretching. DarkEdif's Surface API has no way to draw into a GPU texture or to
run shaders, so this is the efficient design it allows. Cost control:

* lights outside the window are skipped; each light's lightmap is cached and only rebuilt if it or an obstacle in
  its range changed; scrolling reuses the caches (the lightmap is aligned to the world, not the screen)
* shadows come from angular ray casting over a coarse obstacle grid, so cost does not grow with obstacle count

Benchmark (core only, 1280x720, pixel size 4, 100 lights of radius 200, 300 obstacles): full rebuild about 35 ms,
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

Verified here: the core (108 checks), the extension layer against a mock that enforces the real SDK's rules for
`Point`, `Rect`, `SetPixel` and `CopySection` (557 checks), that the generated declarations match the C++ signatures,
and the assembly script against the real template's `Extension.hpp/.cpp`, `Edittime.cpp` and `DarkExt.json`.
Verified by a real CI build and a first run in Fusion: assembly, compile, link, `DarkEdifPreBuildTool`, loading in Fusion.

Checked against the SDK headers (written from the real declarations, not run in Fusion yet): per-pixel-alpha
rendering (`Surface(..., alpha = true)`, darkness written into the alpha plane (`GetAlphaBytes()` or `GetAlphaSurface()`; `SetPixel` moves colour only and `SetAlpha()` crashed in testing), `CopySection` with `BlitOptions::AntiAliasing`),
`Runtime.RunObjPtrFromFixed` / `FixedFromRunObjPtr`, `HeaderObject::get_X/get_Y/get_ImgWidth/...`, and Object action
parameters (`Params::Object`, delivered as `RunObject*`, once per selected instance).

Not verified:

1. Whether stretching a small alpha surface over the window works on the Direct3D frame (pixel size 2 and above).
2. Whether `DarkExt.json` accepts the parameter type spelled `"Object"` (the SDK's pre-build tool will say).
3. Backdrop objects cannot cast shadows. Reading them means following the runtime's object-info and image-bank
   structures, which the SDK headers do not make safe; use `Add obstacle rectangle` for those, or any object that
   appears in the event editor with `Make object cast shadows`. The `Backdrop obstacles` property is reserved.
4. Scroll actions apply at the end of the tick, so a light may lag one tick behind a scroll.

## Diagnosing a crash

A hard crash shows no message, so Lumen appends its first steps to `%TEMP%\Lumen-debug.log` (80 lines at most, rewritten
at each start); the last line names the step that crashed. Lumen also has three drawing methods and remembers in
`%TEMP%\Lumen-method.txt` which was tried and which was confirmed: a method that crashed is skipped on the next launch, so
restarting the application a couple of times finds one that works. See `docs/INSTALL.txt`.

## Stages

1. **Done:** overlay, ambient, lights, obstacle rectangles, object-attached lights, object shadow casters.
2. Backdrop import, if a safe way is found. Performance pass from real numbers (`DebugInfo$(`). Coloured and spot
   lights if wanted.

Editor properties (`Backdrop obstacles`, `Pixel size`) are fixed now: adding one after release breaks existing objects.
Identifier `LUMN` and all action, condition and expression IDs are stable from the first release; only append.
