# MUON Async QC tools

## Analysis of Asyn QC output

The analysis code for the Async QC uses the intermediate `QC.root` files of the individual async jobs as a starting point, and generates PDF files with the comparison of the various runs in interaction rate intervals. The plots from the inidividual runs are also compared with those of reference runs, and their compatibility is checked to identify potential bad time intervals.

### Configuration

The QC analysis parameters are defined in a JSON configuration file, an example of which is given below.
In particular, it specifies the reconstruction pass, the list of runs, the reference runs and the configuration of the plots to be analyzed.

```
{
    "id": "test",
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

The input `QC.root` files need to be downloaded locally. An helper script is provided to automate this process:
```
./aqc-fetch.sh config.json
```

The command above will download all the root files under `inputs/ID/YEAR/PERIOD/PASS`, which would correspond to `inputs/test/2024/LHC24ar/cpass0` with the sample configuration file above. Files that were already downloaded will be skipped.
The script will fetch all the `QC.root` files from the intermediate async jobs of the runs listed in the configuration, as well as those of the reference runs.

### Processing the QC.root files

Once the root files are downloaded locally, they can be processed via the following helper script:

```
./aqc-process.sh config.json
```
which in turn runs the `aqc_process.C` root macro with the list of input root files and the name of the configurations file.

The PDF files with the output plots are stored under `outputs/ID`, which would correspond to `outputs/test` with the sample configuration file above.
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

#### Conparison with reference plots

The analysis macro uses reference runs to assess the quality of the plots. The configuration can include one or more reference runs, each valid up to a given maximum interaction rate. In the configuration above, run 560070 is for example used to check plots corresponding to rates up to 15 kHz.

The quality of the plots is assessed using two criteria:
* all histogram bins whose relative deviation from the reference is higher than `checkThreshold` are considered Bad
* if a fraction of bin larger than `maxBadBinsFrac` is Bad, then the plots is considered Bad
