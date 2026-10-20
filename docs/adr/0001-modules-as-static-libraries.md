# One static library per module, linked as a strict DAG

The engine was one static library (`betacraft_core`) whose source directories included each other freely, with a dozen cycles (`world↔worldgen/storage/fluids/redstone/gameplay`, `render↔ui/streaming/lighting`, `audio↔acoustics`, `ui→game`…). Each directory becomes a module under `modules/<m>/{include/<m>,src,test}` with its own static library. A module only sees the headers of the modules it links, so an undeclared or cyclic dependency is a compile or link error rather than a convention.

## Considered Options

- **Coarser modules** (one "terrain" module for world, worldgen, storage, fluids and redstone; one "audio" for audio and acoustics) would have removed most cycles without moving files. We rejected it: the directories are already coherent units, and almost every cycle came down to one or two misplaced headers.
- **Tolerating cycles** between static libraries (CMake allows it) would have made the split cosmetic.

## Consequences

- `world` is split in two. **`voxel`** holds block and chunk data (`Block`, `BlockShapes`, `BlockEntity`, `BlockEntityStore`, `PalettedSection`, `Chunk`, `ScheduledTicks`, `SectionCoord`) at the bottom of the graph. **`world`** keeps the orchestrator (`World`, `WorldGen`, block ticks, weather, lighting engine, `ChunkGenPipeline`) *above* `worldgen`, `storage`, `fluids`, `redstone` and `gameplay`. Most modules that "needed the world" only needed `voxel`.
- A file's namespace follows its module (`betacraft::voxel::BlockId`, `betacraft::math::AABB`, `betacraft::ui::ScreenKind`). An include path or namespace always says where the code lives.
- `streaming` only holds GL-free meshing (it takes `PackedChunkVertex` and `BlockShapeMesher` from `render`). `render` depends on it, never the other way round. `lighting` stays GL-free: its uniform buffer lives in `render`.
