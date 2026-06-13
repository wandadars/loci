# Working With Loci

This note collects edit workflow, useful implementation shapes, and recurring
mistakes for Loci framework work.

## Existing Analogue First

Use this for every nontrivial Loci edit:

1. Find the closest existing rule, module, helper, or test.
2. Identify the relation/fact being provided and the container that stores it.
3. Identify the inputs, maps, constraints, time levels, and derived rule
   context.
4. Identify what query or downstream consumer needs the result.
5. Make the smallest change that exposes the intended dependency graph.
6. Build, test, or inspect schedule artifacts enough to prove the change.

If new code looks much more imperative, templated, or manually scheduled than
nearby working code, pause and re-express the dependency in Loci terms.

## Reading C++-Implemented Rules

Some `.loci` files are mostly C++ rule classes. In those files, start with the
class constructor before reading the algorithm body:

1. Read each `name_store(...)` call to map C++ member names back to Loci fact
   names.
2. Read `input(...)`, `output(...)`, and `constraint(...)` as the rule
   signature.
3. Read member types to identify storage: `const_store`, `const_param`,
   `const_MapVec`, `const_multiMap`, `store`, `param`, and so on.
4. Read `compute(const sequence &seq)` to see whether the rule loops over the
   scheduled context, performs setup work, or reaches into framework state.
5. If `compute` calls `do_loop(seq, this)`, read `calculate(Entity e)` as the
   per-entity kernel.
6. Find the `register_rule<...>` line to identify the rule name that the module
   registers.

Treat `disable_threading()`, direct `fact_db` access, file IO, MPI/distribution
helpers, and module databases as signs that the rule is setup, IO, remeshing, or
boundary plumbing. Those rules can still be legitimate Loci code, but schedule
strings alone may not explain all side effects. List the facts they create,
read, or stage before changing them.

## Good Patterns

- Prefer small rules and kernels with explicit inputs and outputs.
- Give meaningful quantities scheduler-visible names when they have a real
  entity set, time level, or downstream consumer.
- Use a named fact and provider rule instead of hiding important dataflow in a
  C++ helper.
- Let the rule signature document map composition and indirect accesses instead
  of burying them in helper code.
- Keep helper functions small and mechanical when they are genuinely shared
  algorithms, type adapters, or opaque runtime objects.
- Add explicit constraints for mode-driven behavior.
- Validate option strings and unsupported combinations early.
- Pair reduction `apply(...)` rules with matching `unit(...)` rules.
- Keep field location, sign convention, units, and conservation meaning
  explicit when physics or numerics are involved.
- In helper-heavy modules, keep a distinction between scheduler-visible facts
  and implementation objects. A C++ helper tree is reasonable when it implements
  an algorithm behind a rule whose inputs and outputs are still declared in the
  rule signature.

## Named Quantity As A Fact

Create a named Loci fact when a value has meaningful identity:

- it is reused by more than one rule
- it has cell, face, boundary, node, parameter, or time-level variants
- it participates in constraints, reductions, mappings, or queries
- it is useful to search for in schedule output

In the reference papers this named item is usually a relation. In the current
framework source it often appears as a fact or container. Use whichever term
matches the code being discussed, but keep the same mental model: Loci rules
derive named relations/facts from existing ones.

Do not move a meaningful Loci quantity into a complex header-only helper merely
because the dependency path is hard to trace.

## Reductions

For reduction-style assembly:

- define the accumulator entity set with `unit`
- choose the correct neutral value
- keep contributor constraints explicit
- use `join(...)` paths that match the target entities
- check both `unit` and `apply` paths when validating schedule output

Wrong identities and mismatched `unit`/`apply` constraints are common sources of
empty or partially assembled results.

## Entity Variants

Treat each location as a separate fact family unless the code proves otherwise:

- cell
- face
- boundary face
- node
- reference entity
- parameter
- particle or module-specific entity
- time or iteration level

A cell fact existing does not imply a face, boundary, nodal, or time-shifted
provider exists.

Also treat priority and iteration labels as part of the fact identity. Names
such as `priority::refmesh::balancedCellPlan`, `tmpCellPlan{n}`, and
`tmpCellPlan{n+1}` are distinct facts even when the underlying C++ member names
look similar.

## Schedule Proof

For rule-family changes, validation is not just "the code compiled." If
schedule artifacts are available, confirm that the intended rule path was
assembled:

```bash
rg -n "rule_name|produced_fact|consumed_fact" debug/schedule
rg -n "eliminating .* due to|cleanout" debug/debug
```

Downstream applications may split these files by rank, for example
`schedule-0`, `schedule-1`, `debug.0`, or `debug.1`. Start with one rank unless
the behavior is specifically parallel.

## Antipatterns

### Hidden Header Dataflow

Symptom: a large C++ helper computes a meaningful Loci quantity while the rule
body becomes a thin wrapper.

Prefer a named fact and provider rule when the scheduler should see the
dependency.

Exception: modules such as mesh adaptation may use C++ helper objects to encode
topology trees, refinement plans, ordering, or remeshing algorithms. That is not
automatically hidden dataflow if the rule declares the scheduler-visible inputs,
outputs, maps, and constraints before invoking the helper. It becomes hidden
dataflow when the helper reads undeclared facts, global module state, or
framework state that changes the result without appearing in the rule interface.

### Manual Scheduling

Symptom: code tries to force execution order with mutable state, side effects,
or custom orchestration.

Prefer exposing the missing dependency as a fact, relation, constraint, or rule.

### Patching The Consumer

Symptom: a rule is changed to work around a missing input.

Prefer tracing backward to the provider rule, then to its inputs, maps,
constraints, and query path.

### Wrong Fact Location

Symptom: a value from one entity location is used where another location is
required.

Prefer naming the intended location and adding or selecting the correct
provider.

### Loose Domain Language

Symptom: "domain" is used to mean entity set, relation, fact location, module
scope, and loop bounds all at once.

Prefer "entity set" for concrete sets of entities, "relation" or "fact" for the
named container, and "computation domain" or "rule context" for the set of
entity indices Loci deduces from a rule signature.

### Build-Only Validation

Symptom: a Loci change is called done because the module builds.

Prefer schedule evidence for query-specific behavior whenever a downstream
schedule can be produced.

### Silent Fallback

Symptom: an unknown mode or option quietly selects a default.

Prefer explicit validation and a clear error for unsupported values.

### Removing Apparent Duplicates Too Early

Symptom: rule blocks are removed because they look redundant.

Prefer proving the shared provider is active for the relevant constraints,
modes, modules, and schedules before removing specialized paths.
