#!/usr/bin/env bash
#
# Configure and build the gammaConversion study.
#
# Runnable from anywhere: paths are resolved relative to this script, not to
# the working directory. Override the defaults with environment variables:
#
#   JOBS=4 ./build.sh                                  fewer parallel jobs
#   Geant4_DIR=/some/prefix/lib/cmake/Geant4 ./build.sh use another Geant4
#   CLEAN=1 ./build.sh                                 wipe the build tree first
#
set -euo pipefail

STUDY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${STUDY_DIR}/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/gammaConversion"

# 10 of the 12 cores by default, leaving the machine usable
JOBS="${JOBS:-10}"
: "${Geant4_DIR:=${REPO_ROOT}/install/geant4/lib/cmake/Geant4}"

if [[ ! -f "${Geant4_DIR}/Geant4Config.cmake" ]]; then
  echo "error: no Geant4 install at ${Geant4_DIR}" >&2
  echo "       build Geant4 first, or point Geant4_DIR at an existing prefix:" >&2
  echo "       Geant4_DIR=<prefix>/lib/cmake/Geant4 $0" >&2
  exit 1
fi

if [[ -n "${CLEAN:-}" ]]; then
  echo "==> removing ${BUILD_DIR}"
  rm -rf "${BUILD_DIR}"
fi

echo "==> configuring  (Geant4_DIR=${Geant4_DIR})"
cmake -S "${STUDY_DIR}" -B "${BUILD_DIR}" -DGeant4_DIR="${Geant4_DIR}"

echo "==> building with -j${JOBS}"
cmake --build "${BUILD_DIR}" -j"${JOBS}"

cat <<EOF

==> built ${BUILD_DIR}/gammaConversion

To run it:

  source ${REPO_ROOT}/install/geant4/bin/geant4.sh
  cd ${BUILD_DIR}
  ./gammaConversion config/default.cfg           # loops over the config material list
  ${STUDY_DIR}/run_materials.sh                   # one job per material in the script

Results land in ntuples/ and logs/ inside that directory.
EOF
