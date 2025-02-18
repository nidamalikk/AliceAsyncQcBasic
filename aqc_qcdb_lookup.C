#include <QualityControl/MonitorObject.h>
#include <QualityControl/MonitorObjectCollection.h>
#include "QualityControl/ObjectMetadataKeys.h"
#include "QualityControl/CcdbDatabase.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string>
#include <set>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "nlohmann/json.hpp"
using json = nlohmann::json;

using namespace o2::quality_control::core;
using namespace o2::quality_control::repository;

std::string sessionID;
std::string type;
std::string year;
std::string period;
std::string pass;
std::string beamType;

std::string mDatabaseUrl;
CcdbDatabase mDatabase;


std::tuple<uint64_t, uint64_t, uint64_t, int> getObjectInfo(const std::string path, const std::map<std::string, std::string>& metadata)
{
  // find the time-stamp of the most recent object matching the current activity
  // if ignoreActivity is true the activity matching criteria are not applied

  auto listing = mDatabase.getListingAsPtree(path, metadata, true);
  if (listing.count("objects") == 0) {
    //std::cout << "Could not get a valid listing from db '" << mDatabaseUrl << "' for latestObjectMetadata '" << path << "'" << std::endl;
    return std::make_tuple<uint64_t, uint64_t, uint64_t, int>(0, 0, 0, 0);
  }
  const auto& objects = listing.get_child("objects");
  if (objects.empty()) {
    //std::cout << "Could not find the file '" << path << "' in the db '"
    //                     << mDatabaseUrl << "' for given Activity settings. Zeroes and empty strings are treated as wildcards." << std::endl;
    return std::make_tuple<uint64_t, uint64_t, uint64_t, int>(0, 0, 0, 0);
  } else if (objects.size() > 1) {
    //std::cout << "Expected just one metadata entry for object '" << path << "'. Trying to continue by using the first." << std::endl;
  }

  // retrieve timestamps
  const auto& latestObjectMetadata = objects.front().second;
  uint64_t creationTime = latestObjectMetadata.get<uint64_t>(metadata_keys::created);
  uint64_t validFrom = latestObjectMetadata.get<uint64_t>(metadata_keys::validFrom);
  uint64_t validUntil = latestObjectMetadata.get<uint64_t>(metadata_keys::validUntil);

  // retrieve 'runNumber' metadata element, or zero if not set
  uint64_t runNumber = latestObjectMetadata.get<int>(metadata_keys::runNumber, 0);

  //TDatime datime;
  //datime.Set(creationTime / 1000);
  //std::cout << std::format("Run {}  created at {} ({})\n", runNumber, datime.AsSQLString(), creationTime);

  return std::make_tuple(validFrom, validUntil, creationTime, runNumber);
}


void aqc_qcdb_lookup(const char* runsConfig)
{
  std::ifstream fRunsConfig(runsConfig);
  auto jRunsConfig = json::parse(fRunsConfig);

  type = jRunsConfig.at("type").get<std::string>();
  year = jRunsConfig.at("year").get<std::string>();
  period = jRunsConfig.at("period").get<std::string>();
  pass = jRunsConfig.at("pass").get<std::string>();

  // input runs
  std::vector<int> inputRuns = jRunsConfig.at("productionRuns");
  std::vector<int> runNumbers;
  for (const auto& inputRun : inputRuns) {
    runNumbers.push_back(inputRun);
  }
  TDatime productionStart(jRunsConfig.at("productionStart").get<std::string>().c_str());
  std::cout << "Production started on " << productionStart.AsSQLString() << "  "
      << productionStart.Convert() << std::endl;

  std::string dbPrexif;
  if (type == "sim") {
    mDatabaseUrl = "ali-qcdbmc-gpn.cern.ch:8083";
    dbPrexif = "qc_mc";
  } else {
    mDatabaseUrl = "ali-qcdb-gpn.cern.ch:8083";
    dbPrexif = "qc_async";
  }
  mDatabase.connect(mDatabaseUrl, "", "", "");

  std::vector<std::string> plotsToLookup{
    dbPrexif + "/ITS/MO/Tracks/EtaDistribution",
    dbPrexif + "/TPC/MO/Tracks/hEta"
  };


  std::string runlist;
  std::string runlistMissing;
  for (auto runNumber : runNumbers) {
    std::map<std::string, std::string> metadata;
    metadata[metadata_keys::runNumber] = std::to_string(runNumber);
    metadata[metadata_keys::periodName] = period;
    if (type != "sim") {
      metadata[metadata_keys::passName] = pass;
    }

    //std::cout << std::endl << std::format("Checking run {}", runNumber) << std::endl;
    bool found = true;
    for (auto plot: plotsToLookup) {
      auto timestamps = getObjectInfo(plot, metadata);
      auto objRunNumber = std::get<3>(timestamps);
      if (objRunNumber != runNumber) {
        std::cout << std::format("Run {}: plot \"{}\" not found in QCDB", runNumber, plot) << std::endl;
        found = false;
        continue;
      }
      auto objCreationTime = std::get<2>(timestamps) / 1000;
      //std::cout << std::format("Run {}  created {}  start {}\n", runNumber, creationTime, productionStart.Convert());
      if (objCreationTime < productionStart.Convert()) {
        TDatime objCreationTimeAsDate;
        objCreationTimeAsDate.Set(objCreationTime);
        std::cout << std::format("Run {}: plot \"{}\" is too old ({})", runNumber, plot, objCreationTimeAsDate.AsSQLString()) << std::endl;
        found = false;
        continue;
      }
    }
    if (found) {
      if (!runlist.empty()) {
        runlist += ", ";
      }
      runlist += std::to_string(runNumber);
    } else {
      if (!runlistMissing.empty()) {
        runlistMissing += ", ";
      }
      runlistMissing += std::to_string(runNumber);
    }
  }
  std::cout << "\n\n=============================\nList of runs missing in QCDB\n=============================\n\n" << runlistMissing << std::endl;
  std::cout << "\n\n=============================\nList of runs found in QCDB\n=============================\n\n" << runlist << std::endl;
}
