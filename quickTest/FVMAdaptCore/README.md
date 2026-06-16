# FVMAdapt Core Quick Test

This directory exercises the C++ helper library behind FVMAdapt without running
the full Loci rule database.

The first harness builds small single-cell fixtures for the core volume-cell
families:

- hexahedron through `HexCell`
- triangular prism through `Prism`
- tetrahedron and pyramid through the generic `Cell`/`DiamondCell` path

It applies each supported root split code for those paths, adds a few nested
breadth-first plan cases, checks the resulting leaf counts, checks plan
canonicalization, and can write legacy VTK files for ParaView inspection.

```bash
make -C quickTest/FVMAdaptCore LOCI_BASE=../../OBJ
make -C quickTest/FVMAdaptCore LOCI_BASE=../../OBJ examples
```

The `examples` target writes:

- `output/hex_split_0.vtk` through `output/hex_split_7.vtk`
- selected nested hex examples such as `output/hex_split_7_child0_z.vtk`
- `output/prism_split_0_wire.vtk` through `output/prism_split_3_wire.vtk`
- selected nested prism wireframes such as
  `output/prism_split_3_child0_z_wire.vtk`
- `output/general_tet_split_0_wire.vtk` and `output/general_tet_split_1_wire.vtk`
- selected nested general-cell wireframes such as
  `output/general_tet_split_1_child0_wire.vtk`
- `output/general_pyramid_split_0_wire.vtk` and
  `output/general_pyramid_split_1_wire.vtk`
- `output/core_split_summary.dat`
- `output/core_plan_audit.dat`
- `output/core_plan_replay.dat`

`core_split_summary.dat` is the compact test matrix. `core_plan_audit.dat` is
the more explanatory output: it prints input and canonical cell plans, derived
hex/prism face plans, extracted edge plans for hex faces, leaf counts, and `c1`
owner-cell mappings for fine faces.

`core_plan_replay.dat` is the most direct plan introspection file. It traces
each cell plan as a breadth-first tree replay, showing the node id, parent id,
child slot, path, plan-vector index, whether the code was explicit or defaulted
after the vector ended, local fold, child count, leaf id, and a short meaning
for the split code.

These files are generated artifacts and are removed by `make clean`.
