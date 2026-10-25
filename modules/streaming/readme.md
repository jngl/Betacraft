# streaming

GL-free asynchronous meshing: capture a section and its neighbours on the main thread, mesh it on a worker pool, hand the result back to the render thread under an upload budget. `render` uses it, never the other way round (ADR 0001).

## Public entry points

- `streaming/WorldMeshStreamer.hpp`: main-thread glue between the live `World` and the pipeline (dirty sections, eviction, lifecycle).
- `streaming/AsyncChunkPipeline.hpp`: the `std::jthread` pool, job priorities, cancellation and supersession.
- `streaming/SectionNeighborhoodSnapshot18.hpp`: the immutable 18³ capture a job reads.
- `streaming/SnapshotMesher.hpp`, `streaming/BlockShapeMesher.hpp`: meshing a snapshot, detailed shapes shared with `render::ChunkMesher`.
- `streaming/PackedChunkVertex.hpp`: the 8-byte vertex and the shared index pattern.
- `streaming/PingPongStaging.hpp`, `streaming/UploadBudget.hpp`: worker-to-render hand-off and the per-frame upload budget.
- `streaming/ChunkLifecycle.hpp`: the per-chunk streaming state machine.

## What it does not do

No GL calls: buffers and draws are `render/WorldRenderer`. No terrain generation (`world/ChunkGenPipeline`).
