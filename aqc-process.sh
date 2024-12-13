#! /bin/bash

export INFOLOGGER_MODE=stdout
export SCRIPTDIR=$(readlink -f $(dirname $0))
#echo "SCRIPTDIR: ${SCRIPTDIR}"

CONFIG="$1"

ID=$(jq ".id" "$CONFIG" | tr -d "\"")

mkdir -p "outputs/${ID}"

echo "root -b -q \"aqc_process.C(\\\"inputs/${ID}/qclist.txt\\\", \\\"$CONFIG\\\")\""
root -b -q "aqc_process.C(\"inputs/${ID}/qclist.txt\", \"$CONFIG\")" >& "outputs/${ID}/log.txt"

cat "outputs/${ID}/log.txt" | grep "Bad time interval"
