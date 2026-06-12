@page fvmamr_module FVMAMR Module

# FVMAMR Module

`FVMAMR` is a solver-facing AMR control module for finite-volume Loci
applications. It sits above the existing `FVMAdapt` remeshing machinery and
collects solver-neutral AMR facts, request-composition rules, diagnostics, and
database staging in one loadable module while leaving physics-specific state
and driver-loop recovery in the calling solver.

The module should be treated as a control layer, not as a replacement for
`FVMAdapt`. `FVMAdapt` still owns mesh refinement, de-refinement, balancing,
parent-child maps, interpolation primitives, and `onlineRefineMesh()`.

This document covers the solver-coupled online AMR path. That path exchanges
adaptation state through `DataXFER_DB` entries such as `refineTag`,
`currentPlan`, `nextPlan`, and `c2p`/`c2pglobal`, and is driven by solver
queries and calls to `onlineRefineMesh()`.

## Quick Interface Answers

### How does the module decide which input controls a cell?

Each input is a request source. For each cell, an active source writes a signed
`amrRequest(SRC)`: `1` refines, `-1` coarsens, and `0` makes no request.
The scheduler-visible policy chooses the highest-priority nonzero source for
that cell. If two active sources have the same priority, the lower
source id wins as a deterministic tie break. The source id is the stable
integer stored in `amrSourceID(SRC)` when the source is registered; it is not a
cell id. The winning source's signed command becomes `amrFusedRequest`, which
is then translated into the final `FVMAdapt` marker.

### How are multiple decision inputs represented?

Multiple inputs are represented as parametric Loci facts keyed by the source
name `SRC`. Source metadata is stored in facts such as
`amrRequestSource(SRC)`, `amrSourceID(SRC)`, `amrSourcePriority(SRC)`,
`amrSourceKind(SRC)`, and `amrSourceHardConstraint(SRC)`. A solver normally
does not write these metadata facts by hand; it registers the source with a
helper such as `createAMRRequestSourceFacts()`.

For the current priority-fusion policy, the required live inputs for a custom
source are:

- metadata created by registration: `amrRequestSource(SRC)`,
  `amrSourceID(SRC)`, and `amrSourcePriority(SRC)`
- the per-cell command written by the solver or a module helper:
  `amrRequest(SRC)`

Other per-cell facts such as `amrRequestStrength(SRC)`,
`amrRequestTargetLevel(SRC)`, and `amrRequestTargetSize(SRC)` are optional
policy inputs. They define the vocabulary for richer arbitration, but a simple
solver source does not need to provide them.

`SRC` is the source name substituted into the parametric fact. For example,
`amrRequest(vofInterface)` is the request fact for the source named
`vofInterface`; `amrSourcePriority(wallDistance)` is the priority fact for the
source named `wallDistance`.

In this interface, `SRC` is not a per-cell store and its value is not read from
the mesh. It is a schedule-time name used to create a family of facts. The
store with cell values is the whole fact `amrRequest(vofInterface)`, not a
separate store named `vofInterface`.

This is a selector-style parametric use, similar to rules instantiated from
`volumeTag(X)`. It is different from field-style parametric rules where the
parameter also names a solver store and the rule reads `$X` as cell data. The
scaled-error sensor path uses that field-style pattern: `adaptSensor(Perror)`
selects the solver store `Perror`, and the rule reads `$Perror`. The request
source path uses `SRC` only as the source label unless the solver separately
chooses to create a store with the same name.

The order is:

1. Startup code registers the source and creates facts such as
   `amrRequestSource(vofInterface)` and `amrSourcePriority(vofInterface)`.
2. Parametric fusion rules using `parametric(amrRequestSource(SRC))` are
   instantiated for `SRC=vofInterface`.
3. Solver or helper rules compute the cell store `amrRequest(vofInterface)`.
4. Fusion rules compare `amrRequest(vofInterface)` with requests from other
   registered sources.

The source id is separate from the source name because the fusion ledger stores
the winning source in integer cell stores such as `amrWinningSource`,
`amrRequestedWinningSource`, and `amrAcceptedWinningSource`. Those integer
stores are cheap to combine, output, and carry through AMR history. The source
name remains the readable key used to instantiate parametric facts and rules.
Source ids must be unique across active sources. The final selected request is
recovered by matching `amrSourceID(SRC)`, so duplicate ids can make the selected
command ambiguous.

The code that registers a source is responsible for setting
`amrSourceID(SRC)`. The built-in `scaledError` source is registered by this
module with id `0`. A solver source registered as
`("vofInterface", 1, 10, ...)` creates `amrSourceID(vofInterface)=1` and
`amrSourcePriority(vofInterface)=10`. Vars-file helper sources may provide an
explicit `id`; if omitted, the setup helper assigns one.

The source name does not have to be the name of a solver field or flag. It is
usually better to name the objective, then let solver-owned facts provide the
evidence. For example, a solver might compute a cell-centered
`VOFRefineFlag`; the AMR source can still be named `vofInterface`:

```cpp
Loci::createAMRRequestSourceFacts(facts,
  "vofInterface", 1, 10, "solver-field", false) ;
```

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

Here `VOFRefineFlag` is solver data. `amrRequest(vofInterface)` is the generic
request consumed by `FVMAMR`.

### Custom Request Source Checklist

Use a custom request source when the solver has already reduced some physics or
geometry criterion to a mesh-action decision. Use `adaptSensor` or
`adaptSensorMetrics` instead when the solver wants the built-in statistical
scaled-error path to normalize scalar error metrics.

For each custom source, the solver does three things:

1. Choose a source name that describes the objective, such as `vofInterface`,
   `shockSensor`, `wallDistance`, or `oversetProtect`.
2. Register that source before the AMR query schedule is built.
3. Provide a rule that writes `amrRequest(SRC)` on `geom_cells`.

`amrRequest(SRC)` is declared by this module as:

```loci
$type amrRequest(SRC) store<int> ;
```

The valid command convention is:

- `1`: request refinement
- `0`: request no mesh change
- `-1`: request coarsening

The fusion code normalizes signed integers, so positive values are treated as
refinement and negative values as coarsening, but solver rules should write
`1`, `0`, or `-1` for clarity. Solvers do not write the final `FVMAdapt` tag.
`FVMAMR` converts the fused signed command to the `FVMAdapt` convention
`1=refine`, `0=keep`, and `2=de-refine`.

A `-1` command is a coarsening request, not proof that FVMAdapt can legally
coarsen the cell. Solvers that depend on coarsening should inspect accepted
topology facts such as `amrAcceptedAction`, `amrLineageParentCount`, and
`amrCellLevel` after remeshing.

A fixed source can be registered in startup C++:

```cpp
Loci::createAMRRequestSourceFacts(facts,
  "vofInterface", 1, 10, "solver-field", false) ;
```

or by fixed `.loci` defaults when the source is compiled into the solver:

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

The C++ helper is preferred for sources selected from vars files, sources
created after grid facts exist, and sources that need mesh lookup during setup.
In Stream, this fact setup is run from a grid post-processing hook after grid
facts are available and is rerun after inline AMR rebuilds.

Registration is a schedule-building step, not an in-schedule cell operation.
`createAMRRequestSourceFacts()` mutates the `fact_db`, so it is called from
application setup code before `makeQuery()` builds the AMR-capable schedule. A
normal pointwise Loci rule should not call it while the schedule is executing.

If a solver knows a source at compile time, the solver can register that source
entirely with fixed `.loci` default rules as shown above. If the source list
comes from a vars file or depends on grid facts, use setup C++ or an application
post-grid hook to create the metadata facts before the query is made. If a
source should become inactive during a run, keep the source registered and make
its `amrRequest(SRC)` rule write `0` when it has no request. New source names
require a new fact setup pass and a rebuilt query schedule.

In other words, source names are static for a built query, while request values
are dynamic. The solver may compute `amrRequest(vofInterface)` differently on
every timestep and every cell, but it should not try to create a brand-new
`vofInterface` source from inside the running schedule. Register a finite set of
possible sources before query construction, then let their request rules decide
whether they are active by writing `1`, `0`, or `-1`.

`setupAdaptSensorFacts()` is the historical helper name. It now seeds all
FVMAMR setup facts, including structured metric selectors and request-source
option lists. New solver code may call the clearer alias
`setupFVMAMRFacts()`.

### Solver-Facing Fact Summary

These facts are the main AMR interface points a solver may need to know about.

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

New solver code should usually add `amrRequest(SRC)` sources rather than direct
`cellRefFlag` rules. Direct marker writes bypass source diagnostics and the
shared fusion policy.

## Source Layout

The module keeps public fact declarations in `src/include/FVMAMR/amr.lh`.
Core adaptation controls, scaled-error sensors, request fusion, and diagnostics
live in `adaption.loci`. Distance-based request sources live in
`distance.loci`. Restart and field-transfer staging remain in `restart.loci`
and `transfer.loci`.

## Relationship To Existing FVMAdapt Notes

`src/FVMAdapt/doc` contains older notes for the marker XML region input. Those
files describe how region expressions are built from shapes, transforms, and
set operations before the low-level adaptation tool marks cells.

`FVMAMR` has a different purpose. It documents the dynamic solver-side AMR
interface: how a Loci solver supplies generic adaptation evidence, how the
module turns that evidence into signed requests, and how those requests are
staged for `FVMAdapt`.

## Core Concepts

The AMR module uses a small vocabulary so solver-specific refinement logic can
be translated into solver-neutral facts.

### Solver Field

A solver field is a physics variable owned by the calling code, such as
pressure, density, temperature, velocity, species mass fraction, volume
fraction, turbulence state, or any other solver-defined quantity. `FVMAMR` does
not know the meaning of these fields and does not prescribe their names.

### Solver-Field Error Metric

A solver-field error metric is a cell-centered scalar store that summarizes how
strongly one solver field suggests mesh change on a given cell. The metric is
owned by the solver. It may be built from reconstructed face jumps, gradient
magnitudes, limiter activity, interface indicators, residual estimates,
distance functions, or another solver-specific construction.

For the built-in scaled-error path, the module ultimately consumes scalar cell
stores. A solver may provide those scalar stores directly through `adaptSensor`,
or ask `FVMAMR` to build common scalar metrics from solver fields through
`adaptSensorMetrics`.

### Sensor Selection

`adaptSensor` is a vars-file list of solver-field error metric names. The
startup helper expands

```text
adaptSensor: Perror,Terror
```

into the boolean facts `adaptSensor(Perror)` and `adaptSensor(Terror)`. Those
facts instantiate the generic statistical normalization rules for the selected
metrics. A metric that is not selected does not contribute to
`totScaledError`.

`adaptSensorMetrics` is the structured form for module-generated metrics. Each
top-level option names a solver field, and the option value describes how the
module should turn that field into a scalar metric:

```text
adaptSensorMetrics: <p=[type=scalar,kernel=faceJump,weight=1.0],
                     v=[type=vector,kernel=faceJump,weight=1.0],
                     y=[type=mvector,kernel=faceJump,weight=0.5]>
```

The `faceJump` kernel is the first module-provided metric kernel. It builds a
cell metric from the maximum reconstructed jump across adjacent faces. The field
`type` selects the common FVM reconstruction facts used by the kernel:

- `scalar`: `leftsP(X,Zero)` and `rightsP(X,Zero)`
- `vector`: `leftv3d(X)` and `rightv3d(X)`
- `mvector`: `leftvM(X)` and `rightvM(X)`

The startup helper expands the example above into selector facts such as
`amrUseScalarFaceJump(p)`, `amrUseVectorFaceJump(v)`, and
`amrUseMVectorFaceJump(y)`. The module-generated scalar stores are then named
`amrScalarFaceJump(p)`, `amrVectorFaceJump(v)`, and `amrMVectorFaceJump(y)`.
Those generated stores are selected exactly like direct solver metrics by
creating `adaptSensor(...)` and `adaptSensorWeight(...)` facts.

### Statistical Normalization

The built-in path does not compare raw solver error metrics directly to the AMR
threshold. Instead, `FVMAMR` measures how unusual the local metric value is
relative to the mesh-wide distribution of the same metric. In code, the module
computes `cellMean(NAME)` and `cellSigmaSq(NAME)` for each selected metric, then
uses those statistics to form a dimensionless normalized value.

This is the key separation of responsibility:

- the solver defines what physical or numerical feature the metric measures
- `FVMAMR` defines how the selected metric is normalized and converted into AMR
  evidence

### Scaled-Error Source

`scaledError` is the built-in request source created from the statistical
normalization path. It is not itself a solver field and it is not the raw error
metric. It is the module-owned source that converts `totScaledError` into
`amrRequest(scaledError)`.

### Request Source

A request source is one objective that can ask for a mesh action. The built-in
`scaledError` source is one example. Other sources may represent geometry
protection, a volume-fraction interface, an immersed-boundary distance, an
overset constraint, or a named region. Each source writes the generic command
fact `amrRequest(SRC)` and may add optional policy facts such as
`amrRequestStrength(SRC)`.

### Request Arbitration

Request arbitration is the module's formal version of the "many objectives
enter, one mesh command leaves" model. For each cell, every active source may
request refinement, coarsening, or no change. The fusion policy compares those
requests using source metadata and emits one signed command for that cell.

The arbitration inputs are:

- required source command: `amrRequest(SRC)`
- required source priority: `amrSourcePriority(SRC)`
- optional source strength: `amrRequestStrength(SRC)`
- optional target level or cell size
- optional source category: `amrSourceKind(SRC)`
- optional hard/soft metadata: `amrSourceHardConstraint(SRC)`

The current scheduler-visible policy uses command, priority, and source id.
Strength, target level, target size, source category, and hard/soft
metadata are reserved for richer policies and diagnostics.

For regression tests and debugging, `amrConstantRequestSources` can create
solver-neutral sources directly from a case deck:

```text
amrConstantRequestSources: <lowRefine=[command=refine,id=10,priority=0],
                           highCoarsen=[command=coarsen,id=20,priority=10]>
```

These sources emit the same `amrRequest(SRC)` facts that solver rules would
emit. They are intended for focused arbitration tests, not as the normal way for
production solvers to define AMR objectives.

### Distance Request Sources

Distance-based refinement is represented as a request source whose evidence is
a cell-centered distance field. The distance may come from a named boundary, a
signed-distance function, an immersed body, a VOF interface, or another solver
construction. `FVMAMR` converts `amrDistanceToTarget(SRC)` into
`amrRequest(SRC)` using source-specific thresholds.

```cpp
Loci::createAMRDistanceRequestSourceFacts(facts,
  "cylinderDistance", 2, 20, "geometry", false,
  0.002, -1.0, -1.0, 0.006, "cylinder") ;
```

This registers `cylinderDistance` as an ordinary request source and enables the
distance adapter. With the thresholds above, cells closer than `0.002` request
refinement and cells farther than `0.006` request coarsening. Negative
thresholds are disabled, so the same helper also supports "refine if farther
than this distance" or one-sided refinement-only rules.

The solver then supplies the distance itself:

```cpp
$rule pointwise(amrDistanceToTarget(cylinderDistance)<-distToCylinder),
constraint(geom_cells) {
  $amrDistanceToTarget(cylinderDistance) = $distToCylinder ;
}
```

This keeps named-boundary mechanics out of the fusion policy. A solver or grid
reader may compute `distToCylinder` from a boundary-name lookup, a wall-distance
map, or a more accurate nearest-surface search; the module only consumes the
resulting distance and arbitrates the request with the other sources.

For mesh boundaries that are already named in the VOG file, a solver can ask the
module to build a nearest-boundary-face map directly. The boundary argument is
the mesh boundary name recorded in `boundary_names`; in normal cases it is the
same name used as the key in the `boundary_conditions` section.

```cpp
Loci::createAMRBoundaryDistanceSourceFacts(facts,
  "cylinderDistance", "cylinder", 2, 20, false,
  0.002, -1.0, -1.0, 0.006) ;
```

This helper uses the core mesh facts `boundary_names`, `boundary_faces`, `ref`,
`face2node`, `pos`, `upper`, `lower`, and `boundary_map` to find the nearest
face on the named boundary for each cell. It records the selected face set as
`amrBoundaryFaces(cylinderDistance)`. The `.loci` distance rule then maps
through `amrClosestBoundaryFace(cylinderDistance)` to compute
`amrDistanceToTarget(cylinderDistance)`. Solvers should call this helper after
grid and boundary facts have been created, and again after an adapted grid has
been injected into a fresh fact database.

Applications that call `Loci::setupFVMAMRFacts(facts)` after grid facts are
available may expose the same helper directly through the vars file:

```text
amrBoundaryDistanceSources:
  <cylinderDistance=[boundary=cylinder,
                     id=30,
                     priority=5,
                     refineLessThan=0.002,
                     coarsenGreaterThan=0.006]>
```

The top-level option name is the request source name. The `boundary` value names
the mesh boundary. The optional `id`, `priority`, `kind`, and `hard` entries set
source metadata, while `refineLessThan`, `refineGreaterThan`,
`coarsenLessThan`, and `coarsenGreaterThan` set the enabled distance
thresholds. Short aliases `refineLT`, `refineGT`, `coarsenLT`, and
`coarsenGT` are also accepted. `amrNamedBoundaryDistanceSources` remains an
accepted compatibility spelling for older test cases; setup still emits the
canonical `amrBoundaryDistanceSource(SRC)` selector.

The arbitration outputs are:

- the fused command: `amrFusedRequest`
- the winning command: `amrWinningCommand`
- the winning source priority: `amrWinningPriority`
- the winning source id, when available: `amrWinningSource`
- conflict diagnostics: `amrConflictCount`, `amrRefineRequestCount`, and
  `amrCoarsenRequestCount`

Fusion policies are expressed in terms of the same request-source vocabulary:
priority, strength, equal vote, source order, hard constraints, and
tie-breaking rules. Solver interfaces should be written in terms of sources,
requests, priorities, and strengths rather than direct writes to the final
marker.

### Final Marker

`cellRefFlag` is the final signed AMR marker before FVMAdapt tag encoding. It
should normally be reached through request fusion rather than by direct solver
rules. The final path is

```text
amrRequest(SRC)
  -> amrFusedRequest
  -> cellRefFlag
  -> cellRefFlagMask
  -> cellRefineTag
```

## Module Boundary

`FVMAMR` owns:

- common AMR vars-file controls
- generic sensor normalization and thresholding
- source-based AMR request facts
- request-fusion diagnostics
- edge-length protection masks
- conversion from signed requests to `FVMAdapt` refine tags
- named helpers for common AMR database entries
- generic transfer and restart rule classes

The solver owns:

- loading `fvmamr`
- deciding when an AMR-capable query is made
- defining solver-specific sensor or distance fields
- selecting which solver state fields must survive remeshing
- registering solver-specific transfer and restart variables
- calling `onlineRefineMesh()`
- injecting the adapted grid and rebuilding its driver state

`FVMAMR` should not contain default solver field lists such as pressure,
density, temperature, turbulence variables, or species mass fractions. Those
names belong to each solver.

## Startup Contract

A solver loads the module in the same way as other Loci modules:

```cpp
Loci::load_module("fvmamr", rdb) ;
```

After vars facts are loaded, the solver should expand selected sensor names,
request-source options, and configure the low-level `FVMAdapt` globals. If the
case uses grid-dependent setup such as `amrBoundaryDistanceSources`, call
`setupFVMAMRFacts()` after grid and boundary facts are present:

```cpp
Loci::setupFVMAMRFacts(facts) ;
int adaptMode = Loci::configureFVMAdaptGlobals(facts) ;
```

For example, a vars-file entry

```text
adaptSensor: Perror,Terror
```

creates boolean facts named `adaptSensor(Perror)` and `adaptSensor(Terror)`.
Those facts instantiate the generic sensor-normalization rules for the
solver-provided stores `Perror` and `Terror`.

`configureFVMAdaptGlobals()` reads `adaptMode`, `adaptMinEdgeLength`, and
`adaptFaceFold`, updates the low-level `FVMAdapt::Globals` values, and returns
the split-mode integer expected by `onlineRefineMesh()`. `adaptBalanceType` is
currently reserved compatibility metadata; this helper does not pass it into
`FVMAdapt`.

`refineLevel` is the AMR cycle id, not the per-cell refinement depth. Set or
increment it before the marking query that stages `do_adapt`, then carry the
same cycle id into the adapted-continuation query together with
`refineRestart`. Per-cell depth is tracked separately by `amrCellLevel`.

## Built-In Scaled-Error Source

The built-in source named `scaledError` is the default bridge from selected
solver-field metrics to the generic request-fusion path. It has three layers:

1. The solver selects one or more scalar metrics. These may be solver-owned
   stores such as `Perror`, or module-generated stores such as
   `amrScalarFaceJump(p)`.
2. `FVMAMR` computes mesh-wide statistics for each selected metric and converts
   the metric value on each cell into a dimensionless normalized value.
3. `FVMAMR` thresholds the combined normalized value to produce
   `amrRequest(scaledError)`.

For a selected sensor \f$E_k\f$ on cell \f$c\f$, the module computes

\f[
  \mu_k = \frac{1}{N}\sum_{c=1}^{N} E_k(c)
\f]

and the sample variance

\f[
  \sigma_k^2 =
  \frac{1}{N-1}\sum_{c=1}^{N}\left(E_k(c)-\mu_k\right)^2 .
\f]

The normalized sensor value is

\f[
  z_k(c) =
  w_k\frac{E_k(c)-\mu_k}{\sigma_k + \epsilon},
  \qquad \epsilon = 10^{-30}.
\f]

where \f$w_k\f$ is the selected metric weight. Direct `adaptSensor` entries use
\f$w_k=1\f$ unless an `adaptSensorWeight(NAME)` fact is supplied. Structured
`adaptSensorMetrics` entries use their `weight` option.

The scaled-error source combines selected weighted sensors using a maximum
reduction:

\f[
  \mathrm{totScaledError}(c) = \max_k z_k(c).
\f]

`totScaledError` is therefore not itself a gradient. It is the largest weighted,
normalized selected error metric on a cell. In a solver such as Stream, the
metric may be built from reconstructed face jumps, so it is often related to
gradients or sharp flow features. The request source only sees the final scalar
metric value and the statistics of that metric.

The threshold comparison is therefore made in normalized statistical units, not
in the original physical units of the solver field. For the built-in path,
`adaptSensitivity` means approximately "refine cells whose strongest selected
error metric is this many standard deviations above its mean." If
`maxAdaptCellCount` is active, `limitedAdaptSensitivity` may be raised so that
no more than the allowed number of cells request refinement.

The built-in source then converts this normalized metric into a signed request:

\f[
\mathrm{amrRequest}_{\mathrm{scaledError}}(c) =
\begin{cases}
  1,  & \mathrm{totScaledError}(c) >
        \mathrm{limitedAdaptSensitivity}, \\
 -1,  & \mathrm{totScaledError}(c) <
       -\mathrm{coarsenSensitivity}, \\
  0,  & \text{otherwise}.
\end{cases}
\f]

The signed convention is:

- `1`: request refinement
- `0`: request no mesh change
- `-1`: request coarsening

The coarsening condition uses the same maximum selected normalized error. This
means the scaled-error source requests coarsening only when every selected
metric is sufficiently below its mean. A single high selected metric is enough
to prevent this source from asking for coarsening.

## Request Sources

Solvers should add new AMR objectives by registering request sources instead of
writing directly to `cellRefFlag`. A source is one objective that may ask for
mesh change, such as a flow-error sensor, a volume-fraction interface, an
immersed-boundary distance, an overset protection rule, or a named region.

Each source uses generic AMR facts:

- `amrRequest(SRC)`: required signed command
- `amrRequestStrength(SRC)`: optional source strength; ignored by the stock
  priority policy
- `amrRequestTargetLevel(SRC)`: optional requested refinement level; ignored by
  the stock priority policy
- `amrRequestTargetSize(SRC)`: optional requested cell size; ignored by the
  stock priority policy
- `amrSourcePriority(SRC)`: required policy priority created during registration
- `amrSourceKind(SRC)`: optional source category created during registration
- `amrSourceHardConstraint(SRC)`: optional hard/soft metadata created during
  registration

The helper

```cpp
Loci::createAMRRequestSourceFacts(facts,
  "vofInterface", 1, 10, "solver-field", false) ;
```

creates the source registry facts for `vofInterface`. The solver then provides
ordinary Loci rules for that source:

```cpp
$rule pointwise(amrRequest(vofInterface)<-vofError),
constraint(geom_cells) {
  if($vofError > 1.0) {
    $amrRequest(vofInterface) = 1 ;
  } else {
    $amrRequest(vofInterface) = 0 ;
  }
}
```

## Request Fusion And Final Tags

Request fusion is the per-cell arbitration stage that converts all active
source requests into one module-owned command. This is the AMR control point
where disparate objectives are allowed to disagree without writing directly to
the final `FVMAdapt` marker.

The design goal is that solver modules should contribute requests, not final
tags. For example, a flow-error source may request refinement, a cell-count
policy may prefer no change, a coarsening source may request coarsening, and a
geometry source may carry hard-constraint metadata. Fusion is the shared place
where those requests are compared.

The fusion stage computes:

- `amrFusedRequest`
- `amrWinningPriority`
- `amrRefineRequestCount`
- `amrCoarsenRequestCount`
- `amrConflictCount`
- `amrWinningCommand`
- `amrWinningSource`

`amrFusedRequest` is joined into `cellRefFlag`, geometric protection masks are
applied to produce `cellRefFlagMask`, and the final signed marker is translated
to the `FVMAdapt` tag convention:

- `1`: refine
- `0`: keep
- `2`: de-refine

The scheduler-visible fusion policy is priority-first. Among active sources,
the source with the highest `amrSourcePriority(SRC)` wins. If multiple active
sources have the same winning priority, the lowest `amrSourceID(SRC)` wins as a
deterministic tie break. The winning source's `amrRequest(SRC)` then becomes
`amrFusedRequest`. The command itself does not decide the winner, so a
high-priority coarsening source may beat a lower-priority refinement source.

The rule path is:

```text
solver sensor or source rules
  -> amrRequest(SRC)
  -> amrFusedRequest
  -> cellRefFlag
  -> cellRefFlagMask
  -> cellRefineTag
  -> FVMAdapt database
```

Source metadata facts record priorities, hard/soft classification, strengths,
and target sizes so arbitration policies can be kept solver-neutral.

The C++ `AMRRequestArbiter` helper records vocabulary for future fusion
policies:

- priority policy: the highest-priority source wins, with strength as a
  secondary ranking.
- strength policy: the strongest request wins, with priority as a secondary
  ranking.
- equal-vote policy: active sources vote for refine or coarsen.
- source-order policy: source order decides, optionally rotated by adaptation
  cycle for round-robin behavior.
- hard-constraint handling: hard sources can be evaluated before soft requests.
- tie behavior: ties may prefer refinement, coarsening, or no change depending
  on the selected policy.

Those policy choices are design vocabulary for keeping final marker generation
centralized while letting solvers describe the reason, strength, and priority of
each requested mesh action.

The scheduler-visible `.loci` rules currently implement the priority policy
subset: highest active `amrSourcePriority(SRC)` wins, lowest
`amrSourceID(SRC)` breaks priority ties, and the winning source's command is
written to `amrFusedRequest`.

## Decision, Topology, And History Facts

The request-fusion layer produces a compact per-cell decision ledger. These
facts are small cell stores so they can be queried, visualized, and carried
through adapted continuation without requiring each solver to invent its own
AMR bookkeeping scheme.

The fusion diagnostics are:

- `amrFusedRequest`: signed command selected before final geometry masks.
- `amrWinningCommand`: signed command selected by fusion.
- `amrWinningPriority`: priority of the winning active source.
- `amrWinningSource`: source id associated with the fused command.
- `amrRefineRequestCount`: number of sources requesting refinement.
- `amrCoarsenRequestCount`: number of sources requesting coarsening.
- `amrConflictCount`: number of active requests opposing the fused command.

The requested-decision facts are written before `FVMAdapt` runs:

- `cellRefFlagMask`: signed command after final protection masks.
- `amrRequestedAction`: signed command staged for `FVMAdapt`.
- `amrRequestedWinningSource`: source id associated with the staged command.
- `amrRequestedAdaptStep`: adaptation step associated with the staged command.

The accepted-topology facts are written after `FVMAdapt` has produced the
adapted mesh and the dynamic `c2p` relation is available:

- `amrTopologyAction`: observed topological action for the current cell.
- `amrLineageMapped`: nonzero when the current cell appears in `c2p`.
- `amrLineageParentCount`: number of previous cells mapped to this current
  cell.
- `amrLineageChildCount`: number of current cells produced by this cell's
  previous parent.
- `amrAcceptedAction`: signed action accepted by the topology operation.
- `amrAcceptedWinningSource`: source id associated with the accepted action.
- `amrAcceptedAdaptStep`: adaptation step associated with the accepted action.
- `amrCellLevel`: per-cell refinement depth carried through accepted lineage.

The compatibility facts `amrLastAction`, `amrLastWinningSource`, and
`amrLastAdaptStep` mirror the requested decision on ordinary marking queries
and the accepted topology decision on adapted-continuation queries.

These facts define the handoff between the AMR decision logic and the
adapted-continuation logic. The decision logic records what was requested on
the parent mesh. The continuation logic uses the `FVMAdapt` parent-child map to
classify what happened on the adapted mesh:

- one previous parent mapped to several current cells is refinement.
- one previous parent mapped to one current cell is a direct map.
- several previous parents mapped to one current cell is coarsening.

Categorical history is transferred by lookup, not by numeric interpolation.
Before remeshing, `FVMAMR` stages the requested action, requested source,
requested adaptation step, and current `amrCellLevel` in file-order parent
stores. After remeshing, the lineage rule normalizes the parent ids in
`c2p`/`c2pglobal`, gathers the parent history stores, and writes accepted facts
on the adapted cells. Refined children inherit the parent level plus one,
directly mapped cells preserve the parent level, and coarsened cells use the
minimum parent level minus one, clamped at zero. A source id and step are copied
only when a parent request matches the actual accepted topology action; topology
changes without a matching parent request keep source id `-1`.

## Database And Field Handoff

`FVMAMR` uses typed enum helpers for common `FVMAdapt` `DataXFER_DB` keys such
as `AMR_DB_REFINE_TAG`, `AMR_DB_CURRENT_PLAN`, `AMR_DB_C2P`, and
`AMR_DB_CELL_LEVEL`. Those enum keys are reserved for the shared AMR
infrastructure contract.

Solver fields use string keys because each solver owns its restart names,
time-level naming, and field list. Choose keys that are unique within the
solver's AMR handoff. `replaceAMRDBItem()` deletes any existing value for the
same key before inserting the new object, and restart rules consume and delete
their keys after interpolation.

The transfer/restart base classes are generic:

- `Loci::AMRTransferScalar`, `Loci::AMRTransferVector`, and
  `Loci::AMRTransferStoreVec` stage parent-mesh stores before remeshing.
- `Loci::AMRRestartScalar`, `Loci::AMRRestartVector`, and
  `Loci::AMRRestartStoreVec` interpolate staged parent stores onto the adapted
  mesh.

The public macros only register small solver-specific rule classes. The solver
supplies every variable name, key, constraint, conditional, and output name, so
`FVMAMR` does not need to know pressure, density, species, turbulence, or
time-level naming conventions.

## Rendering This Page

From this directory, run:

```bash
make html
```

The generated page is written under `build/html/index.html`. Math rendering is
enabled through Doxygen's MathJax support.
