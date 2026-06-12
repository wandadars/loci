# FVMAMR Solver Interface

`FVMAMR` is the solver-facing AMR module for finite-volume Loci codes. It
collects the solver-neutral parts of the in-memory AMR path while leaving
physics state, timestep control, restart naming, and mesh injection in the
calling code.

The public renderable module reference lives in `src/FVMAMR/doc/README.md`.
This file is a branch-local integration guide with the same solver contract and
additional development notes.

The module is intentionally small. It wraps the `FVMAdapt` remeshing database
with named helpers, provides generic marking rules, stages interpolation
metadata, and provides transfer/restart rule classes that a solver instantiates
with its own variable names.

This interface is for the dynamic online AMR workflow used by solvers. It is
not the XML region/marker-tool workflow documented under `src/FVMAdapt/doc`.
The solver path stages and consumes `DataXFER_DB` entries such as `refineTag`,
`currentPlan`, `nextPlan`, and `c2p`/`c2pglobal`, then calls
`onlineRefineMesh()` inside the solver driver.

## Module Boundary

`FVMAMR` owns:

- vars-file controls common to AMR marking
- generic sensor normalization and threshold marking
- geometry guards such as minimum edge length
- conversion from signed marker fields to `FVMAdapt` refine tags
- `DataXFER_DB` helper names for the common AMR entries
- pre-remesh staging of interpolation metadata
- scalar, vector, and `storeVec` transfer/restart rule classes

The solver owns:

- loading `fvmamr` at startup
- calling the AMR setup helpers after vars facts are loaded
- deciding when an adaptation query is allowed
- defining solver-specific sensor fields
- selecting which state fields must be carried across AMR
- choosing restart variable names and database keys
- calling `onlineRefineMesh()` and injecting the adapted grid
- rebuilding or advancing its own timestep loop after adaptation

`FVMAMR` should not grow default field lists such as density, pressure,
temperature, turbulence variables, or species mass fractions. Those names are
solver contracts. A solver may define a compact macro layer for its own naming
scheme, but the module-level API stays name-agnostic.

## Interface Shape

The intended interface is not that each solver reimplements AMR control. A
solver should translate its own physics into FVMAMR facts, then let the module
operate on those facts using module-owned formalisms.

The solver-facing surface should stay small:

- register request sources, such as `vofInterface`, `wallDistance`, or
  `oversetProtect`
- provide request facts such as `amrRequest(SRC)` and, when needed,
  `amrRequestStrength(SRC)` or target size/level facts
- provide solver-specific sensor or distance fields used to build those
  requests
- register solver state fields that must be transferred across remeshing
- own cadence and driver-loop recovery after remeshing

The module-owned surface can grow deeper:

- source metadata, priorities, hard/soft classification, and diagnostics
- request fusion and conflict accounting
- lineage/history facts such as `amrCellLevel` and `amrLastWinningSource`
- generic region, distance, and sensor helper rules
- conversion from fused requests to the `FVMAdapt` tag encoding
- staging of common AMR database entries and interpolation metadata

This keeps solver code focused on producing evidence and state-transfer
registrations. It keeps the AMR decision machinery, diagnostics, and low-level
`FVMAdapt` contract in one module.

## Plain Interface Questions

### How does the module decide what input controls refinement?

Each input is a request source. A source writes `amrRequest(SRC)` for each
cell, using `1` for refine, `-1` for coarsen, and `0` for no request.
`FVMAMR` considers the active nonzero requests for that cell, chooses the
highest-priority source, breaks priority ties with the source id stored in
`amrSourceID(SRC)`, and writes the winner to `amrFusedRequest`. The source id
is a stable integer assigned to a request source; it is not a cell id.

### How are multiple decision inputs defined?

Multiple inputs are parametric facts keyed by source name. Source metadata is
stored as `amrRequestSource(SRC)`, `amrSourceID(SRC)`,
`amrSourcePriority(SRC)`, `amrSourceKind(SRC)`, and
`amrSourceHardConstraint(SRC)`. A solver normally creates these metadata facts
by registering the source, not by writing each fact directly.

For the current priority-fusion policy, a custom source needs:

- registration metadata: `amrRequestSource(SRC)`, `amrSourceID(SRC)`, and
  `amrSourcePriority(SRC)`
- one per-cell command: `amrRequest(SRC)`

The optional facts `amrRequestStrength(SRC)`, `amrRequestTargetLevel(SRC)`, and
`amrRequestTargetSize(SRC)` are reserved for richer policies and diagnostics.
A simple source does not need to provide them.

`SRC` is the source name substituted into the parametric fact. If the source is
named `vofInterface`, the concrete facts are
`amrRequestSource(vofInterface)`, `amrRequest(vofInterface)`,
`amrSourceID(vofInterface)`, and so on.

For this interface, `SRC` is a schedule-time name, not a per-cell store. The
cell data lives in the full fact `amrRequest(vofInterface)`. There does not
need to be a separate store named `vofInterface` unless the solver chooses to
create one for its own physics.

This is the same selector-style parametric pattern used by facts such as
`volumeTag(X)`. It differs from field-style parametric rules where the
parameter also names a solver store and the rule reads `$X`. The scaled-error
sensor path uses the field-style form: `adaptSensor(Perror)` selects the solver
store `Perror`, and the normalization rule reads `$Perror`. A request source
uses `SRC` as a label unless the solver separately makes a store with that
same name.

The order of operations is:

1. Startup code registers the source and creates facts such as
   `amrRequestSource(vofInterface)` and `amrSourcePriority(vofInterface)`.
2. Rules with `parametric(amrRequestSource(SRC))` are instantiated for
   `SRC=vofInterface`.
3. Solver or helper rules compute `amrRequest(vofInterface)` on cells.
4. Fusion rules compare that request with requests from other registered
   sources.

The source id is the integer label used in per-cell winner/history stores such
as `amrWinningSource`, `amrRequestedWinningSource`, and
`amrAcceptedWinningSource`. The source name is the readable parametric key; the
source id is the compact value stored in cell data. Source ids must be unique
across active sources. The final selected request is recovered by matching
`amrSourceID(SRC)`, so duplicate ids can make the selected command ambiguous.

The code that registers a source sets `amrSourceID(SRC)`. The built-in
`scaledError` source is registered by the module with id `0`. For solver-owned
sources, `createAMRRequestSourceFacts(facts, "vofInterface", 1, 10, ...)`
creates `amrSourceID(vofInterface)=1` and
`amrSourcePriority(vofInterface)=10`. Vars-file helper sources may provide an
explicit `id`; if omitted, the setup helper assigns one.

The source name should usually describe the AMR objective rather than the raw
solver variable. A solver may compute a field called `VOFRefineFlag`, but
register the AMR source as `vofInterface`:

```cpp
Loci::createAMRRequestSourceFacts(facts,
  "vofInterface", 1, 10, "solver-field", false) ;
```

The solver then translates its own evidence into the module request:

```cpp
$rule pointwise(amrRequest(vofInterface)<-VOFRefineFlag),
constraint(geom_cells) {
  if($VOFRefineFlag > 0) {
    $amrRequest(vofInterface) = 1 ;
  } else {
    $amrRequest(vofInterface) = 0 ;
  }
}
```

`VOFRefineFlag` remains a solver fact. `amrRequest(vofInterface)` is the
solver-neutral request fact that enters AMR fusion.

## Custom Request Source Checklist

Use this path when the solver has already formed a refinement objective or a
cell flag. Use the built-in scaled-error path instead when the solver wants
`FVMAMR` to normalize scalar error metrics statistically.

For each custom source:

1. Pick a source name, such as `vofInterface`, `shockSensor`, `wallDistance`, or
   `oversetProtect`.
2. Register that source before the AMR query schedule is built.
3. Write a Loci rule that produces `amrRequest(SRC)` on `geom_cells`.

The request fact type is:

```loci
$type amrRequest(SRC) store<int> ;
```

The valid command values are:

- `1`: request refinement
- `0`: request no mesh change
- `-1`: request coarsening

The implementation normalizes signed integers before fusion, so positive means
refine and negative means coarsen. Solver rules should still write `1`, `0`,
or `-1` to keep diagnostics clear. The solver does not write the final
`FVMAdapt` tag; `FVMAMR` converts the fused signed command to
`1=refine`, `0=keep`, and `2=de-refine`.

A `-1` command is a coarsening request, not proof that FVMAdapt can legally
coarsen the cell. Solvers that depend on coarsening should inspect accepted
topology facts such as `amrAcceptedAction`, `amrLineageParentCount`, and
`amrCellLevel` after remeshing.

The preferred startup form is:

```cpp
Loci::createAMRRequestSourceFacts(facts,
  "vofInterface", 1, 10, "solver-field", false) ;
```

For fixed sources compiled into a solver module, equivalent `.loci` default
rules are also valid:

```cpp
$rule default(amrRequestSource(vofInterface)) {
  $amrRequestSource(vofInterface) = true ;
}

$rule default(amrSourceID(vofInterface)) {
  $amrSourceID(vofInterface) = 1 ;
}

$rule default(amrSourcePriority(vofInterface)) {
  $amrSourcePriority(vofInterface) = 10 ;
}

$rule default(amrSourceKind(vofInterface)) {
  $amrSourceKind(vofInterface) = "solver-field" ;
}

$rule default(amrSourceHardConstraint(vofInterface)) {
  $amrSourceHardConstraint(vofInterface) = false ;
}
```

Use the C++ helper for vars-driven sources, for helper sources that need grid
facts, and for source lists that are not known when the `.loci` module is
compiled. In Stream, the AMR fact setup runs from a grid post-processing hook
after grid facts are available and is repeated after an inline AMR rebuild.

Registration is part of schedule setup. `createAMRRequestSourceFacts()` mutates
the `fact_db`, so it should be called from application setup code before
`makeQuery()` builds the schedule. It is not intended to be called from an
ordinary pointwise rule while the schedule is executing.

For sources known at compile time, fixed `.loci` default rules are the in-module
registration mechanism. For sources selected from vars files or derived from
grid facts, setup C++ creates the metadata facts before the query is built. To
turn a registered source off during a run, write `0` from `amrRequest(SRC)`.
Adding a new source name requires another setup pass and a rebuilt query
schedule.

The practical rule is: source names are static for a built query, request values
are live data. A solver can change `amrRequest(vofInterface)` every timestep and
every cell, but it should not create a new `vofInterface` source from inside the
running schedule. Register the possible sources before query construction, then
let their request rules write `1`, `0`, or `-1` as conditions change.

`setupAdaptSensorFacts()` is the historical helper name. It now seeds all
FVMAMR setup facts, including structured metric selectors and request-source
option lists. New solver code may call the clearer alias
`setupFVMAMRFacts()`.

### Solver-Facing Fact Summary

These facts are the main AMR interface points a solver may need to provide or
consume.

| Fact | Who provides it | When | Purpose |
|---|---|---|---|
| `amrRequestSource(SRC)` | registration helper or fixed solver default | before schedule build | enables source-parametric fusion rules |
| `amrSourceID(SRC)` | registration helper or fixed solver default | before schedule build | stable integer reported in winner/history diagnostics |
| `amrSourcePriority(SRC)` | registration helper or fixed solver default | before schedule build | priority used by the current fusion policy |
| `amrSourceKind(SRC)` | registration helper or fixed solver default | before schedule build | source category metadata for diagnostics |
| `amrSourceHardConstraint(SRC)` | registration helper or fixed solver default | before schedule build | hard/soft metadata; ignored by the stock priority policy |
| `amrRequest(SRC)` | solver rule or module helper | during schedule execution | per-cell signed command: `1`, `0`, or `-1` |
| `amrRequestStrength(SRC)` | optional solver rule | during schedule execution | reserved for richer policies; ignored by the stock priority policy |
| `amrRequestTargetLevel(SRC)` | optional solver rule | during schedule execution | reserved target refinement level; ignored by the stock priority policy |
| `amrRequestTargetSize(SRC)` | optional solver rule | during schedule execution | reserved target cell size; ignored by the stock priority policy |
| `adaptSensor(NAME)` | `setupFVMAMRFacts()` | before schedule build | enables built-in scaled-error normalization for a scalar metric store |
| `adaptSensorWeight(NAME)` | `setupFVMAMRFacts()` or solver setup | before schedule build | weight for the selected scaled-error metric |
| selected sensor store `NAME` | solver rule or generated metric helper | during schedule execution | cell-centered scalar metric consumed by `adaptSensor(NAME)` |
| `amrDistanceToTarget(SRC)` | solver rule or boundary-distance helper | during schedule execution | cell-centered distance consumed by distance request sources |
| `adaptSensorMetrics` | solver vars/setup | before `setupFVMAMRFacts()` | structured request for module-generated metric stores |
| `amrBoundaryDistanceSources` | solver vars/setup | before `setupFVMAMRFacts()` | structured request for named-boundary distance sources |
| `amrConstantRequestSources` | solver vars/setup | before `setupFVMAMRFacts()` | debug/regression request sources with constant commands |
| `do_adapt` | solver cadence rule | during schedule execution | tells FVMAMR to stage tags and transfer metadata |
| `refineLevel` | solver driver | before marking and adapted-continuation queries | adaptation-cycle id used in requested/accepted history |
| `refineRestart` | solver driver | after remeshing | selects adapted-continuation interpolation rules |
| `cellPartitionWeights` | optional solver rule | during staging | repartitioning hints; FVMAMR supplies unit weights by default |

New solver code should normally add `amrRequest(SRC)` sources rather than direct
`cellRefFlag` rules. Direct marker writes bypass source diagnostics and the
shared fusion policy.

The request-source interface is the main boundary:

- A request source is one objective that may ask for mesh change, such as a
  flow-error sensor, a VoF interface distance, an immersed-boundary distance, an
  overset protection rule, or a named region.
- A source writes generic AMR facts. The command `amrRequest(SRC)` is required;
  strength, target level, and target size are optional policy inputs.
- `FVMAMR` fuses those generic facts into one signed command per cell, then
  translates that command to the `FVMAdapt` tag encoding.

This lets solvers add any number of objectives without teaching `FVMAMR` the
solver's variable names or physics. The solver owns the meaning of its source;
the module owns the bookkeeping, conflict accounting, and final tag generation.

## Startup Contract

A solver loads the module like any other Loci module:

```c++
Loci::load_module("fvmamr", rdb) ;
```

After vars facts are loaded and before the first AMR-capable query is made, the
solver calls the setup helpers. If the case uses grid-dependent setup such as
`amrBoundaryDistanceSources`, call `setupFVMAMRFacts()` after grid and boundary
facts are present:

```c++
Loci::setupFVMAMRFacts(facts) ;
int adaptMode = Loci::configureFVMAdaptGlobals(facts) ;
```

`setupFVMAMRFacts()` expands a vars entry such as:

```text
adaptSensor: Perror,Terror
```

into boolean facts named `adaptSensor(Perror)` and `adaptSensor(Terror)`. The
parametric marking rules use those facts to decide which solver-provided sensor
stores participate in the generic marker.

It also expands the structured metric form:

```text
adaptSensorMetrics: <p=[type=scalar,kernel=faceJump,weight=1.0],
                     v=[type=vector,kernel=faceJump,weight=0.5]>
```

This form lets the module build common scalar metrics from solver fields. The
example creates facts selecting `amrScalarFaceJump(p)` and
`amrVectorFaceJump(v)`, then selects those generated stores for the built-in
scaled-error path. The first supported kernel is `faceJump`, with field types
`scalar`, `vector`, and `mvector`.

`configureFVMAdaptGlobals()` reads `adaptMode`, `adaptMinEdgeLength`, and
`adaptFaceFold`, updates the low-level `FVMAdapt::Globals` values, and returns
the split-mode integer expected by `onlineRefineMesh()`.
`adaptBalanceType` is currently reserved compatibility metadata; this helper
does not pass it into `FVMAdapt`.

`refineLevel` is the AMR cycle id, not the per-cell refinement depth. Set or
increment it before the marking query that stages `do_adapt`, then carry the
same cycle id into the adapted-continuation query together with
`refineRestart`. Per-cell depth is tracked separately by `amrCellLevel`.

## Marking Contract

The generic marker expects cell-centered scalar sensor stores. The solver may
provide those stores directly and name them however it wants, or it may request
module-generated stores through `adaptSensorMetrics`. When a sensor name is
enabled by `adaptSensor(NAME)`, `FVMAMR` computes:

- `cellMean(NAME)`
- `cellSigmaSq(NAME)`
- `totScaledError`
- `amrRequest(scaledError)`
- `amrFusedRequest`
- `cellRefFlag`
- `cellRefFlagMask`
- `cellRefineTag`

`cellRefFlag` uses the signed convention:

- `1`: refine
- `0`: keep
- `-1`: coarsen

`cellRefineTag` is the `FVMAdapt` convention:

- `1`: refine
- `0`: keep
- `2`: de-refine

The built-in scaled-error marker is represented as the request source
`amrRequest(scaledError)`. It does not write directly to `cellRefFlag`.
`cellRefFlag` is the final marker consumed by the database staging path after
request fusion has produced `amrFusedRequest`.

Solvers should normally add their own AMR objectives by registering request
sources and writing `amrRequest(SRC)` rules. Direct `apply(cellRefFlag<-...)`
rules are still possible as a low-level escape hatch, but they bypass source
diagnostics and the shared priority policy. Geometry protection is applied later
through `cellRefFlagMask`, so request rules do not need to repeat the
minimum-edge-length guard.

`adaptTagsActive` is a convenience constraint. It is present only when the
current marking pass selected at least one cell for refinement or coarsening.
Solvers can use it to avoid requesting no-op AMR cycles.

## Request Source Contract

The newer request-source path gives solvers a smaller surface than writing
directly to `cellRefFlag`. A solver registers each objective, then provides a
cell-centered request for that source:

```c++
Loci::createAMRRequestSourceFacts(facts,
  "vofInterface", 1, 10, "solver-field", false) ;
```

The arguments are source name, source id, priority, kind, and hard/soft
classification metadata. The stock `.loci` fusion policy records that metadata
but uses only priority and source id. The helper creates facts such as
`amrRequestSource(vofInterface)`, `amrSourceID(vofInterface)`, and
`amrSourcePriority(vofInterface)`. Source id `0` is reserved for the built-in
`scaledError` source.

The core request facts are:

- `amrRequest(SRC)`: required signed command, with `1` for refine, `-1` for
  coarsen, and `0` for no change.
- `amrRequestStrength(SRC)`: optional normalized strength reserved for richer
  policies and diagnostics.
- `amrRequestTargetLevel(SRC)`: optional desired refinement level reserved for
  richer policies and diagnostics.
- `amrRequestTargetSize(SRC)`: optional desired cell size reserved for richer
  policies and diagnostics.

The core source metadata are:

- `amrSourceID(SRC)`: stable integer id for diagnostics and winner tracking.
- `amrSourcePriority(SRC)`: source priority for policy-driven arbitration.
- `amrSourceKind(SRC)`: human-readable category such as `error`, `geometry`,
  `region`, or `solver-field`.
- `amrSourceHardConstraint(SRC)`: hard/soft classification metadata. The stock
  `.loci` policy records it but does not use it for arbitration.

The solver then writes a normal Loci rule for its source:

```c++
$rule pointwise(amrRequest(vofInterface)<-vofError),constraint(geom_cells) {
  if($vofError > 1.0) {
    $amrRequest(vofInterface) = 1 ;
  } else if($vofError < -0.5) {
    $amrRequest(vofInterface) = -1 ;
  } else {
    $amrRequest(vofInterface) = 0 ;
  }
}
```

Distance-driven objectives can use a narrower helper when the solver already
has, or can compute, a distance field. For example, a solver that knows the
distance from each cell to the named boundary `cylinder` can register a source
like this:

```c++
Loci::createAMRDistanceRequestSourceFacts(facts,
  "cylinderDistance", 2, 20, "geometry", false,
  0.002, -1.0, -1.0, 0.006, "cylinder") ;
```

The four threshold arguments are:

- refine if distance is less than the first value
- refine if distance is greater than the second value
- coarsen if distance is less than the third value
- coarsen if distance is greater than the fourth value

Negative thresholds are disabled. The example therefore refines near the named
boundary and coarsens far away from it. The solver supplies the distance store:

```c++
$rule pointwise(amrDistanceToTarget(cylinderDistance)<-distToCylinder),
constraint(geom_cells) {
  $amrDistanceToTarget(cylinderDistance) = $distToCylinder ;
}
```

This helper is solver-agnostic at the request level. It does not require the
module to know how `distToCylinder` was built. A solver may derive that field
from existing wall-distance data, a boundary-name lookup, an immersed-boundary
distance function, or another mesh search.

For ordinary VOG boundaries with names, the module can build the nearest-face
distance input itself. The boundary argument is the mesh boundary name recorded
in `boundary_names`; it is usually the same name used as a
`boundary_conditions` key.

```c++
Loci::createAMRBoundaryDistanceSourceFacts(facts,
  "cylinderDistance", "cylinder", 2, 20, false,
  0.002, -1.0, -1.0, 0.006) ;
```

This helper registers the same distance request source, searches the mesh facts
for faces on boundary `cylinder`, creates
`amrBoundaryFaces(cylinderDistance)` and
`amrClosestBoundaryFace(cylinderDistance)`, and lets the `.loci` rule compute
`amrDistanceToTarget(cylinderDistance)`. It should be called only after the
grid and boundary facts exist, and repeated after rebuilding facts on an
adapted grid.

Solvers that call `Loci::setupFVMAMRFacts(facts)` after grid facts are
available can expose this helper through a vars-file option:

```text
amrBoundaryDistanceSources:
  <cylinderDistance=[boundary=cylinder,
                     id=2,
                     priority=20,
                     refineLessThan=0.002,
                     coarsenGreaterThan=0.006]>
```

The source name is the top-level option name. The value provides the mesh
boundary name, source metadata, and any enabled thresholds. The long threshold
names may also be written as `refineLT`, `refineGT`, `coarsenLT`, and
`coarsenGT`. `amrNamedBoundaryDistanceSources` is still accepted as a
compatibility spelling; setup still emits the canonical
`amrBoundaryDistanceSource(SRC)` selector.

For focused regression tests, the module also accepts constant request sources
from the case deck:

```text
amrConstantRequestSources: <lowRefine=[command=refine,id=10,priority=0],
                           highCoarsen=[command=coarsen,id=20,priority=10]>
```

These entries create ordinary `amrRequest(SRC)` facts and source metadata. They
are useful for testing arbitration behavior without adding solver-specific test
rules.

`FVMAMR` applies a priority-first scheduler-visible fusion policy. For each
cell, the highest-priority active source wins. If multiple active sources have
the same winning priority, the lowest `amrSourceID(SRC)` wins as a deterministic
tie break. The winning source's command then becomes `amrFusedRequest`, and that
fused request is joined into `cellRefFlag`.

This means the source is chosen before the command. A high-priority coarsening
source can therefore beat a lower-priority refinement source. The built-in
`scaledError` source uses this same path, so solver-provided objectives and the
generic sensor marker are fused consistently.

The richer `AMRRequestArbiter` data structure is available in C++ for future
policy work involving strength, hard constraints, equal-vote behavior, or source
ordering. The live `.loci` policy currently uses source priority and source id.

## Decision And Lineage Ledger

The request-source path also builds a small per-cell decision ledger. Solvers
can use these stores for diagnostics, output, and restart handoff without
writing their own AMR bookkeeping.

Fusion diagnostics:

- `amrFusedRequest`: signed command selected before geometry masks.
- `amrWinningCommand`: command selected by fusion.
- `amrWinningPriority`: priority of the source selected by fusion.
- `amrWinningSource`: source id reported for the fused command.
- `amrRefineRequestCount` and `amrCoarsenRequestCount`: number of active
  sources requesting each action.
- `amrConflictCount`: number of active sources opposing the fused command.

Requested-decision facts:

- `amrRequestedAction`: signed action staged after `cellAdaptMask` is applied.
- `amrRequestedWinningSource`: source id associated with the staged action, or
  `-1` when the cell has no request.
- `amrRequestedAdaptStep`: `refineLevel` associated with the staged action, or
  `-1` when the cell has no request.

Accepted-topology facts:

- `amrTopologyAction`: signed topological action observed from FVMAdapt's
  dynamic `c2p` relation.
- `amrLineageMapped`: nonzero when the current cell appears in the lineage map.
- `amrLineageParentCount`: number of previous cells mapped to the current cell.
- `amrLineageChildCount`: number of current cells produced by this cell's
  previous parent.
- `amrAcceptedAction`: signed action accepted by the topology operation.
- `amrAcceptedWinningSource`: source id associated with the accepted action.
- `amrAcceptedAdaptStep`: adaptation step associated with the accepted action.
- `amrCellLevel`: per-cell refinement depth carried through accepted lineage.

The compatibility facts `amrLastAction`, `amrLastWinningSource`, and
`amrLastAdaptStep` mirror requested facts on ordinary marking queries and
accepted topology facts on adapted-continuation queries.

Accepted source and step values are categorical history, not interpolated
solution data. `FVMAMR` copies them from staged parent facts only when the
parent request matches the actual topology action observed after remeshing. If
FVMAdapt changes topology without a matching parent request, the accepted source
id remains `-1`; the accepted step falls back to the current adaptation cycle.
`amrCellLevel` is carried by the same lineage map: refined children increment
the parent level, direct cells preserve it, and coarsened cells use the minimum
parent level minus one, clamped at zero.

`currentPlan` and `nextPlan` remain opaque `FVMAdapt` database entries. Solvers
should query the lineage facts above rather than depending on the plan encoding.

## Adaptation Request

`do_adapt` is solver-owned. When it is true for a query, `FVMAMR` stages the
data that the driver needs for remeshing:

- `cellPartitionWeights`
- `refineTag`
- requested AMR history: `amrRequestedAction`,
  `amrRequestedWinningSource`, `amrRequestedAdaptStep`, and `amrCellLevel`
- `gradCells`
- `deltas`
- `vol`

The default `cellPartitionWeights` value is `1` on every geometric cell. A
solver can replace that rule if it has better repartitioning weights.

If no cells are marked, `FVMAMR` deletes the staged refine-tag entry and prints
a no-op message. The driver should still guard the remesh call by checking that
`AMR_DB_REFINE_TAG` exists and contains at least one nonzero tag.

## Database Keys

Use the enum helpers for module/FVMAdapt keys:

```c++
Loci::storeRepP tags =
  Loci::getAMRDBItem(Loci::AMR_DB_REFINE_TAG) ;
Loci::deleteAMRDBItem(Loci::AMR_DB_REFINE_TAG) ;
```

Use string keys for solver field data:

```c++
Loci::replaceAMRDBItem("pressure", pressureStore) ;
Loci::storeRepP pressure = Loci::getAMRDBItem("pressure") ;
```

The enum is the shared AMR infrastructure contract. Solver field keys are
deliberately strings so each code can match its own restart and time-level
scheme. Choose keys that are unique within the solver's AMR handoff:
`replaceAMRDBItem()` deletes any existing value for the same key, and restart
rules consume and delete their keys after interpolation.

## Transfer And Restart Rules

Before remeshing, transfer rules stage parent-mesh field values in file-order
distribution. After remeshing, restart rules interpolate those staged values to
the adapted mesh through `AMRInterpolationMapping`.

The module provides three transfer base classes:

- `Loci::AMRTransferScalar`
- `Loci::AMRTransferVector`
- `Loci::AMRTransferStoreVec`

and three restart base classes:

- `Loci::AMRRestartScalar`
- `Loci::AMRRestartVector`
- `Loci::AMRRestartStoreVec`

The public macros only register small derived rule classes. They do not impose
field names:

```c++
FVMAMR_TRANSFER_SCALAR_RULE(AMRTransferPressure,
  "p{n,it}", "p{n}",
  "pressure", "pressurePrev",
  "UNIVERSE{n,it}", "do_adapt{n,it}",
  "geom_cells{n,it}", "OUTPUT{n,it}") ;

FVMAMR_RESTART_SCALAR_RULE(AMRRestartPressure,
  "restart::p_ic", "pressure",
  "UNIVERSE", "amr_restart", "geom_cells",
  "refineRestart", "AMRInterpolationMapping") ;
```

A solver with many similarly named fields can wrap these in solver-local
macros. For example, a code whose current and previous time levels are always
`X{n,it}` and `X{n}` could define:

```c++
#define MY_AMR_TRANSFER_SCALAR(X,C,K) \
  FVMAMR_TRANSFER_SCALAR_RULE(AMRTransfer_##X,#X "{n,it}",#X "{n}", \
    K,K "Prev",#C "{n,it}","do_adapt{n,it}", \
    "geom_cells{n,it}","OUTPUT{n,it}")
```

That wrapper belongs in the solver module, not in `FVMAMR`, because the time
level names, restart names, constraints, and field list are solver-specific.

## Driver Loop

The driver remains responsible for the remesh loop. A typical flow is:

1. Build the query.
2. Set or increment `refineLevel` for the AMR cycle about to be marked.
3. Check whether the solver requested an AMR cycle.
4. Read `AMR_DB_REFINE_TAG`.
5. Skip or abort if no active tags exist.
6. Call `onlineRefineMesh()`.
7. Rebuild the fact database on the adapted grid.
8. Carry the same `refineLevel` and set `refineRestart`.
9. Call `setupFVMAMRFacts()` again on the rebuilt facts.
10. Query again so `AMRInterpolationMapping` and restart rules run.

The module does not know how a solver stores its current timestep, residual
state, restart cadence, grid reader state, or output controls. Those remain in
the driver.

## Validation Checklist

For a solver integration, inspect the generated schedule in addition to checking
exit status. A minimal AMR-capable schedule should show:

- `cellRefFlagMask -> cellRefineTag`
- `amrTagCount <- cellRefineTag`
- `OUTPUT <- cellRefineTag,amrTagCount` conditional on `do_adapt`
- transfer rules for every field the solver needs after remeshing
- `AMRInterpolationMapping` constrained by `amr_restart`
- restart rules consuming the same string keys staged by the transfer rules

If the request-source path is used, the schedule should also show:

- `amrWinningPriority <- amrRequest(SRC),amrSourcePriority(SRC)`
- `amrWinningSource <- amrWinningPriority,amrRequest(SRC),amrSourceID(SRC),amrSourcePriority(SRC)`
- `amrFusedRequest <- amrWinningSource,amrRequest(SRC),amrSourceID(SRC)`
- `cellRefFlag <- amrFusedRequest`
- `amrRefineRequestCount` and `amrCoarsenRequestCount` reductions

At runtime, the log should show `fvmamr_m.so` loaded, an AMR request, a completed
remesh, adapted grid injection, and continuation on the adapted query.

## Extension Points

Good module-level extensions are solver-neutral:

- region or shape masks that contribute to `cellAdaptMask` or `amrRequest(SRC)`
- additional generic sensor kernels
- helpers that validate staged tag data before remeshing
- additional typed wrappers for shared `FVMAdapt` database keys
- new field container transfer/restart classes when the container type is
  broadly useful across solvers

Solver-specific extensions should stay in the consuming code:

- physics metric definitions
- default state field lists
- output or restart naming conventions
- timestep recovery logic
- code-specific macro wrappers
