# Lumen API reference

Generated from `extension/gen_ext_json.py`. IDs are list positions and never change once released.
Object parameters make Fusion run the action once for every selected instance of the chosen object.

## Actions

### Ambient light
- **Set ambient light level**: Set ambient light level to <Ambient level> (0 = pitch black, 1 = fully lit)
  - Float: Ambient level (0 to 1)

### Lights
- **Create or update light**: Create light <Light ID> at X=<X position>, Y=<Y position> (radius <Radius in pixels>, brightness <Brightness>, shadow layer <Shadow layer>)
  - Integer: Light ID (-1 = pick one automatically)
  - Float: X position
  - Float: Y position
  - Float: Radius in pixels
  - Float: Brightness (0 to 1)
  - Integer: Shadow layer (Fusion layer number)
- **Delete light**: Delete light <Light ID>
  - Integer: Light ID
- **Delete all lights**: Delete all lights
- **Move light**: Move light <Light ID> to X=<X position>, Y=<Y position>
  - Integer: Light ID
  - Float: X position
  - Float: Y position
- **Set light radius**: Set radius of light <Light ID> to <Radius in pixels> pixels
  - Integer: Light ID
  - Float: Radius in pixels
- **Set light brightness**: Set brightness of light <Light ID> to <Brightness>
  - Integer: Light ID
  - Float: Brightness (0 to 1)
- **Set light falloff**: Set falloff of light <Light ID> to <Falloff> (1 = linear, 2 = smooth, higher = tighter)
  - Integer: Light ID
  - Float: Falloff
- **Turn light on or off**: Turn light <Light ID> on or off: <On> (1 = on, 0 = off)
  - Integer: Light ID
  - Integer: On (1) or off (0)

### Objects
- **Create light on object**: Create light <Light ID> on <Object to attach the light to> (radius <Radius in pixels>, brightness <Brightness>, shadow layer <Shadow layer>, offset X=<Offset X>, Y=<Offset Y>)
  - Integer: Light ID (-1 = pick one automatically)
  - Object: Object to attach the light to
  - Float: Radius in pixels
  - Float: Brightness (0 to 1)
  - Integer: Shadow layer (Fusion layer number)
  - Float: Offset X
  - Float: Offset Y
- **Attach existing light to object**: Attach light <Light ID> to <Object to follow> (offset X=<Offset X>, Y=<Offset Y>)
  - Integer: Light ID
  - Object: Object to follow
  - Float: Offset X
  - Float: Offset Y
- **Detach light from object**: Detach light <Light ID> from its object
  - Integer: Light ID

### Shadows
- **Add obstacle rectangle**: Add obstacle on shadow layer <Shadow layer> at X=<X position>, Y=<Y position>, size <Width> x <Height>
  - Integer: Shadow layer (Fusion layer number)
  - Integer: X position
  - Integer: Y position
  - Integer: Width
  - Integer: Height
- **Clear obstacles on a layer**: Clear all obstacle rectangles on shadow layer <Shadow layer>
  - Integer: Shadow layer (Fusion layer number)
- **Make object cast shadows**: Make <Object that casts shadows> cast shadows on shadow layer <Shadow layer>
  - Object: Object that casts shadows
  - Integer: Shadow layer (Fusion layer number)
- **Stop object casting shadows**: Stop <Object> casting shadows
  - Object: Object

### Settings
- **Set lightmap pixel size**: Set lightmap pixel size to <Pixel size> (1 = sharpest, 4 = fastest)
  - Integer: Pixel size (1 to 16)
- **Debug: render options**: Debug: smooth edges <Smooth edges> (1/0), copy-alpha flag <Copy-alpha blit flag> (1/0), method <Method> (-1 auto, 0, 1, 2), invert blend <Invert blend direction, rectangles method> (1/0)
  - Integer: Smooth edges (1/0)
  - Integer: Copy-alpha blit flag (1/0)
  - Integer: Method (-1 = automatic, 0 = alpha lock, 1 = alpha surface, 2 = rectangles)
  - Integer: Invert blend direction, rectangles method (1/0)

## Conditions

- **On light created** (trigger): %o : On light created
- **Light exists**: %o : Light <Light ID> exists

## Expressions

- **Number of lights**: `NumberOfLights()` returns Integer
- **ID of the last created light**: `LastCreatedLightID()` returns Integer
- **Light X position**: `LightX(Light ID)` returns Float
- **Light Y position**: `LightY(Light ID)` returns Float
- **Light radius**: `LightRadius(Light ID)` returns Float
- **Light brightness**: `LightBrightness(Light ID)` returns Float
- **Ambient light level**: `AmbientLevel()` returns Float
- **Debug information**: `DebugInfo$()` returns Text
- **Last error message**: `LastError$()` returns Text

## Editor properties

- **Backdrop obstacles**: Reserved for a later version: backdrops cannot cast shadows yet. Use Add obstacle rectangle or Make object cast shadows (default: True)
- **Pixel size**: Lightmap pixel size in screen pixels (1-8). 1 is sharpest and slowest, 4 is a good fast setting (default: 2)
