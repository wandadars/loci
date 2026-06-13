# Loci Cheat Sheet

This is a compact reference for reasoning about Loci programs without dropping
immediately to generated C++.

## Core Mental Model

- Loci is query-driven. A program does not execute source files from top to
  bottom; a query asks for facts, and Loci assembles the rules needed to produce
  those facts.
- The foundational papers describe Loci as a tuple-based relational data model:
  arrays are treated as relations, and computations are transformation rules.
- Current source often uses the word "fact" for a named relation or container in
  the fact database. In these notes, "relation" is the formal data-model term and
  "fact" is the practical code/schedule term.
- A rule signature documents inputs, outputs, and indirect accesses. Loci uses
  that signature to derive the rule context: the set of entity indices for which
  the computation can run.
- Constraints are valueless property relations over subsets of entities. They
  help decide where a rule is legal.
- Missing rules are usually dataflow or context problems: a needed fact, map,
  constraint, or query path was not available over the expected entities.

When reading or designing a change, ask:

1. What query pulls this fact into the schedule?
2. What relation or container does the output populate?
3. Which maps or relations are followed to read each input?
4. What entity set becomes the rule context?
5. Which constraints must be active?
6. Is this a direct assignment, a reduction, a setup constraint, or an opaque
   runtime object?

## Entities, Sets, Context, And Sequences

- An `entity` is an abstract item identified by an entity id or index.
- `entitySet` is the set-theory view of entities.
- `sequence` is the ordered execution view used for rule application.
- The rule context is the entity set inferred from the output relation, input
  relation availability, map compositions, and constraints.
- "Computation domain" is useful when discussing the derived loop bounds for a
  rule. Prefer "entity set" when discussing concrete sets in source code.
- `EMPTY` is the empty set.
- `~EMPTY` commonly means the full active entity set for a constraint.
- Inside a `.loci` rule body, `_e_` is the current entity id for that rule
  invocation. Use it only for temporary debugging, not permanent policy.

## Relations And Facts

The primary Loci data abstraction is a binary relation. The first tuple entry is
an entity id. The second entry is either a value or another entity id.

Foundational relation types:

- store: relates entity ids to values
- parameter: relates a group of entity ids to a shared value
- index map: relates entity ids to other entity ids
- constraint: marks a subset of entity ids with a valueless property

The initial stored relations are analogous to an extensional database. Rules
derive new relations, analogous to an intensional database. In current code and
debug output, these named relations are often discussed as facts.

## Common Containers

| Container | Meaning | Typical use |
|---|---|---|
| `store<T>` | `entity -> T` | Per-entity scalar, vector, tensor, or struct data |
| `storeVec<T>` | `entity -> vector<T>` | Per-entity variable-length state |
| `param<T>` | shared value over an entity set | Options, constants, global controls |
| `Map` | `entity -> entity` | One-to-one topology or references |
| `multiMap` | `entity -> [entities...]` | One-to-many topology or stencils |
| `Constraint` / `constraint` | set of entities | Rule activation over subsets |
| `blackbox<T>` | opaque typed object | Runtime databases or non-elementwise state |

`storeVec` and `blackbox` often require more care than ordinary fields because
their sizing, ownership, or internal state may not be obvious from the rule
signature alone.

## Arrow Syntax And Map Composition

The rule signature describes the output, inputs, context, and map traversal.

| Pattern | Read it as |
|---|---|
| `X` | use fact `X` on the current entity |
| `m->X` | follow map `m`, then read `X` |
| `(cl,cr)->X` | read `X` on both mapped entities |
| `face2node->pos` | gather `pos` over entities reached by a multi-map |
| `pmap->cl->X` | follow chained maps from left to right |

The `->` operator is map composition. It is the Loci notation for indirect
accesses such as "follow this map, then read that relation." The arrows are part
of the data dependency, not just syntax decoration. A rule can be syntactically
valid and still vanish if mapped inputs, output context, and constraints do not
intersect for the active query.

## Rule Types

| Rule type | Conceptual role |
|---|---|
| `pointwise` | Direct elementwise computation |
| `unit` | Neutral element for a reduction target |
| `apply [Op]` | Reduction contribution |
| `singleton` | Shared single-value computation |
| `constraint` | Compute an activation set |
| `blackbox` | Compute opaque or non-elementwise state |
| `default` | Provide fallback input values before overrides |

The core Loci references describe five main computational rule categories:
`pointwise`, `singleton`, `unit`, `apply`, and `blackbox`. Current Loci source
and applications also use setup/default/constraint-style forms, so read nearby
examples before assuming the table above is exhaustive.

## C++ Rule Classes In `.loci` Files

Some older or helper-heavy modules implement rules as C++ classes inside
`.loci` files rather than with `$rule ...` blocks. Read the constructor as the
rule signature:

```cpp
class some_rule : public pointwise_rule {
  const_store<T> input_value;
  store<U> output_value;

public:
  some_rule() {
    name_store("inputFact", input_value);
    name_store("outputFact", output_value);
    input("inputFact");
    output("outputFact");
    constraint("activeCells");
  }
};
```

In this style:

- `name_store("factName", member)` binds a C++ member to a named Loci fact.
- `const_store`, `const_param`, `const_multiMap`, and similar `const_...`
  members are read-side dependencies.
- `store`, `param`, `Map`, `multiMap`, and similar non-const members are usually
  outputs or mutable accumulators.
- `input(...)`, `output(...)`, and `constraint(...)` are the scheduler-visible
  rule interface. Treat the strings in those calls as the authoritative Loci
  signature.
- `compute(const sequence &seq)` is the scheduled rule body over the derived
  rule context.
- Many pointwise classes call `do_loop(seq, this)` and place per-entity work in
  `calculate(Entity e)`.
- `unit_rule` and `apply_rule` classes express the same reduction pairing as
  `unit` and `apply` rules.
- `disable_threading()` is a warning sign for side effects, file or database IO,
  global framework state, or ordering-sensitive work. Read those rules more
  like setup or boundary code than ordinary elementwise kernels.
- `register_rule<RuleClass> register_name;` makes the class available to the
  rule database.

The same dependency questions still apply: what fact is produced, what inputs
and maps are declared, what constraints gate the rule, and what query path needs
the output?

## Reductions

Use `unit` and `apply` when many contributors assemble one output.

- `unit` defines where the accumulator lives and what value it has when no
  contribution arrives.
- `apply` defines which contributors run and how they join into the target.
- The accumulator entity set is often different from the contributor entity set.
- Pair each reduction `apply` with a matching `unit` unless nearby code shows a
  deliberate exception.

Common identities:

| Operator | Usual identity |
|---|---|
| `Loci::Summation` | `0` |
| `Loci::Product` | `1` |
| `Loci::Minimum` | positive infinity or a large upper bound |
| `Loci::Maximum` | negative infinity or a large lower bound |

## Constraints And Selectors

Constraints are first-class subset relations. A constrained rule exists only on
the intersection of its output context, mapped input availability, and listed
constraints.

Keep these forms distinct:

```cpp
$rule constraint(C <- inputs) { ... }
```

computes the set `C`, while

```cpp
$rule pointwise(A <- inputs), constraint(C), { ... }
```

uses `C` to gate the rule.

Selectors are a common control-flow idiom. A setup rule registers allowed
choices, then downstream rules are guarded by constraints that represent the
active choice. Prefer explicit mode constraints and early validation over silent
fallbacks.

## Generated Code

Generated `*.lcc~` files are useful evidence when diagnosing how the Loci
preprocessor lowered a rule. They are not the source of truth for edits. Change
the `.loci`, `.lh`, C++, or header source that produced them.
