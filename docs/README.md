# Loci Documentation Tooling

This directory is the top-level home for generated documentation tooling.

- `doxygen/` contains the C++ API documentation configuration.
- `.lh` and `.loci` sources are intentionally excluded from Doxygen because
  they are documented by the Loci documentation parser.

From the repository root:

```bash
make api-docs
```

This generates the experimental C++ API reference under
`OBJ/docs/api-cpp/html/`.
