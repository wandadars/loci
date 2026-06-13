# Agent Notes For Loci

This directory holds source-only guidance for coding agents and maintainers
working on the Loci framework. It is intentionally separate from the tutorial
sources and Doxygen configuration.

## Files

- `loci_cheatsheet.md`: generic Loci concepts, rule syntax, containers,
  constraints, reductions, maps, queries, and schedule-reading terms.
- `working_with_loci.md`: practical workflow for edits, useful implementation
  shapes, and common mistakes.
- `architecture_and_evidence.md`: repository orientation and validation
  evidence for framework, module, documentation, and downstream solver work.

## Transfer Notes

These notes were distilled from the Loci-Stream `documentation/loci/` reference
set, but only the Loci-generic material belongs here.

Transferred:

- the query-driven mental model
- relations/facts, entity sets, rule contexts, containers, maps, constraints, and
  rule forms
- `unit`/`apply` reduction reasoning
- schedule-proof and generated-code cautions
- existing-analogue-first edit workflow
- common Loci antipatterns such as hidden dataflow and build-only validation

Not transferred:

- Stream-specific fact names, solver stages, and pressure-correction vocabulary
- Stream case-running commands and case database paths
- Stream theory-guide and user-guide maintenance rules
- solver-specific option names, boundary-condition names, and runtime chains

## Organization Rule

- Put short, always-loaded operating instructions in the repository root
  `AGENTS.md`.
- Put stable Loci concepts in `loci_cheatsheet.md`.
- Put edit workflow, patterns, and antipatterns in `working_with_loci.md`.
- Put source-tree orientation and validation expectations in
  `architecture_and_evidence.md`.
- Ground high-level terminology in checked-in source, tutorial material, and
  cited references rather than solver-specific notes.
- Put module-specific guidance near the module when the guidance is not generic
  to Loci.

Avoid turning these notes into a second tutorial or API reference. The goal is
to preserve working context that helps future edits start from the right model
of the code.
