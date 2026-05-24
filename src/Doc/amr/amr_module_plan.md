# FVMAMR Module Plan

This branch starts a solver-neutral AMR module modeled on `FVMOverset`, while
leaving the heavy remeshing machinery in `FVMAdapt`.

## Current Code Shape

`FVMOverset` is the cleanest module pattern in this tree. It has a small
Makefile, public includes under `src/include/FVMOverset`, module rules grouped
by concern, and callers load it with:

```c++
Loci::load_module("fvmoverset", rdb) ;
```

The public contract is split between C++ helpers in `overset`/`overset.h` and
Loci-visible variables in `overset.lh`.

`FVMAdapt` already builds `fvmadapt_m.so`, plus the linked helper library
`libfvmadaptfunc`. It owns the mesh refinement algorithms, cell plans,
balancing, `c2p` maps, `refinedGridData`, `onlineRefineMesh()`,
`initializeGridFromPlan()`, `mapCellPartitionWeights()`, `getC2PGlobal()`, and
`AMRinterpolation`. That should be reused, not rewritten.

The awkward part is the public interface: `FVMAdapt` is a low-level remesh
database/toolkit. In-memory solver AMR currently requires solver code to know
raw `DataXFER_DB` key names such as `refineTag`, `cellweights`, `currentPlan`,
`nextPlan`, `c2p`, `c2pglobal`, `gradCells`, and `deltas`.

`Loci-Stream/amr/adaption.loci` already contains a useful solver-neutral layer:
adaptation options, mode constraints, cell-count limiting, edge-length masks,
generic sensor normalization, threshold limiting, `cellRefFlagMask`, and
generic `cell_error_*` kernels. That is the first slice moved here.

## Branch Starting Point

This branch adds:

- `src/FVMAMR/Makefile`
- `src/FVMAMR/adaption.loci`
- `src/FVMAMR/transfer.loci`
- `src/FVMAMR/restart.loci`
- `src/include/FVMAMR/amr`
- `src/include/FVMAMR/amr.h`
- `src/include/FVMAMR/amr.lh`

The module name is `fvmamr`, so solvers should be able to load:

```c++
Loci::load_module("fvmamr", rdb) ;
```

The initial module intentionally does not own solver cadence, field lists, or
field-specific sensors. It provides common adaptation variables, rules that
compute `cellRefFlagMask` from FVM geometry plus solver-provided generic
sensors, the database staging needed by `FVMAdapt`, and transfer/restart rule
classes that solvers instantiate with their own variable names.

## Minimal Solver Interface

A code that wants to use the module should:

1. Load the module rules:

   ```c++
   Loci::load_module("fvmamr", rdb) ;
   ```

2. After vars/grid facts are loaded, expand the selected sensor list and
   configure the low-level `FVMAdapt` globals:

   ```c++
   Loci::setupAdaptSensorFacts(facts) ;
   int adaptMode = Loci::configureFVMAdaptGlobals(facts) ;
   ```

   `adaptSensor` remains a `param<std::string>` in this module. A vars entry
   such as `adaptSensor: Perror,Terror` creates boolean facts named
   `adaptSensor(Perror)` and `adaptSensor(Terror)`, which enable the generic
   sensor rules.

3. Provide solver-owned sensor fields and cadence. The module exposes
   `adaptTagsActive`, so a solver can constrain its `do_adapt` rule to only
   request an AMR cycle when at least one cell was marked.

4. Query normally. When `do_adapt` is true, `FVMAMR` stages:

   - `cellPartitionWeights` with a default value of 1 per cell
   - `refineTag`, using the `FVMAdapt` encoding `0=keep`, `1=refine`,
     `2=de-refine`
   - stale parent/child map cleanup for `c2p` and `c2pglobal`
   - interpolation metadata (`gradCells`, `deltas`, `vol`) required by
     `Loci::AMRinterpolation`

5. In the driver outer loop, read the staged tag through the public helper:

   ```c++
   Loci::storeRepP tags = Loci::getAMRDBItem(Loci::AMR_DB_REFINE_TAG) ;
   if(tags != 0) {
     Loci::onlineRefineMesh(gridDataP, refmesh_rdb, adaptMode,
                            adaptLevel, tags, casename) ;
   }
   ```

   The solver still owns adapted-grid injection, restart/time-loop collapse,
   and field transfer/restart registrations.

6. Register state fields that must survive adapted continuation with the generic
   transfer/restart rule classes. The module does not infer solver time levels,
   restart variable names, or physics constraints.

With no selected sensors and no solver-provided marking rules, the module is a
safe no-op: `cellRefFlag` defaults to 0, `refineTag` is not staged, and
`adaptTagsActive` is empty.

## Proposed Ownership

Keep in `FVMAdapt`:

- mesh refinement and de-refinement algorithms
- cell plan construction and balancing
- VOG/container construction
- `refinedGridData`
- `onlineRefineMesh()` and `initializeGridFromPlan()`
- parent/child maps and AMR interpolation primitives

Own in `FVMAMR`:

- public AMR option/types header
- generic tagging and sensor rules
- enum-based wrapper functions around the `DataXFER_DB` contract
- stale-key cleanup and validation
- no-op tag guards
- adapt-level/current-plan state helpers
- transfer/restart templates that solvers instantiate with their own field
  names, time levels, DB keys, and constraints

Keep in each solver:

- when adaptation is requested
- which fields are used as sensors
- how timestep loops collapse/restart
- which state fields must be staged across adapted meshes
- solver-specific transfer/restart registrations

## Near-Term Phases

1. Add a tiny test/tutorial driver that loads `fvmamr`, creates one scalar
   sensor, queries `cellRefFlagMask`, and does not run remeshing.
2. Add a driver-side guard/helper that scans an existing `refineTag` store for
   all-zero content before calling `onlineRefineMesh()`.
3. Add compact examples showing scalar, vector, and storeVec transfer/restart
   registration from a solver module.
4. Add optional overset-aware tagging as a separate rule file, because it
   depends on `FVMOverset` variables such as `componentGeometryList`,
   `componentID`, and `iblank`.

## Validation

Use the `OBJ` configuration when validating from this source tree:

```bash
make -C src/FVMAMR LOCI_BASE=/home/wandadar/software/loci/OBJ all
```

This is currently the lowest-churn smoke test because it runs `lpp` and compiles
`fvmamr_m.so` without requiring a solver driver. A later module-load smoke can
use `vogcheck -load_module fvmamr -doc` once the local `OBJ/bin` and `OBJ/lib`
are known to be rebuilt consistently.

## Open Design Questions

- Should the long-term module be named `fvmamr` or should the cleaned interface
  live under the existing `fvmadapt` module name?
- Should `FVMAMR` build AD variants like `FVMOverset`, or stay normal-only like
  `FVMAdapt` until transfer rules need AD coverage?
- How much of the in-memory remesh loop can be wrapped without assuming a
  solver's grid reader/injection API?
