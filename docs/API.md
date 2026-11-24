# Lumen API reference

Generated from `extension/gen_ext_json.py`. IDs are list positions and never change once released.

## Actions

### Ambient
- **Set ambient light to <Level>**
  - Float: Level (0 = pitch black, 1 = fully lit)

### Lights
- **Create or update light <Light ID> at (<X>, <Y>), radius <Radius>, brightness <Brightness>, on layer <Collision layer>**
  - Integer: Light ID (-1 = assign automatically)
  - Float: X (frame pixels)
  - Float: Y (frame pixels)
  - Float: Radius (pixels)
  - Float: Brightness (0-1)
  - Integer: Collision layer (1-based Fusion layer)
- **Delete light <Light ID>**
  - Integer: Light ID
- **Delete all lights**
- **Move light <Light ID> to (<X>, <Y>)**
  - Integer: Light ID
  - Float: X
  - Float: Y
- **Set light <Light ID> radius to <Radius>**
  - Integer: Light ID
  - Float: Radius (pixels)
- **Set light <Light ID> brightness to <Brightness>**
  - Integer: Light ID
  - Float: Brightness (0-1)
- **Set light <Light ID> falloff exponent to <Exponent> (1 = linear, 2 = soft)**
  - Integer: Light ID
  - Float: Exponent
- **Set light <Light ID> on/off: <On> (1 = on, 0 = off)**
  - Integer: Light ID
  - Integer: On (1) / Off (0)

### Objects
- **Attach light <Light ID> to object <Object> (offset <Offset X>, <Offset Y>; delete with object: <Delete light when object is destroyed>)**
  - Integer: Light ID
  - Integer: Object (use Fixed("object"))
  - Float: Offset X
  - Float: Offset Y
  - Integer: Delete light when object is destroyed (1/0)
- **Detach light <Light ID> from its object**
  - Integer: Light ID
- **Create light <Light ID> on object <Object> (radius <Radius>, brightness <Brightness>, layer <Collision layer>, offset <Offset X>, <Offset Y>)**
  - Integer: Light ID (-1 = assign automatically)
  - Integer: Object (use Fixed("object"))
  - Float: Radius (pixels)
  - Float: Brightness (0-1)
  - Integer: Collision layer (1-based Fusion layer)
  - Float: Offset X
  - Float: Offset Y

### Shadows
- **Add obstacle on layer <Layer> at (<X>, <Y>) size <Width> x <Height>**
  - Integer: Layer
  - Integer: X
  - Integer: Y
  - Integer: Width
  - Integer: Height
- **Clear all obstacles on layer <Layer>**
  - Integer: Layer
- **Make object <Object> cast shadows on layer <Layer>**
  - Integer: Object (use Fixed("object"))
  - Integer: Layer
- **Stop object <Object> casting shadows**
  - Integer: Object (use Fixed("object"))

### Quality
- **Set lightmap pixel size to <Pixel size> x <Pixel size> screen pixels (1 = sharpest, 4 = fast)**
  - Integer: Pixel size (1-16)
- **Debug: set shade levels to <Shade levels>, invert blend: <Invert blend coefficient>**
  - Integer: Shade levels (2-64)
  - Integer: Invert blend coefficient (1/0)

## Conditions

- **On light created** (trigger)
- **Light <Light ID> exists**

## Expressions

- `LightCount()` returns Integer
- `LastLightID()` returns Integer
- `LightX(Light ID)` returns Float
- `LightY(Light ID)` returns Float
- `LightRadius(Light ID)` returns Float
- `LightBrightness(Light ID)` returns Float
- `Ambient()` returns Float
- `DebugInfo$()` returns Text
- `LastError$()` returns Text

## Editor properties

- **Backdrop obstacles**: Backdrop objects with an obstacle type cast shadows (reserved; imported in a later version) (default: True)
- **Quality**: Lightmap resolution: lower is faster, higher has sharper shadows (default: Half)
