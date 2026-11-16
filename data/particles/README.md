# Bundled particle sprites

`fire.png`, `smoke.png` and `sparkles.png` were AI-generated specifically for
LegacyEngine's particle presets, then resized to 256x256 and converted to white
RGB with soft alpha. The emitter supplies their color. No external stock-photo
or third-party sprite pack is used.

The original sprite shapes are grayscale intensity masks on black; black was
converted to transparency, with a border fade to prevent rectangular edges.
These are runtime engine assets, copied with the rest of `data` during builds.
