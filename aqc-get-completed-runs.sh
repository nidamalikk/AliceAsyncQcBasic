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

UPDATE_CONFIG=0
if [ x"$1" = "x-u" ]; then
    UPDATE_CONFIG=1
    shift
fi

CONFIG="$1"

TYPE=$(jq ".type" "$CONFIG" | tr -d "\"")
YEAR=$(jq ".year" "$CONFIG" | tr -d "\"")
PERIOD=$(jq ".period" "$CONFIG" | tr -d "\"")
PASS=$(jq ".pass" "$CONFIG" | tr -d "\"")

RUNLIST=
RUNLISTJSON=

if [ x"$TYPE" = "xsim" ]; then

    BASEDIR="/alice/${TYPE}/${YEAR}/${PERIOD}"
    echo "--------------------"
    echo "Getting completed runs from $BASEDIR/${PASS}"
    echo "--------------------"

    FIRST=1
    while IFS= read -r RUN
    do

        ROOTFILE=$(alien_ls "${BASEDIR}/${PASS}/${RUN}/QC/vertexQC.root" 2> /dev/null)
        #echo "ROOTFILE: $ROOTFILE"
        if [ -n "${ROOTFILE}" ]; then
            echo "Run $RUN found"
            if [ ${FIRST} -eq 0 ]; then
                RUNLIST="${RUNLIST}, "
                RUNLISTJSON="${RUNLISTJSON}, "
            fi
            RUNLIST="${RUNLIST}${RUN}"
            RUNLISTJSON="${RUNLISTJSON}\"${RUN}\""
            FIRST=0
        fi
        
    done < <(alien_ls "${BASEDIR}/${PASS}" | tr -d "/")

else

    BASEDIR="/alice/${TYPE}/${YEAR}/${PERIOD}"
    echo "--------------------"
    echo "Getting completed runs from $BASEDIR/*/${PASS}"
    #echo "alien_find $BASEDIR \"*/$PASS/*/QC/QC_fullrun.root\""
    echo "--------------------"
    
    FIRST=1
    while IFS= read -r LINE
    do
        
        #echo $LINE
        RUN=$(echo "$LINE" | tr "/" "\n" | head -n 6 | tail -n 1)
        echo "Run $RUN found"
        if [ ${FIRST} -eq 0 ]; then
            RUNLIST="${RUNLIST}, "
            RUNLISTJSON="${RUNLISTJSON}, "
        fi
        RUNLIST="${RUNLIST}${RUN}"
        RUNLISTJSON="${RUNLISTJSON}\"${RUN}\""
        FIRST=0
        #echo "        \"$RUN\","
        
    done < <(alien_find $BASEDIR "*/$PASS/*/QC/QC_fullrun.root")

fi

echo ""; echo $RUNLIST  

if [ x"${UPDATE_CONFIG}" = "x1" ]; then
    #echo "$RUNLISTJSON"
    # save the .productionRuns key as a comma-separated single line
    PRODUCTIONRUNS=$(cat "$CONFIG" | jq -c '.productionRuns' | tr -d '[' | tr -d ']' | sed 's|,|, |g')

    # replace the contents of the .productionRuns and .runs keys with strings that can be easily replaced with sed
    jq -j --argjson array "[\"PRODUCTIONRUNLIST\"]" '.productionRuns = $array' "$CONFIG" > temp.json && cp temp.json "$CONFIG" && rm temp.json
    jq -j --argjson array "[\"RUNLIST\"]" '.runs = $array' "$CONFIG" > temp.json && cp temp.json "$CONFIG" && rm temp.json

    # use sed to inject the single-line runlists into the JSON configuration
    cat "$CONFIG" | sed "s|\"PRODUCTIONRUNLIST\"|$PRODUCTIONRUNS|" | sed "s|\"RUNLIST\"|$RUNLIST|" > temp.json && cp temp.json "$CONFIG" && rm temp.json
    
fi
