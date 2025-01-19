#! /bin/bash

export INFOLOGGER_MODE=stdout
export SCRIPTDIR=$(readlink -f $(dirname $0))
#echo "SCRIPTDIR: ${SCRIPTDIR}"

RUNS_CONFIG="$1"
PLOTS_CONFIG="$2"

YEAR=$(jq ".year" "${RUNS_CONFIG}" | tr -d "\"")
PERIOD=$(jq ".period" "${RUNS_CONFIG}" | tr -d "\"")
PASS=$(jq ".pass" "${RUNS_CONFIG}" | tr -d "\"")

ID=$(jq ".id" "${PLOTS_CONFIG}" | tr -d "\"")

mkdir -p "outputs/${ID}/${YEAR}/${PERIOD}/${PASS}"

echo "root -b -q \"aqc_process.C(\\\"${RUNS_CONFIG}\\\", \\\"${PLOTS_CONFIG}\\\")\""
root -b -q "aqc_process.C(\"${RUNS_CONFIG}\", \"${PLOTS_CONFIG}\")" #>& "outputs/${ID}/log.txt"

cat "outputs/${ID}/log.txt" | grep "Bad time interval"
