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

## Getting the list of completed runs for a given production

An utility script allows to print the list of runs that are identified as completed for the production specified in the JSON configuration file, like in the example below:

```
./aqc-get-completed-runs.sh runs.json
--------------------
Getting completed runs from /alice/data/2024/LHC24ar/*/apass1
--------------------

559544, 559545, 559561, 559574, 559575, 559595, 559596, 559613, 559614, 559615, 559616, 559617, 559631, 559632, 559679, 559680, 559681, 559713, 559731, 559749, 559762, 559781, 559782, 559783, 559784, 559802, 559803, 559804, 559827, 559828, 559830, 559843, 559856, 559901, 559902, 559903, 559917, 559919, 559920, 559933, 559966, 559968, 559969, 559970, 559987, 560012, 560031, 560033, 560034, 560049, 560066, 560067, 560070, 560089, 560090, 560105, 560106, 560123, 560127, 560141, 560142, 560162, 560163, 560164, 560168, 560169, 560184
```

### Auto-filling the configuration with the list of completed runs

If the `-u` option is passed to the script as the first parameter, the `'runs` key in the configuration file is automatically updated with the list of completed runs, like this:

    ./aqc-get-completed-runs.sh -u runs.json

## Creating a new runs configuration for a given production

The following steps should be followed in order to create a new JSON configuration for a given production. In the examples below, we will be creating from scratch a configuration for the `apass1` production pass of the `LHC24as` period.


1\. copy the runs configuration template:

   `cp runs-data-template.json runs-LHC24as.json`

2\. fill the `"year"`, `"period"`, `"pass`" and `"beamType"` fields with the appropriate values. The beam type can be set to either `"pp"` or `"Pb-Pb"`.
For this example, the configuration should look like this:

    ```
    {
      "type": "data",
      "year": "2024",
      "period": "LHC24as",
      "pass": "apass1",
      "beamType": "Pb-Pb",
      "enable_chunks": "0",
      "productionRuns": [
      ],
      "productionStart": "1970-01-01 00:00:00",
      "runs": [
      ],
      "referenceRuns": [
      ]
    }
    ```
You can ignore the `"productionRuns"` and `"productionStart"` fields for now.

3\. Fill the configuration with the completed runs:

    ```
    ./aqc-get-completed-runs.sh -u runs-LHC24as.json
    ```

The configuration at this point should look like this:

```
{
  "type": "data",
  "year": "2024",
  "period": "LHC24as",
  "pass": "apass1",
  "beamType": "Pb-Pb",
  "enable_chunks": "0",
  "productionRuns": [
    
  ],
  "productionStart": "1970-01-01 00:00:00",
  "runs": [
    560223, 560229, 560243, 560244, 560279, 560310, 560313, 560314, 560330, 560331, 560334, 560352, 560355, 560371, 560397, 560399, 560401, 560402
  ],
  "referenceRuns": []
}
```

It is now possible to move to the next step, and fetch the QC ROOT files locally.

## Fetching of QC_fullrun.root files

The input `QC_fullrun.root` files need to be downloaded locally. An helper script is provided to automate this process, taking the runs configuration file as parameter:
```
./aqc-fetch.sh runs.json
```

The command above will download all the root files under `inputs/YEAR/PERIOD/PASS/RUN`. Files that were already downloaded will be skipped.
The script will fetch all the `QC_fullrun.root` files from the async jobs of the runs listed in the configuration, as well as those of the reference runs.

## Processing the QC_fullrun.root files

Once the root files are downloaded locally, they can be processed via the following helper script, taking the runs and plots configuration files as parameters:

```
./aqc-process.sh runs.json plots.json
```

The PDF files with the output plots are stored under `outputs/ID/YEAR/PERIOD/PASS`.
At the end the script also prints the list of plots that did not fulfill the compatibility criteria with the referece plots, for example:

```
[O2PDPSuite/latest-o2-epn] ~/Workflows/MuonAsyncQC $> ./aqc-process.sh runs.json plots.json
root -b -q "aqc_process.C(\"inputs/test/qclist.txt\", \"config.json\")"
Bad time interval for plot "mMFTTrackEta": 560123 [07:14:26 - 07:19:26]
Bad time interval for plot "mMFTTrackEta": 560123 [07:09:26 - 07:14:26]
Bad time interval for plot "mMFTTrackEta": 560123 [06:59:26 - 07:04:26]
Bad time interval for plot "mMFTTrackEta": 560123 [07:04:26 - 07:09:26]
Bad time interval for plot "mMFTTrackEta": 560127 [07:53:06 - 07:58:06]
```
