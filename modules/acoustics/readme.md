# acoustics

Sound propagation: voxel DDA occlusion at 10 Hz, enclosure detection, and the audio-thread side that applies them (per-voice low-pass and one shared FDN reverb bus). The game-thread driver is `game/AcousticsController`; the mixer that owns the voice bank is `audio`.

## Public entry points

- `acoustics/VoxelDda.hpp`, `acoustics/AcousticOcclusion.hpp`: rays through the voxel grid and the occlusion they give.
- `acoustics/WorldSolidity.hpp`: the solidity query over a live `World`.
- `acoustics/OcclusionScheduler.hpp`, `acoustics/EnclosureDetector.hpp`, `acoustics/EnclosureMonitor.hpp`: game-thread updates, at most 10 Hz.
- `acoustics/AcousticCommand.hpp`: what the game thread sends to the audio thread.
- `acoustics/AcousticVoiceBank.hpp`, `acoustics/VoiceAcousticFilter.hpp`, `acoustics/OnePoleFilter.hpp`, `acoustics/AuxReverbBus.hpp`, `acoustics/FdnReverb.hpp`: the audio-thread processing, allocation-free.

## What it does not do

No audio device or sound synthesis (`audio`).
