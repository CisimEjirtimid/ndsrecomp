# Texture filtering and texture packs

The accelerated (compute) 3D renderer decodes each DS texture once, into a
texture cache. Three options act on that step (`runner/src/melonds_compute/
TextureUpscale.cpp`). The software renderer has no texture cache and ignores
them.

## Filter

Esc menu **Display > Textures**:

- **Original**: the DS texels, sharp. Stored at 1x, or replicated when a
  texture pack needs larger storage.
- **Smooth**: the xBR-lv2 filter at `texture_upscale` (2 or 4; 4 when it
  is 1).

The default follows `[display] texture_upscale` (above 1 means Smooth), or
`[display] texture_filter = "original" | "smooth"`, then the player's menu
choice (saved as `texture_filter=` in the runtime settings file), then
`--texture-filter`.

## Texture packs

`--texture-pack DIR` names the pack folder. Esc menu **Display > Texture
pack** turns it on or off (on by default when the folder exists; saved as
`texture_pack=`). Subfolders are searched too.

Every texture is named by what the game decoded, not where it sits in
VRAM:

```
<width>x<height>_<hash>.png      e.g. 64x64_6e7a792f1570ce58.png
```

`<hash>` is FNV-1a 64 over the decoded texels (palette applied, four bytes
per texel: r, g, b in 0..63, alpha in 0..31), in lowercase hex. A palette
swap or another transparency mode is a different texture.

A replacement may be any whole multiple of the native size (a 64x64 texture
takes 64x64, 128x128, 256x256, ...). It is resampled to the storage factor
(the `texture_upscale` factor, 4 by default): box-averaged when it is a
multiple of that factor, nearest otherwise. Use 4x for full detail. Alpha
is kept; colours are stored at the DS renderer's precision (6 bits per
channel, 5 for alpha), so very smooth gradients can band.

Textures without a replacement follow the filter.

## Dumping

`--texture-dump DIR` writes every texture the cache decodes, once, as
`DIR/<name>.png` (8-bit RGBA) under the name a pack uses. Play through the
scenes to collect them, edit or upscale them, and put the results in a pack
folder. Textures the game builds at runtime (captures, animated textures)
dump once per distinct content.

## Changing settings while playing

A menu change rebuilds the 3D renderer at its current scale (the same
operation the performance governor uses to change resolution), so the new
cache is filled with the new filter and pack: a short hitch, no state is
lost.
