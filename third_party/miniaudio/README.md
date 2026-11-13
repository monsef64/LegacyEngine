# miniaudio

Vendored miniaudio 0.11.23 from https://github.com/mackron/miniaudio
(commit f40cf03f80cdb7e741d43e53b7e706e8c1394bcf).

Used for WAV/MP3 decoding, conversion to stereo float PCM at 48 kHz, and
playback devices for scene audio and Asset Browser previews. The engine retains
its deterministic main-thread mixer for script-controlled voices and spatial gains.
The device callback consumes a bounded PCM queue without accessing the ECS.

The standalone dr_libs dependency has been removed. miniaudio internally embeds
its upstream dr_* decoders, including its MP3 decoder, in the single header.
See LICENSE for miniaudio's public-domain / MIT-0 licensing options.
