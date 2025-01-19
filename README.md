# MUON Async QC tools

## Introduction

The analysis code for the Async QC uses the `QC_fullrun.root` files of the processed runs as a starting point, and generates PDF files with the comparison of the various runs in interaction rate intervals. The plots from the inidividual runs are also compared with those of reference runs, and their compatibility is checked to identify potential bad time intervals.

## Configuration

The configuration of the QC analysis uses two configuration files in JSON format, one containing the list of input and reference files, the other the configuration of the produced plots.

### Run list configuration

The input ROOT files are specified via the year, period and reconstruction pass of the production being checked. In addition to the list of runs, the configuration contains also one or more reference run numbers, each valid up to a given interaction rate value.

The QC analysis parameters are defined in a JSON configuration file, an example of which is given below.
In particular, it specifies the reconstruction pass, the list of runs, the reference runs and the configuration of the plots to be analyzed.

```
{
    "year": "2024",
    "period": "LHC24ar",
    "pass": "cpass0",
    "runs": [
        "560031",
        "560033",
        "560034",
        "560049",
        "560066",
        "560067",
        "560070",
        "560089",
        "560090",
        "560105",
        "560106",
        "560123",
        "560127",
        "560141",
        "560142"
    ],
    "referenceRuns": [
        {
            "number": "560070",
            "rateMax": "15"
        },
        {
            "number": "560034",
            "rateMax": "28"
        },
        {
            "number": "560033",
            "rateMax": "45"
        },
        {
            "number": "560031",
            "rateMax": "50"
        }
    ],
```

### Plots configuration

The plots are identified by their path in the ROOT file, given by `/detector/task/name`.
The display of the plots and their comparison with the reference ones is controlled by some additional options:
* `"drawOptions"`: the string to be passed to the histogram's Draw() function
* `logx`, `logy`: if set to `1`, the corresponding axis is drawn inlog scale
* `"projection"`: for 2-D histograms, draw the projection into the specified axis (`"x"` or `"y"`)

#### Comparison with reference values

The analysis macro uses reference runs to assess the quality of the plots. The configuration can include one or more reference runs, each valid up to a given maximum interaction rate. In the configuration above, run 560070 is for example used to check plots corresponding to rates up to 15 kHz.

In the case that no reference run is found for a given rate interval, the expected distribution is estimated by computing the average of all the histograms in the interval.

The comparison with the reference values can be configured with the following parameters:
* `"checkRangeMin"`, `"checkRangeMin"`: the horizontal range to be considered for the comparion with the reference run(s)
* `"checkThreshold"`: the maximum acceptable deviation from unity of the ratio to the reference plot
* `"maxBadBinsFrac"`: the fraction of bins above/below the threshold above which the check is considered to be Bad


An example of plots configuration is given below.

```
{
    "id": "test",
    "beamType": "Pb-Pb",
    "plots": [
        {
            "detector": "MFT",
            "task": "Tracks",
            "name": "mMFTTrackEta",
            "drawOptions": "H",
            "checkRangeMin": "-3.4",
            "checkRangeMax": "-2.4",
            "checkThreshold": "0.025",
            "maxBadBinsFrac": "0.25"
        },
        {
            "detector": "MFT",
            "task": "Tracks",
            "name": "mMFTTrackPhi",
            "checkThreshold": "0.1",
            "maxBadBinsFrac": "0.25"
        },
        {
            "detector": "GLO",
            "task": "MUONTracks",
            "name": "MCH-MID/WithCuts/TrackPt",
            "checkThreshold": "0.05",
            "maxBadBinsFrac": "0.05",
            "drawOptions": "H",
            "logx": "1",
            "logy": "0"
        },
        {
            "detector": "TPC",
            "task": "Tracks",
            "name": "h2DNClustersEta",
            "drawOptions": "H",
            "projection": "x",
            "checkThreshold": "0.05",
            "maxBadBinsFrac": "0.05"
        }
    ],
    "trends": [
        {
            "detector": "MFT",
            "task": "Tracks",
            "name": "mMFTTrackROFSize"
        }
    ]
}
```

### Fetching of QC.root files

The input `QC_fullrun.root` files need to be downloaded locally. An helper script is provided to automate this process, taking the runs configuration file as parameter:
```
./aqc-fetch.sh runs.json
```

The command above will download all the root files under `inputs/YEAR/PERIOD/PASS/RUN`. Files that were already downloaded will be skipped.
The script will fetch all the `QC_fullrun.root` files from the async jobs of the runs listed in the configuration, as well as those of the reference runs.

### Processing the QC.root files

Once the root files are downloaded locally, they can be processed via the following helper script, taking the runs and plots configuration files as parameters:

```
./aqc-process.sh runs.json plots.json
```

The PDF files with the output plots are stored under `outputs/ID/YEAR/PERIOD/PASS`.
At the end the script also prints the list of plots that did not fulfill the compatibility criteria with the referece plots, for example:

```
[O2PDPSuite/latest-o2-epn] ~/Workflows/MuonAsyncQC $> ./aqc-process.sh config.json 
root -b -q "aqc_process.C(\"inputs/test/qclist.txt\", \"config.json\")"
Bad time interval for plot "mMFTTrackEta": 560123 [07:14:26 - 07:19:26]
Bad time interval for plot "mMFTTrackEta": 560123 [07:09:26 - 07:14:26]
Bad time interval for plot "mMFTTrackEta": 560123 [06:59:26 - 07:04:26]
Bad time interval for plot "mMFTTrackEta": 560123 [07:04:26 - 07:09:26]
Bad time interval for plot "mMFTTrackEta": 560127 [07:53:06 - 07:58:06]
```
