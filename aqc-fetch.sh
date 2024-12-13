#! /bin/bash

export INFOLOGGER_MODE=stdout
export SCRIPTDIR=$(readlink -f $(dirname $0))
#echo "SCRIPTDIR: ${SCRIPTDIR}"

if [[ -z $(which jq) ]]; then
       echo "The jq command is missing, exiting."
       exit 1
fi


if [[ -z $(which alien.py) ]]; then
       echo "The alien.py command is missing, exiting."
       exit 1
fi
  

CONFIG="$1"

ID=$(jq ".id" "$CONFIG" | tr -d "\"")
YEAR=$(jq ".year" "$CONFIG" | tr -d "\"")
PERIOD=$(jq ".period" "$CONFIG" | tr -d "\"")
PASS=$(jq ".pass" "$CONFIG" | tr -d "\"")
RUNLIST=$(jq ".runs[]" "$CONFIG" | tr -d "\"")

REFRUNLIST=$(jq ".referenceRuns[].number" "$CONFIG" | tr -d "\"")

rm -f "inputs/${ID}/qclist.txt"

OUTDIR="inputs/${ID}/${YEAR}/${PERIOD}/${PASS}"
mkdir -p "${OUTDIR}"

#echo "$REFRUNLIST $RUNLIST" | tr " " "\n" | sort | uniq
FULLRUNLIST=$(echo "$REFRUNLIST $RUNLIST" | tr " " "\n" | sort | uniq)
echo "$FULLRUNLIST"
#exit;

for RUN in $FULLRUNLIST
do

    echo "RUN: $RUN"

    ROOTFILES=""
          
    BASEDIR="/alice/data/${YEAR}/${PERIOD}/${RUN}/${PASS}"
    
    echo "BASEDIR: $BASEDIR"
    
    CHUNKS=$(alien.py ls $BASEDIR)
  
    INDEX=0
    while IFS= read -r CHUNK
    do
        
        #echo "$line"

        alien.py cp ${BASEDIR}/${CHUNK}/QC/QC.root file://./${OUTDIR}/QC-${RUN}-$(printf "%03d" ${INDEX}).root
        echo "${OUTDIR}/QC-${RUN}-$(printf "%03d" ${INDEX}).root" >> "inputs/${ID}/qclist.txt"
        if [ -e "${OUTDIR}/QC-${RUN}-$(printf "%03d" ${INDEX}).root" ]; then
            ROOTFILES="$ROOTFILES ${OUTDIR}/QC-${RUN}-$(printf "%03d" ${INDEX}).root"
        fi
        
        INDEX=$((INDEX+1))
        
    done < <(printf '%s\n' "$CHUNKS")
    
done
