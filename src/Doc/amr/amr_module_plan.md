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

This plan is about the dynamic online AMR path that solvers drive through
`DataXFER_DB` and `onlineRefineMesh()`. The older XML region/marker-tool path
documented under `src/FVMAdapt/doc` is adjacent FVMAdapt functionality, but it
is not the target interface for this module.

`Loci-Stream/amr/adaption.loci` already contains a useful solver-neutral layer:
adaptation options, mode constraints, cell-count limiting, edge-length masks,
generic sensor normalization, threshold limiting, `cellRefFlagMask`, and
generic `cell_error_*` kernels. That is the first slice moved here.

## Primary Developer Guidance

The existing `FVMAdapt` module is already the loadable module for low-level
adaptation rules. The open design question is not whether to replace that
module; it is whether a separate solver-side helper module should exist, and
which parts of online adaptation are stable enough to move out of individual
solvers.

The current branch should therefore be treated as an exploratory solver-control
prototype. It captures useful common pieces, but the long-term interface should
not be considered settled until we have compared the current online adaptation
use cases, including CHEM, GGFS, Stream, and multi-solver contexts. Keeping
experimental logic in solvers has a real advantage while the interface is still
moving: different policies can be tried without disrupting existing
applications.

Important concerns raised by the primary Loci developer:

- Current online adaptation interfaces, including those used in CHEM, still
  have shortcomings that need more exploration.
- The current refinement/coarsening path is largely built around error
  variables. That does not cover all cases, especially geometry-driven
  refinement such as immersed-boundary or overset-region refinement.
- Some desired behavior may require new features in `FVMAdapt`, which is loaded
  into a separate rule database for the remeshing phase.
- A solver can request coarsening, but the interface does not yet expose enough
  information to know whether the present cell came from refinement and can
  actually be coarsened.
- Multiple refinement objectives may conflict. Error indicators, geometric
  constraints, overset/immersed-boundary requirements, and mesh-size limits may
  all request different actions on the same cells.
- The solver-side contract must eventually say what the solver must provide so
  that AMR has enough information to make controlled mesh-refinement decisions.

Possible path before hardening a solver helper module:

- Write an integration survey of existing online AMR users. For each solver or
  workflow, record how it chooses cadence, computes sensors, requests
  refinement/coarsening, transfers state, rebuilds the grid/fact database, and
  handles restart or continuation.
- Document what works and what is awkward in those integrations before adding
  more public API.
- Keep candidate policies in solver code until at least two use cases need the
  same behavior.
- Promote only small, clearly solver-neutral pieces into this module:
  database-key wrappers, common transfer/restart templates, no-op guards,
  diagnostics, and reusable request-composition helpers.
- Consider a final name such as `fvmadaptsolver` or `fvmadaptcontrols` if the
  stabilized interface is clearly a solver-control layer over `FVMAdapt`, not a
  replacement for the existing `fvmadapt` module.

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

The public solver-facing contract is documented in the renderable module page
under `src/FVMAMR/doc/README.md`. The companion
`src/Doc/amr/amr_solver_interface.md` is the branch-local integration guide;
this file remains the branch plan and design record.

## Candidate Minimal Solver Interface

This is the current candidate interface used by the prototype branch. It is a
useful test surface, but should be revised as the existing solver use cases and
request-fusion requirements become clearer.

A code that wants to use the prototype module should:

1. Load the module rules:

   ```c++
   Loci::load_module("fvmamr", rdb) ;
   ```

2. After vars/grid facts are loaded, expand the selected sensor list and
   configure the low-level `FVMAdapt` globals:

   ```c++
   Loci::setupFVMAMRFacts(facts) ;
   int adaptMode = Loci::configureFVMAdaptGlobals(facts) ;
   ```

   `adaptSensor` remains a `param<std::string>` in this module. A vars entry
   such as `adaptSensor: Perror,Terror` creates boolean facts named
   `adaptSensor(Perror)` and `adaptSensor(Terror)`, which enable the generic
   sensor rules.

   `adaptSensorMetrics` is the structured selector for common module-generated
   metrics. A vars entry such as
   `adaptSensorMetrics: <p=[type=scalar,kernel=faceJump,weight=1.0]>` creates
   `amrUseScalarFaceJump(p)`, `amrScalarFaceJump(p)`,
   `adaptSensor(amrScalarFaceJump(p))`, and
   `adaptSensorWeight(amrScalarFaceJump(p))`. This lets a solver pass ordinary
   field names through the interface when the module owns the metric kernel.

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

1. Document existing solver integrations: CHEM, GGFS, Stream, and any
   multi-solver online-adaptation workflows available for review.
2. Build a comparison matrix covering cadence, sensors, geometric requests,
   coarsening, state transfer, restart/continuation, and driver-grid rebuild
   responsibilities.
3. Identify the pieces that are demonstrably solver-neutral and the pieces that
   should remain solver-owned while policies are still being explored.
4. Add a tiny test/tutorial driver that loads `fvmamr`, creates one scalar
   sensor, queries `cellRefFlagMask`, and does not run remeshing.
5. Add a driver-side guard/helper that scans an existing `refineTag` store for
   all-zero content before calling `onlineRefineMesh()`.
6. Add compact examples showing scalar, vector, and storeVec transfer/restart
   registration from a solver module.
7. Add optional overset-aware tagging as a separate rule file, because it
   depends on `FVMOverset` variables such as `componentGeometryList`,
   `componentID`, and `iblank`.

## Request Fusion And Control Questions

The module now routes the built-in normalized error marker through a
solver-neutral request representation before final `FVMAdapt` tags are emitted.
That request-fusion layer gives solvers a common place to express priorities and
commands, but it is still only the first part of a complete AMR-control model.

Remaining questions:

- How should AMR combine refinement, keep, and coarsening requests from multiple
  sources?
- Which requests are hard constraints, such as geometry protection or immersed
  boundary resolution, and which are soft optimization requests, such as error
  reduction?
- When a maximum mesh-size limit is active, how should refinement requests be
  prioritized across objectives?
- Should coarsening run before refinement when the mesh-size limit is active, or
  should both be solved together by one request-fusion policy?
- What diagnostics should be emitted so users can see which objective won when
  requests conflict?
- What solver inputs are required: per-objective priority, requested cell size,
  allowed min/max refinement level, hard/soft request type, region membership,
  or source name?

Remaining development directions:

- Extend the solver-neutral request-composition stage so hard constraints,
  target sizes, and request strengths can participate in the live policy.
- Keep final conversion to `cellRefineTag` in one place so all solvers get the
  same conflict-resolution diagnostics.
- Add optional per-source counts before and after fusion: requested refines,
  requested coarsens, blocked coarsens, mesh-limit-trimmed refines, and final
  accepted tags.
- Add FVMAdapt support for exposing whether a cell is eligible for coarsening.
  Possible data includes parent-child ancestry, local refinement level, and
  whether all siblings needed for coarsening are present.
- Keep geometry-specific request generation separate from error-indicator
  request generation, then fuse them through a shared policy.

## Extensible Fact Model

The final `cellRefineTag` should be treated as compiled output for
`FVMAdapt`, not as the main extension point. The current prototype allows
multiple rules to join `cellRefFlag`, but that collapses source, priority, and
reason too early. A better long-term model is to let rules contribute AMR
evidence and requests as ordinary Loci facts, then run one fusion stage that
turns those facts into the final refine/coarsen tag.

Useful fact layers:

- Observations: scalar facts that measure something about a cell, such as an
  error indicator, distance to a wall, distance to an immersed boundary, or
  closeness to a solver field's target value.
- Requests: source-specific refine, keep, or coarsen requests derived from one
  observation or from a small set of observations.
- Constraints: hard limits that block refinement or coarsening, such as
  protected geometry, allowed refinement levels, coarsening eligibility, or
  maximum cell count.
- Policy data: per-source thresholds, priorities, hard/soft request type, and
  requested target cell size.
- Diagnostics: per-source counts and winning-request reasons after fusion.

This suggests a rule pipeline like:

1. Solvers or helper modules compute observation facts.
2. Parametric request rules convert observations into source-specific AMR
   requests.
3. Constraint rules mask or limit those requests.
4. A fusion rule resolves conflicts and writes the one signed internal marker.
5. The existing staging rule converts the marker to the `FVMAdapt` tag encoding.

Neutral fact names used by the prototype:

- `amrRequest(SRC)`: a signed cell request from one source.
- `amrRequestStrength(SRC)`: optional normalized source strength.
- `amrDistanceToTarget(SRC)`: source-specific geometric or field distance.
- `amrSourcePriority(SRC)`: source-level priority used by fusion.
- `amrRequestTargetSize(SRC)`: optional source-specific target cell size.
- `amrFusedRequest`: the source-fused signed request that replaces direct
  writes to `cellRefFlag`.

The first implementation keeps scheduler-visible state in scalar `store<real>`
and `store<int>` facts. `storeVec` or `multiStore` can hold variable-length
per-cell objective data later, but they are harder to inspect and document.
`blackbox<T>` and custom data-schema types are reserved for policy objects or
metadata that cannot be expressed cleanly as simple Loci facts.

Distance-based AMR fits this model naturally. Stream already has a
`dist_noslip` style fact for turbulence modeling: rules compute a cell distance
from geometry and later rules consume it. AMR could use the same pattern for
geometry and solver fields. For example, a VoF solver could provide a scalar
field and a target value; a helper rule could compute an `amrIndicator` based on
distance from that value, distance in cell steps from a mask, or eventually a
true geometric distance to an isosurface. The solver still owns the field name
and physics meaning, while `FVMAMR` owns the generic request and fusion path.

The near-term compatibility path is to keep `cellRefFlag` as the final
FVMAdapt-facing marker, but treat direct joins to that fact as a low-level
escape hatch. The generic scaled-error marker now follows the intended layering:
it writes `amrRequest(scaledError)`, the fusion rules produce
`amrFusedRequest`, and only that fused request joins `cellRefFlag`. New shared
rules should follow the same source-specific evidence/request pattern.

## Request Ledger And Winner History

Overset provides a useful pattern for richer module state. It keeps named
component metadata in `blackbox` maps and vectors, then uses compact cell stores
such as `componentID` and `iblank` for per-cell decisions. AMR can follow the
same split: keep source metadata and optional debug ledgers in richer global
facts, while keeping the scheduling-critical cell facts small and easy to query.

Each objective that can request adaptation is represented as an AMR request
source. A source might be a solver error indicator, an immersed-boundary
distance rule, a VoF isosurface rule, an overset protection rule, or a user
region rule.

Candidate source metadata:

- `amrSourceNames`: ordered names for all registered request sources.
- `amrSourcePriority(SRC)`: default priority for a source.
- `amrSourceKind(SRC)`: optional category such as error, geometry, region, or
  solver-field.
- `amrSourceHardConstraint(SRC)`: hard/soft classification metadata. The live
  priority policy records this but does not yet give it special behavior.

Candidate per-cell request facts:

- `amrRequest(SRC)`: signed request from one source, using `1` for refine,
  `-1` for coarsen, and `0` for no request.
- `amrRequestStrength(SRC)`: normalized source strength reserved for richer
  fusion policies and diagnostics.
- `amrRequestTargetLevel(SRC)`: optional desired refinement level.
- `amrRequestTargetSize(SRC)`: optional desired cell size.

Candidate fusion outputs:

- `amrFusedRequest`: final signed request before translation to
  `cellRefineTag`.
- `amrWinningPriority`: priority of the source that won the fusion decision.
- `amrWinningSource`: integer source id that won the fusion decision, or `-1`
  when no source requested adaptation.
- `amrWinningCommand`: signed command chosen by the fusion rule.
- `amrConflictCount`: number of nonzero requests that disagreed with the winner.
- `amrRefineRequestCount` and `amrCoarsenRequestCount`: per-cell request
  counts useful for debugging and visualization.

The initial code sketch for this idea lives in `src/include/FVMAMR/amr`.
`AMRRequestArbiter` stores source metadata and fusion rules, while
`AMRCellRequest` and `AMRFusionResult` represent the per-cell request and
winner summary. The public `.lh` file declares the matching Loci facts so the
names can be discussed and refined before more advanced rules depend on them.
The current scheduler-visible rule path is priority-first: registered
`amrRequest(SRC)` stores are counted, the highest active `amrSourcePriority(SRC)`
selects the winning source, source id breaks priority ties, and the winning
source command becomes `amrFusedRequest`. The fused request is then joined into
the existing `cellRefFlag` path. The built-in `scaledError` source is registered
by the module, so solver-selected scalar sensors and solver-provided request
sources participate in the same fusion layer.

This is enough for normal operation without storing a full variable-length
request list on every cell. If users need to inspect every request, add an
optional debug output later. Possible debug forms include a `storeVec<int>`
indexed by source id, a `multiStore<int>` ledger containing source/command
pairs, or an external text/HDF5 report written in stable global-cell order. The
external report may be the best first diagnostic because it avoids making a
large debug structure part of the normal schedule.

History should also be separated into current-state facts and optional event
logs:

- `amrRequestedAction`: source-fused, mask-filtered action staged for
  `FVMAdapt`.
- `amrTopologyAction`: direct/refined/coarsened topology observed from the
  dynamic `c2p` relation after `onlineRefineMesh()`.
- `amrAcceptedAction`: signed action accepted by the topology operation.
- `amrCellLevel`: current per-cell refinement depth.
- `amrLastAction`: compatibility view of requested action before remeshing and
  accepted action after adapted continuation.
- `amrLastWinningSource`: source id associated with `amrLastAction`, or `-1`
  when the accepted topology action did not match a staged parent request.
- `amrLastAdaptStep`: adaptation cycle associated with `amrLastAction`.

Those facts are carried forward through the AMR parent-child mapping, but they
are not a complete geometric lineage. Refined children inherit parent history
and increment `amrCellLevel`; directly mapped cells preserve parent state; and
coarsened cells are identified through the same `c2p`/`c2pglobal` topology by
finding new cells with multiple old parents. Categorical fields such as source
id and adaptation step are transferred by deterministic parent lookup rather
than interpolation. A source id is reported only when a staged parent request
matches the topology that FVMAdapt actually accepted.

## Feedback-Driven Backlog

The following notes collect early user feedback about AMR post-processing and
related Loci/FVM tooling. Some items belong in `FVMAMR`; others likely belong in
`FVMAdapt`, VOG tooling, or solver-specific modules. They are kept here so the
module plan can steer future work without pretending that all of it should live
inside the first solver-facing AMR interface.

### Marker And FVM Common Rules

Observed issues:

- The marker tool can produce a different refinement plan when run with too
  many MPI ranks. A similar issue has been observed for VOG creation. This has
  not been observed for solver parallelization.
- Two-dimensional refinement with `rlevels` is not currently supported.

Possible development directions:

- Add a rank-count determinism test for marker output. Run the same case at a
  few MPI sizes and compare a canonical refinement-plan representation sorted
  by stable global cell identifiers.
- Audit marker and VOG creation paths for rank-local ordering assumptions. Any
  output that later drives refinement should be written in file/global order,
  not in incidental local sequence order.
- Add optional debug hashes for refinement plans and VOG entity tables so plan
  drift can be detected before a solver run consumes the data.
- Define the intended `rlevels` semantics for 2D AMR before implementing it.
  The implementation should document whether 2D means planar cell splitting,
  z-normal face selection, or a reduced-dimensional plan format.
- Keep the deterministic writer and `rlevels` support in FVMAdapt/tools unless
  a solver-neutral runtime hook is needed in `FVMAMR`.

### Named Tagged Regions

Observed issue:

- Named tagged regions appear to be stored in the VOG file as cell ID ranges.
  Those ranges are not updated after AMR changes the cell numbering.

Possible development directions:

- Treat named regions as entity sets that must be remapped through the AMR
  parent-child relation, not as static ID ranges.
- After remeshing, create updated region sets by walking `c2p` or `c2pglobal`
  from child cells back to parent cells that belonged to each original region.
- Decide on an output format for updated regions. Options include writing a new
  VOG with updated volume tags, writing a sidecar region file per adapt level,
  or writing both a human-readable summary and a machine-readable map.
- Prefer explicit entity sets or compressed sorted ranges generated after AMR
  over carrying the original raw ranges forward unchanged.
- This likely belongs in FVMAdapt/VOG I/O first. `FVMAMR` can expose helper
  names or a runtime request flag once the lower-level remapping is defined.

### MPI Partition Debug Output

Observed need:

- Because marker and VOG creation have shown MPI-size sensitivity, users want a
  debugging output that shows how the mesh was partitioned. Named tagged
  regions may be a useful way to visualize this.

Possible development directions:

- Add an optional partition-debug output that marks cells by owning MPI rank.
  This could be emitted as named regions, a cell-centered rank field, or both.
- Include enough metadata to compare runs: MPI size, partition method, local
  cell counts, global cell count, and a stable hash of each rank's global cell
  IDs.
- Make the output cheap and opt-in, for example behind an AMR/debug flag or a
  tool command-line option, so production runs do not carry extra I/O.
- Reuse the same partition-summary format in marker, VOG creation, and solver
  AMR where possible. That would make cross-tool comparisons much easier.

### Stream And VoF Follow-On Ideas

These are useful AMR ideas, but they should build on top of the solver-neutral
module rather than becoming hard-coded `FVMAMR` behavior.

Automatic buffer distance:

- Compute a recommended buffer distance or cell-step count from AMR frequency,
  refined cell size, and CFL. A starting inequality is:
  `AMR_freq < RegionDistance/(RefinedCellSize*CFL)`, or equivalently compare
  AMR frequency against `RefinedCellSteps/CFL`.
- Track whether a moving isosurface leaves its refinement region during an AMR
  cycle. Emit a warning by default and provide a stricter option that aborts the
  run.
- Use the measured distance from the isosurface to the region boundary as a
  possible dynamic AMR trigger, so adaptation cadence can become safety-driven
  rather than purely fixed-frequency.
- Keep the isosurface metric and VoF-specific region definitions in Stream/VoF.
  A solver-neutral `FVMAMR` contribution could be a generic distance-to-mask or
  warning/abort hook once more than one solver needs it.

Frozen `N`/time solution AMR:

- Expand support for AMR based on frozen solution states for both `N=0` and
  `N!=0`.
- Clarify whether this is a post-processing workflow, a solver initialization
  workflow, or an in-memory adaptation workflow. The data ownership and restart
  requirements are different in each case.
- Reuse the existing transfer/restart classes where possible. Solver-specific
  field lists and restart variable names should still stay in the solver.
- Add small regression cases that verify frozen-state marking, remeshing, and
  restart interpolation independently before combining them with transient VoF
  logic.

## Validation

Validate the module by compiling it against the active local Loci installation:

```bash
make -C src/FVMAMR LOCI_BASE=/home/wandadar/software/loci_install all
```

This runs `lpp` and compiles `fvmamr_m.so` without requiring a solver driver.
A module-load smoke can use `vogcheck -load_module fvmamr -doc` once the local
binary and library paths are rebuilt consistently.

## Open Design Questions

- Should the long-term module be named `fvmamr` or should the cleaned interface
  live under the existing `fvmadapt` module name? If this remains a solver-side
  control layer, would `fvmadaptsolver` or `fvmadaptcontrols` be clearer?
- Should `FVMAMR` build AD variants like `FVMOverset`, or stay normal-only like
  `FVMAdapt` until transfer rules need AD coverage?
- How much of the in-memory remesh loop can be wrapped without assuming a
  solver's grid reader/injection API?
- What information should `FVMAdapt` expose so solvers can make reliable
  coarsening decisions, especially whether a cell is eligible to coarsen?
- What minimum request-fusion policy is stable enough to share across solvers,
  and what prioritization should remain solver-owned?
- Which existing online-adaptation use cases should be considered mandatory
  before the solver-side helper API is treated as stable?
