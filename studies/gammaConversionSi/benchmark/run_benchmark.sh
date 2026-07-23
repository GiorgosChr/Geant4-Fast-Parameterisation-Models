#!/usr/bin/env bash
#
# Time the two simulation modes over many experiments.
#
# For each mode (full, fast) this runs the executable EXPERIMENTS times, each a
# fresh process of nEvents photons, and records the per-experiment event-loop
# time (the BENCHMARK line the run prints) into benchmark/results/timings.csv.
# Summarise it afterwards with summarise.py.
#
# Runnable from anywhere. Environment overrides:
#   EXPERIMENTS=5 ./run_benchmark.sh          fewer experiments (default 100)
#   EVENTS=20000  ./run_benchmark.sh          fewer events per experiment (smoke)
#   MODES="fast"  ./run_benchmark.sh          only one mode
#   Geant4_DIR=<prefix>/lib/cmake/Geant4 ...  another Geant4 install to source
#
set -euo pipefail

BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STUDY_DIR="$(cd "${BENCH_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${STUDY_DIR}/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/gammaConversionSi"
RESULTS_DIR="${BUILD_DIR}/benchmark/results"

EXPERIMENTS="${EXPERIMENTS:-100}"
EVENTS="${EVENTS:-}"
MODES="${MODES:-full fast}"

EXE="${BUILD_DIR}/gammaConversionSi"
if [[ ! -x "${EXE}" ]]; then
  echo "error: ${EXE} not found; build the study first (./build.sh)" >&2
  exit 1
fi

# Source Geant4 so its data and libraries resolve (geant4.sh self-locates).
: "${Geant4_DIR:=${REPO_ROOT}/install/geant4/lib/cmake/Geant4}"
GEANT4_SH="$(cd "${Geant4_DIR}/../../.." && pwd)/bin/geant4.sh"
# shellcheck disable=SC1090
[[ -f "${GEANT4_SH}" ]] && source "${GEANT4_SH}"

cd "${BUILD_DIR}"
mkdir -p "${RESULTS_DIR}"
CSV="${RESULTS_DIR}/timings.csv"
echo "mode,experiment,events,realElapsed,userCPU,sysCPU" > "${CSV}"

# Pull "key=value" out of the BENCHMARK line.
field() { awk -v k="$2" '{for(i=1;i<=NF;i++){split($i,a,"=");if(a[1]==k)print a[2]}}' <<< "$1"; }

for mode in ${MODES}; do
  cfg="benchmark/${mode}.cfg"
  if [[ ! -f "${cfg}" ]]; then
    echo "error: ${BUILD_DIR}/${cfg} missing; re-run cmake to copy the benchmark configs" >&2
    exit 1
  fi
  # Honour an EVENTS override by writing a patched copy of the config.
  runcfg="${cfg}"
  if [[ -n "${EVENTS}" ]]; then
    runcfg="${RESULTS_DIR}/${mode}.cfg"
    sed "s/^nEvents.*/nEvents = ${EVENTS}/" "${cfg}" > "${runcfg}"
  fi

  echo "==> ${mode}: ${EXPERIMENTS} experiments"
  for (( exp=0; exp<EXPERIMENTS; exp++ )); do
    line="$("${EXE}" "${runcfg}" | grep '^BENCHMARK' | tail -1 || true)"
    if [[ -z "${line}" ]]; then
      echo "  [exp ${exp}] no BENCHMARK line -- run failed; see logs/${mode}*.log" >&2
      exit 1
    fi
    events="$(field "${line}" events)"
    real="$(field "${line}" realElapsed)"
    user="$(field "${line}" userCPU)"
    sys="$(field "${line}" sysCPU)"
    echo "${mode},${exp},${events},${real},${user},${sys}" >> "${CSV}"
    printf '  [exp %3d] real=%ss user=%ss\n' "${exp}" "${real}" "${user}"
  done
done

echo "==> wrote ${CSV}"
echo "    summarise with: python3 ${BENCH_DIR}/summarise.py ${CSV}"
