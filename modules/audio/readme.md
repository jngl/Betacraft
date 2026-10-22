# audio

The mixer: procedurally synthesised sounds, music and rain, mixed in the miniaudio callback. Game threads post commands through a lock-free `MpscRingBuffer`; the callback takes no lock and never allocates. Spatial voices go through `acoustics::AcousticVoiceBank`.

## Public entry points

- `audio/AudioEngine.hpp`: `AudioEngine` (`initialize`, `playSound` and the per-block helpers, `postAcoustic`, `renderFrames` for headless mixing), `SoundEffect`, `AudioCommand`.

## What it does not do

No occlusion or reverb computation (`acoustics`). miniaudio stays private to this module (`miniaudio_lib`, behind `AudioEngine::Device`), so no other module sees `miniaudio.h`.
