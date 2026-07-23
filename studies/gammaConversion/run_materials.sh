#!/usr/bin/env bash
#
# Run the gammaConversion study once per material, looping over a list defined
# here in the script. Each material is a separate process, so the runs are
# independent and can be parallelised; each writes its own ntuple and log,
# named from the material (e.g. ntuples/Pb_2MeV-100GeV_10000000.root).
#
# The material list is passed to the executable as a "materials=<name>"
# override of config/default.cfg, so every other setting comes from that file.
# Edit MATERIALS below to change the scan.
#
set -euo pipefail

STUDY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${STUDY_DIR}/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/gammaConversion"

# The materials to scan -- single-element NIST names, so Z is unambiguous.
MATERIALS=(G4_Si G4_Ge G4_Fe G4_W G4_Pb)

if [[ ! -x "${BUILD_DIR}/gammaConversion" ]]; then
  echo "error: ${BUILD_DIR}/gammaConversion not built; run ${STUDY_DIR}/build.sh first" >&2
  exit 1
fi

source "${REPO_ROOT}/install/geant4/bin/geant4.sh"
cd "${BUILD_DIR}"

for material in "${MATERIALS[@]}"; do
  echo "==> ${material}"
  ./gammaConversion config/default.cfg "materials=${material}"
done

echo "==> done; ntuples in ${BUILD_DIR}/ntuples, logs in ${BUILD_DIR}/logs"
