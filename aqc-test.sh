#! /bin/bash

export INFOLOGGER_MODE=stdout
export SCRIPTDIR=$(readlink -f $(dirname $0))
#echo "SCRIPTDIR: ${SCRIPTDIR}"

RUN=$1

export ALIEN_JDL_LPMRUNNUMBER=${RUN}
export ALIEN_JDL_LPMINTERACTIONTYPE=$2 # pp or PbPb
export ALIEN_JDL_LPMPRODUCTIONTAG=muontest
export ALIEN_JDL_LPMPASSNAME=muontest
export ALIEN_JDL_LPMANCHORYEAR=2024

export ALIEN_JDL_WORKFLOWDETECTORS=CTP,MFT,MCH,MID
export ALIEN_JDL_AODOFF=1
export ALIEN_JDL_REMOTEREADING=1

rm -rf ${RUN} && mkdir -p ${RUN} && cp ctf-${RUN}.list ${RUN}/ctf-${RUN}.list && cd ${RUN} && ${O2DPG_ROOT}/DATA/production/configurations/asyncReco/async_pass.sh ctf-${RUN}.list
