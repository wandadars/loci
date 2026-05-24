# Scheduler Timing and Optimization Notes

This note records the current observations from the schedule-generation timing
investigation on the `scheduler_optimization` branch.  The measurements below
come from the n-dodecane/N2 case:

```
/home/wandadar/software/stream_cases/example_cases/2D_cases/ndodecane_n2_jet/chemkin_properties/no_double_flux/crank_nicolson
```

The validation runs used a disposable case directory with `case.vars` copied and
the large inputs symlinked back to the source case.  This avoided measuring a
slow copied-file path as scheduler cost.

## High-Level Observations

- Schedule generation is not dominated by the final graph scheduler.  The
  actual graph scheduling step is about `0.07 s` on the tested `np=15` run.
- The wall time reported as `time to create schedule` is the max over MPI ranks
  from `makeQuery()`.  Schedule generation is executed on every rank and uses
  MPI collectives in the distributed setup paths, but the graph construction and
  graph scheduling work itself is not visibly threaded.
- The expensive pieces are the pre-graph setup stages: stationary relation
  generation and distributed fact localization/cloning.
- The schedule generation path currently performs distributed clone/localization
  work twice in this case:
  - once inside `stationary_relation_gen()`, before the internal relation query;
  - once after stationary relation generation, before the final dependency graph.
- That second clone is not just redundant-looking bookkeeping.  A prototype that
  skipped it produced identical printed schedules but changed the downstream
  solve trace, so fact-state normalization/reordering matters.

## Current Timing Breakdown

Representative safe restored run, `mpirun -np 15`:

```
time to create schedule                         ~5.85 s
schedule setup: stationary relation generation  ~2.70 s
schedule setup: fact distribution setup         ~1.04 s
graph processing total                          ~1.19 s
existential_analysis                            ~0.40 s
create execution schedule                       ~0.033 s
```

Inside stationary relation generation:

```
dependency graph                                ~0.96 s
clone setup                                     ~1.11 s
internal query                                  ~0.56 s
relation collection                             ~0.04 s
relation restore                                ~0.006 s
empty constraint pruning                        ~0.009 s
rule removal                                    ~0.001 s
```

Inside the stationary relation internal query:

```
fact_db copy                                    ~0.015 s
internal schedule creation                      ~0.54 s
internal schedule execution                     ~0.003 s
result copy                                     ~0.0001 s
```

Inside final graph processing:

```
dependency graph                                ~0.52 s
variable type setup                             ~0.32 s
compiler compile                                ~0.20 s
graph scheduling                                ~0.07 s
graph schedule assembly                         ~0.02 s
```

## Source Locations

- `src/System/scheduler.cc`
  - `create_execution_schedule()` owns the top-level schedule setup, final
    dependency graph creation, graph processing, existential analysis, and final
    execution schedule creation.
  - `internalQuery()` is used by stationary relation generation to build and
    execute a reduced internal schedule for intermediate relations.
  - `create_internal_execution_schedule()` builds the internal schedule used by
    `internalQuery()`.
- `src/System/sched_comp.cc`
  - `stationary_relation_gen()` builds a dependency graph to discover stationary
    relations, clones/localizes facts for the internal query, runs that query,
    removes relation-generation rules, restores generated relation facts, and
    prunes rules dependent on empty constraints.
- `src/System/dist_tools.cc`
  - `get_clone()` computes clone regions, creates local/global maps and
    communication metadata, and reorders facts into local numbering.
- `src/System/fact_db.cc`
  - `fact_db::copy_all_from()` deep-copies distributed metadata, but this was
    not a meaningful cost in the measured case.

## Optimization Ideas

### 1. Split `get_clone()` Into Reusable Metadata and Reorder Steps

This is the highest-value target.  The combined clone/localization paths account
for roughly two seconds of the measured schedule time:

- stationary relation clone setup: about `1.1 s`;
- final fact distribution setup: about `1.0 s`.

A direct "reuse the stationary clone" prototype saved about one second, but it
changed the later solve trace even when the printed schedules were identical.
That suggests the final clone is normalizing the fact database in a way that the
execution phase depends on.

A safer design would be to refactor `get_clone()` so that clone-region and
communication metadata computation can be separated from the fact reordering
and freezing step.  Then test whether metadata can be reused while still
applying relation facts and fact reordering exactly as the current final clone
does.

Questions to answer before changing behavior:

- Which pieces of `get_clone()` depend only on partitions, maps, and the final
  rule database after stationary relation rules are removed?
- Which pieces depend on generated relation facts?
- Can the final reordering still run unchanged while reusing clone-region
  metadata?
- Can we compare fact domains, allocation domains, key domains, `l2g`, `g2lv`,
  `copy`, and `xmit` against the current path?

### 2. Avoid or Reduce the Stationary Relation Discovery Graph

`stationary_relation_gen()` spends about `0.96 s` building a dependency graph to
discover stationary map/constraint relations.  The final graph construction
later costs another `0.52 s`.

There may be a way to pre-index relation-generating rules or cheaply determine
whether stationary relation generation is needed before building a full graph.
This is more invasive than timing or clone refactoring because stationary
relation generation mutates both the fact database and the rule database before
the final dependency graph is built.

Useful direction:

- Build a cheap candidate set of map/constraint relation targets from the rule
  database.
- Only build the relation-generation dependency graph if one of those relation
  targets is reachable from the user query and not already a user-requested
  fact.
- Keep the current topological-order behavior for cases that actually need
  generated maps, since those can affect later existential analysis.

### 3. Investigate Internal Schedule Creation for Relation Queries

The internal query mostly spends time creating its own internal schedule
(`~0.54 s`).  The actual internal query execution is tiny in this case.

Possible improvements:

- Cache internal schedules for identical relation-query shapes, if repeated
  across queries or timesteps.
- Add a specialized path for simple stationary constraint relation generation,
  but only if it can preserve the existing rule semantics.
- Add finer timing inside `create_internal_execution_schedule()` before any
  optimization, because it likely repeats the same graph, variable type, compile,
  and existential-analysis pattern in miniature.

The measured `fact_db` copy inside `internalQuery()` is only about `0.015 s`, so
an in-place internal query does not look worth pursuing by itself.

### 4. Lower-Priority Graph Processing Work

Final graph processing is about `1.2 s`, with the largest subphases:

- final dependency graph: about `0.52 s`;
- variable type setup: about `0.32 s`;
- compiler compile: about `0.20 s`.

This is not the first place to optimize because the setup and clone paths are
larger.  Still, if the clone path becomes cheaper, these may become the next
visible costs.

Possible ideas:

- Avoid repeated variable type setup work where variable types are already known
  from the internal relation schedule.
- Look for repeated rule/type scans in `set_var_types()` and compiler setup.
- Keep graph scheduling itself low priority; it is already small.

### 5. Rule Database Transformation Costs

Top-level setup also spends a smaller but visible amount in rule database
transforms:

```
parametric rule expansion                         ~0.18 s
replace map constraints                           ~0.16 s
rename gpu containers                             ~0.14 s
```

These are not dominant in this case, but together they are close to half a
second.  They may be worth caching only after the larger clone and stationary
relation costs are addressed.

## Unsafe Prototype Result

Prototype tried:

- Let the no-map branch of `stationary_relation_gen()` return the localized
  clone to the outer scheduler.
- Skip the final `get_clone()` in `create_execution_schedule()`.

Result:

- Schedule generation dropped from about `5.8 s` to about `4.8 s`.
- Printed `debug/schedule-*` files were identical to the baseline path.
- The subsequent solve trace changed before the known `volFluxStar` floating
  point exception.

Conclusion:

- Do not merge that direct reuse approach.
- Identical schedule text is not enough validation here.  Fact state entering
  execution must also match.

## Suggested Validation For Future Optimization

Any future optimization should check more than the printed schedule:

- Compare `debug/schedule-*` against the baseline.
- Compare early runtime trace or residual lines against the baseline.
- Add a fact-state comparison around the transition from schedule generation to
  execution, especially for:
  - variable domains and allocation domains;
  - store representation/frozen state;
  - key-domain metadata;
  - `distribute_info` fields such as `l2g`, `l2f`, `g2lv`, `copy`, and `xmit`.
- Test both no-map stationary relation cases and cases where generated maps
  force the successive-query branch.

## Notes On Runtime Knobs

Do not base the optimization plan on runtime knobs such as disabling chomp, DMM,
or memory-greedy behavior.  Those knobs are not the intended user path for this
work.  The useful path is code-level timing, then a narrow, correctness-checked
refactor of the schedule setup internals.
