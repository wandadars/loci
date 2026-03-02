#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OBJ_BASE="${LOCI_OBJ_BASE:-${PROJECT_ROOT}/OBJ}"

if [[ ! -d "${OBJ_BASE}/System" ]]; then
  echo "ERROR: expected System build directory at ${OBJ_BASE}/System" >&2
  echo "Set LOCI_OBJ_BASE if your OBJ directory is elsewhere." >&2
  exit 1
fi

COV_ROOT="${SCRIPT_DIR}/coverage"
COV_SYSTEM_DIR="${COV_ROOT}/system_lib"
COV_TEST_DIR="${COV_ROOT}/test_build"
COV_GCOV_DIR="${COV_ROOT}/gcov"
GCOV_LOG="${COV_ROOT}/gcov.log"

rm -rf "${COV_ROOT}"
mkdir -p "${COV_SYSTEM_DIR}" "${COV_TEST_DIR}" "${COV_GCOV_DIR}"

# Reuse existing System objects for fast linking, but rebuild depend_graph2
# with coverage instrumentation in an isolated location.
ln -s "${OBJ_BASE}/System/depend_graph2.cc" "${COV_SYSTEM_DIR}/depend_graph2.cc"
ln -s "${OBJ_BASE}/System/Loci_version.cc" "${COV_SYSTEM_DIR}/Loci_version.cc"
ln -s "${OBJ_BASE}/System/dist_tools.h" "${COV_SYSTEM_DIR}/dist_tools.h"
ln -s "${OBJ_BASE}/System/loci_globs.h" "${COV_SYSTEM_DIR}/loci_globs.h"

shopt -s nullglob
for obj in "${OBJ_BASE}/System/"*_lo.o; do
  base="$(basename "${obj}")"
  if [[ "${base}" == "depend_graph2_lo.o" || "${base}" == "Loci_version_lo.o" ]]; then
    continue
  fi
  ln -s "${obj}" "${COV_SYSTEM_DIR}/${base}"
done
shopt -u nullglob

make -C "${COV_SYSTEM_DIR}" \
  -f "${PROJECT_ROOT}/src/System/Makefile" \
  LOCI_BASE="${OBJ_BASE}" \
  DEPEND_FILES= \
  INCLUDES="-I${OBJ_BASE}/System -I${OBJ_BASE}/sprng/include" \
  COPT="-Wall -O0 --coverage" \
  LIB_FLAGS="--coverage" \
  depend_graph2_lo.o libLoci.so

make -C "${COV_TEST_DIR}" \
  -f "${PROJECT_ROOT}/quickTest/System/Makefile" \
  LOCI_BASE="${OBJ_BASE}" \
  COPT="-Wall -O0 --coverage" \
  LDFLAGS="--coverage" \
  LIBS="-Wl,-rpath,${COV_SYSTEM_DIR} -L${COV_SYSTEM_DIR} -lLoci -L${OBJ_BASE}/lib -lTools \$(BASE_LIBS) -lm -lsprng" \
  LD_LIBRARY_PATH="${COV_SYSTEM_DIR}:${OBJ_BASE}/lib:${LD_LIBRARY_PATH:-}" \
  test_depend_graph2

pushd "${COV_SYSTEM_DIR}" >/dev/null
gcov -r -b -c -o "${COV_SYSTEM_DIR}" "${COV_SYSTEM_DIR}/depend_graph2_lo.o" | tee "${GCOV_LOG}" >/dev/null
if [[ -f "depend_graph2.cc.gcov" ]]; then
  mv "depend_graph2.cc.gcov" "${COV_GCOV_DIR}/depend_graph2.cc.gcov"
fi
popd >/dev/null

if command -v gcovr >/dev/null 2>&1; then
  gcovr \
    --gcov-executable gcov \
    --root "${PROJECT_ROOT}" \
    --object-directory "${COV_SYSTEM_DIR}" \
    --filter ".*/src/System/depend_graph2.cc$" \
    --txt "${COV_ROOT}/summary.txt" \
    --html-details "${COV_ROOT}/index.html" >/dev/null 2>&1 || true
fi

line_summary="$(awk '
  /File .*depend_graph2.cc/ {in_file=1; next}
  in_file && /Lines executed:/ {print; exit}
' "${GCOV_LOG}")"

echo "Coverage artifacts are in: ${COV_ROOT}"
if [[ -n "${line_summary}" ]]; then
  echo "${line_summary}"
fi
echo "Primary report: ${COV_GCOV_DIR}/depend_graph2.cc.gcov"

if [[ -f "${COV_ROOT}/summary.txt" ]]; then
  echo "Summary report: ${COV_ROOT}/summary.txt"
fi
if [[ -f "${COV_ROOT}/index.html" ]]; then
  echo "HTML report: ${COV_ROOT}/index.html"
fi
