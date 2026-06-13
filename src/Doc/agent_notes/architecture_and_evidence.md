# Architecture And Evidence

This note orients agents inside the Loci framework repository and names the
evidence that should support different kinds of changes.

## Source Layers

- `.loci` files are editable rule sources. They may use `$rule ...` syntax or
  C++ classes derived from rule base classes such as `pointwise_rule`,
  `unit_rule`, `apply_rule`, and `singleton_rule`.
- `.lh` files are Loci-facing headers and declarations.
- C++ sources and headers provide framework, module, and helper
  implementations.
- Generated `*.lcc~` files, when present, are inspection artifacts. Do not edit
  them directly.
- `OBJ/` is a configured build tree. Treat it as generated state unless a task
  is specifically about build output.
- `src/Doc/` contains source-controlled developer notes, examples, and style
  guidance.
- `Tutorial/` contains source-controlled tutorial material.
- `Doxyfile` and `doxygen/` configure generated API documentation. Generated
  documentation belongs in ignored output directories or an install prefix, not
  in source.

## Repository Areas

- `src/System/`: core Loci framework implementation.
- `src/Tools/`: tool and utility sources.
- `src/lpp/`: Loci preprocessor implementation.
- finite-volume support modules under `src/FVM*/`: framework modules and
  support libraries.
- `src/include/`: installed headers and module-facing interfaces.
- `quickTest/`: lightweight framework and module tests.
- `Tutorial/`: tutorial source and companion examples.
- `Doxyfile` and `doxygen/`: Doxygen configuration and theme files.
- `src/Doc/`: developer notes, examples, style guidance, and agent notes.

## Evidence By Change Type

For C++ framework or module changes:

- build the affected module or the full tree
- run the nearest quick test when one exists
- inspect compiler diagnostics for generated or installed-header mismatches

For `.loci` rule changes:

- build the affected module
- use downstream schedule artifacts when the behavior depends on a query
- check that the intended providers and consumers appear in the schedule
- for C++-implemented rules, check constructor declarations, `compute(...)`,
  `calculate(...)`, and `register_rule<...>` registrations

For Doxygen or tutorial changes:

- run the relevant documentation target for the current branch layout
- inspect `doc/html/` for API reference output when Doxygen sources changed
- inspect `Tutorial/docs/tutorial.pdf` when tutorial sources changed

For documentation-only agent notes:

- keep files under `src/Doc/agent_notes/`
- update `src/Doc/agent_notes/README.md` when adding a new note
- check source, tutorial material, and cited references when refining high-level
  Loci terminology
- do not wire notes into Doxygen unless they are meant to become public API
  reference material

For reference material:

- use references to improve terminology, not to replace the checked-in tutorial
  or generated API reference
- avoid adding extracted text dumps to the repository unless they are explicitly
  curated

## Conceptual Layers To Keep Separate

When investigating or documenting behavior, keep these layers distinct:

1. Framework semantics: what Loci means by relations, facts, rules, maps,
   constraints, queries, and rule contexts.
2. Module policy: what a framework module chooses to expose.
3. Downstream solver behavior: what a solver built on Loci asks for and how its
   schedules assemble.
4. Generated code: preprocessor output used to inspect lowering, not editable
   source.
5. Build/install layout: configured `OBJ/` and install-prefix artifacts.

Mixing these layers makes reviews confusing and can make a framework note sound
like a solver contract.

Some modules intentionally cross layers at their boundary. When auditing such
modules, separate the rule interface from the helper algorithm before deciding
whether the code is generic framework behavior, module policy, or internal
implementation detail.

## Common Investigation Route

Before changing Loci code:

1. Search for the nearest analogous implementation.
2. Read the declarations and source around the rule or helper.
3. Identify the facts, maps, constraints, and derived rule contexts.
4. Check whether the behavior is framework-generic or module-specific.
5. Choose the smallest build or test that exercises the affected path.
6. If downstream schedule behavior matters, validate with schedule artifacts
   rather than build success alone.
