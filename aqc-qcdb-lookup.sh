#! /bin/bash

export INFOLOGGER_MODE=stdout
export SCRIPTDIR=$(readlink -f $(dirname $0))
#echo "SCRIPTDIR: ${SCRIPTDIR}"

RUNS_CONFIG="$1"
PLOTS_CONFIG="$2"

echo "root -b -q \"aqc_qcdb_lookup.C(\\\"${RUNS_CONFIG}\\\")\""
root -b -q "aqc_qcdb_lookup.C(\"${RUNS_CONFIG}\")" #>& "outputs/${ID}/log.txt"
