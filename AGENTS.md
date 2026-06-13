# Loci Agent Guide

This file is the short operating guide for coding agents working in the Loci
framework repository. Keep durable explanations under `src/Doc/agent_notes/`
rather than growing this file.

## Start Here

- `src/Doc/loci_style_guide.md`: local C++ and Loci style rules.
- `src/Doc/agent_notes/README.md`: longer Loci guidance for agents.
- `src/Doc/agent_notes/loci_cheatsheet.md`: generic Loci concepts and
  syntax reminders.
- `src/Doc/agent_notes/working_with_loci.md`: edit workflow, patterns,
  and common traps.
- `src/Doc/agent_notes/architecture_and_evidence.md`: repository layers
  and validation evidence.

## Working Stance

- Read nearby source before editing. Prefer established local patterns over new
  abstractions.
- Use `rg` and `rg --files` for searches.
- Edit `.loci` sources and checked-in C++/header files, not generated
  `*.lcc~` files.
- Inspect generated files only to understand how Loci preprocessing lowered a
  rule.
- Keep edits scoped to the active module or documentation topic.
- Preserve the distinction between framework behavior, module policy, generated
  code, and downstream solver behavior.

## Loci Changes

For nontrivial `.loci` or Loci-interface changes:

1. Find the closest existing rule, helper, module, or test.
2. Identify the facts, relations, constraints, maps, and query path involved.
3. State where each quantity lives: cell, face, node, boundary, parameter, or
   another entity set or module-specific location.
4. Make the smallest idiomatic change.
5. Run the smallest relevant build, test, documentation, or downstream schedule
   check that can prove the change.

If schedule artifacts are available from a downstream application, use them to
verify that the intended rules and facts are actually assembled. A successful
build alone does not prove a Loci rule is active for a particular query.
