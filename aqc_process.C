#include <QualityControl/MonitorObject.h>
#include <QualityControl/MonitorObjectCollection.h>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string>
#include <set>

//#include <DataFormatsCTP/CTPRateFetcher.h>
#include "./CTPRateFetcher.h"

//#include <boost/property_tree/ptree.hpp>
//#include <boost/property_tree/json_parser.hpp>

#include <chrono>

#include "nlohmann/json.hpp"
using json = nlohmann::json;

using namespace o2::quality_control::core;

std::string sessionID;
std::string year;
std::string period;
std::string pass;
std::string beamType;

//std::string CTPScalerSourceName{ "T0VTX" };
std::string CTPScalerSourceName{ "ZNC-hadronic" };

std::map<int, std::shared_ptr<o2::ctp::CTPRateFetcher>> ctpRateFatchers;

std::vector<std::pair<double, double>> rateIntervals;

//std::vector<std::pair<int, double>> referenceRunsMap{ {560034, 29}, {560033, 50} };
std::map<double, int> referenceRunsMap; //{ {15, 560070}, {29, 560034}, {40, 560033}, {50, 560031} };

std::map<int, std::shared_ptr<TH1>> referencePlots;

std::map<int, std::map<std::string, std::set<std::pair<long, long>>>> badTimeIntervals;

using namespace o2::quality_control::core;

struct PlotConfig
{
  std::string detectorName;
  std::string taskName;
  std::string plotName;
  std::string plotLabel;
  std::string projection;
  std::string drawOptions;
  bool logx;
  bool logy;
  double checkRangeMin;
  double checkRangeMax;
  double checkThreshold;
  double checkDeviationNsigma;
  double maxBadBinsFrac;
  bool normalize;
};

struct Plot
{
  //Plot(std::shared_ptr<TH1> h, Activity& a, ValidityInterval& v)
  //: histogram(h), activity(a), validity(v) {}

  Plot(TH1* h, Activity a, ValidityInterval v)
  : histogram(h), activity(a), validity(v) {}

  std::shared_ptr<TH1> histogram;
  Activity activity;
  ValidityInterval validity;
};

struct Canvas
{
  std::shared_ptr<TCanvas> canvas;
  std::shared_ptr<TPad> padTop;
  std::shared_ptr<TPad> padBottom;
  std::shared_ptr<TPad> padRight;
};

std::string getPlotOutputFilePrefix(const PlotConfig& plotConfig)
{
  std::string plotNameWithDashes = plotConfig.plotName;
  std::replace( plotNameWithDashes.begin(), plotNameWithDashes.end(), '/', '-');
  std::string outputFileName = std::string("outputs/") + sessionID + "/" + year + "/" + period + "/" + pass + "/" + plotConfig.detectorName + "-" + plotConfig.taskName + "-" + plotNameWithDashes;

  if (!plotConfig.projection.empty()) {
    outputFileName += std::string("-proj") + plotConfig.projection;
  }

  return outputFileName;
}

double getRateForMO(std::shared_ptr<MonitorObject> mo) {
  int runNumber = mo->getActivity().mId;
  auto validityMin = mo->getValidity().getMin();
  auto validityMax = mo->getValidity().getMax();
  auto timestamp = (mo->getValidity().getMax() + mo->getValidity().getMin()) / 2;

  auto& ccdbManager = o2::ccdb::BasicCCDBManager::instance();

  if (ctpRateFatchers.count(runNumber) < 1) {

    // start and stop time of the run
    auto rl = ccdbManager.getRunDuration(runNumber);
    // use the middle of the run as timestamp for accessing CCDB objects
    auto runTimestamp = std::midpoint(rl.first, rl.second);

    // re-create and re-initialise the rate fetcher object at each new run
    ctpRateFatchers[runNumber] = std::make_shared<o2::ctp::CTPRateFetcher>();
    ctpRateFatchers[runNumber]->setupRun(runNumber, &ccdbManager, runTimestamp, true);
  }

  double rate = 0;
  double nPoints = 0;
  timestamp = validityMin;
  while (timestamp < validityMax) {
    rate += ctpRateFatchers[runNumber]->fetchNoPuCorr(&ccdbManager, timestamp, runNumber, CTPScalerSourceName) / 1000;
    timestamp += 30000;
    nPoints += 1;
  }

  rate = (nPoints > 0) ? (rate / nPoints) : 0;
  std::cout << "Rate for run " << runNumber << " and timestamp " << timestamp << " and source \"" << CTPScalerSourceName << "\" is " << rate << " kHz" << std::endl;

  return rate;
}

int getRateIntervalIndex(double rate)
{
  for (int ri = 0; ri < rateIntervals.size(); ri++) {
    auto& rateInterval = rateIntervals[ri];
    if (rate >= rateInterval.first && rate < rateInterval.second) {
      return ri;
    }
  }
  return -1;
}

TDirectory* GetDir(TDirectory* d, TString histname)
{
  //TString histname = TString::Format("ST%d/DE%d/Occupancy_B_XY_%d", station, de, de);
  TKey *key = d->GetKey(histname);
  //std::cout << "dirName: " << histname << "  key: " <<key << std::endl;
  if (!key) return NULL;
  TDirectory* dir = (TDirectory*)key->ReadObjectAny(TDirectory::Class());
  //std::cout << "dirName: " << histname << "  dir: " << dir << std::endl;
  return dir;
}

MonitorObjectCollection* GetMOC(TDirectory* f, TString histname)
{
  //TString histname = TString::Format("ST%d/DE%d/Occupancy_B_XY_%d", station, de, de);
  TKey *key = f->GetKey(histname);
  //std::cout << "MOCname: " << histname << "  key: " <<key << std::endl;
  if (!key) return NULL;
  auto* moc = (MonitorObjectCollection*)key->ReadObjectAny(MonitorObjectCollection::Class());
  //std::cout << "MOCname: " << histname << "  moc: " << moc << std::endl;
  return moc;
}

MonitorObject* GetMO(TFile* f, std::array<std::string, 4>& path)
{
  TDirectory* dir = GetDir(f, path[0].c_str());
  dir = GetDir(dir, path[1].c_str());
  auto* moc = GetMOC(dir, path[2].c_str());
  auto* mo = (MonitorObject*)moc->FindObject(path[3].c_str());
  //std::cout << "mo: " << mo << std::endl;
  //if (mo) {
  //  std::cout << "  run number: " << mo->getActivity().mId << std::endl;
  //  std::cout << "  validity: " << mo->getValidity().getMin() << " -> " << mo->getValidity().getMax() << std::endl;
  //}
  return mo;
}

std::vector<std::shared_ptr<MonitorObject>> GetMOMW(TFile* f, const PlotConfig& plotConfig)
{
  std::vector<std::shared_ptr<MonitorObject>> result;

  TDirectory* dir = GetDir(f, "mw");
  if (!dir) {
    std::cout << "Directory \"mw\" not found in ROOT file \"" << f->GetPath() << "\"" << std::endl;
    return result;
  }
  dir = GetDir(dir, plotConfig.detectorName.c_str());
  if (!dir) {
    std::cout << "Directory \"" << plotConfig.detectorName << "\" not found in ROOT file \"" << f->GetPath() << "\"" << std::endl;
    return result;
  }
  dir = GetDir(dir, plotConfig.taskName.c_str());
  if (!dir) {
    std::cout << "Directory \"" << plotConfig.taskName << "\" not found in ROOT file \"" << f->GetPath() << "\"" << std::endl;
    return result;
  }
  auto listOfKeys = dir->GetListOfKeys();
  for (int i = listOfKeys->GetEntries() - 1 ; i >= 0; --i) {
    //std::cout<< "i: " << i << "  " << listOfKeys->At(i)->GetName() << std::endl;
    auto* moc = dynamic_cast<o2::quality_control::core::MonitorObjectCollection*>(dir->Get(listOfKeys->At(i)->GetName()));
    if (!moc) continue;
    //std::cout << "Getting MO \"" << plotConfig.plotName << "\" from \"" << moc->GetName() << "\"" << std::endl;
    auto* moPtr = (MonitorObject*)moc->FindObject(plotConfig.plotName.c_str());
    //std::cout << "mo: " << moPtr << std::endl;
    if (!moPtr) continue;
    std::shared_ptr<MonitorObject> mo{ moPtr };
    //std::cout << "  run number: " << mo->getActivity().mId << std::endl;
    //std::cout << "  validity: " << mo->getValidity().getMin() << " -> " << mo->getValidity().getMax() << std::endl;
    result.push_back(mo);
  }
  return result;
}
/*
std::vector<std::shared_ptr<MonitorObject>> GetMOMW(std::string fname, const PlotConfig& plotConfig)
{
  std::shared_ptr<TFile> f = std::make_shared<TFile>(fname.c_str());
  return GetMOMW(f, plotConfig);
}
*/
TH1* GetHist(TFile* f, std::array<std::string, 4>& path)
{
  TDirectory* dir = GetDir(f, path[0].c_str());
  dir = GetDir(dir, path[1].c_str());
  auto* moc = GetMOC(dir, path[2].c_str());
  auto* mo = (MonitorObject*)moc->FindObject(path[3].c_str());
  //std::cout << "mo: " << mo << std::endl;
  TH1* h1 = dynamic_cast<TH1*>(mo->getObject());
  //std::cout << "h1: " << h1 << std::endl;
  return h1;
}

bool splitPlotPath(std::string plotPath, std::array<std::string, 4>& plotPathSplitted)
{
  std::string delimiter("/");

  for (int i = 0; i < 3; i++) {
    auto index = plotPath.find(delimiter);
    if (index == std::string::npos) {
      return false;
    }
    plotPathSplitted[i] = plotPath.substr(0, index);
    plotPath.erase(0, index + 1);
  }
  plotPathSplitted[3] = plotPath;

  for (auto s : plotPathSplitted) {
    std::cout << s << " ";
  }
  std::cout << std::endl;

  return true;
}
/*
using DateTime = std::pair<std::chrono::year_month_day, std::chrono::hh_mm_ss<std::chrono::milliseconds>>;

DateTime getLocalTime(uint64_t timeStamp, const char* timeZone)
{
  DateTime result;
  auto localTime = std::chrono::zoned_time{timeZone, std::chrono::system_clock::time_point{std::chrono::milliseconds{timeStamp}}}.get_local_time();
  result.first = std::chrono::year_month_day{floor<std::chrono::days>(localTime)};
  result.second = std::chrono::hh_mm_ss{floor<std::chrono::milliseconds>(localTime - floor<std::chrono::days>(localTime))};
  return result;
}

int getYear(DateTime dateTime)
{
  return int(dateTime.first.year());
}

uint32_t getMonth(DateTime dateTime)
{
  return unsigned(dateTime.first.month());
}

uint32_t getDay(DateTime dateTime)
{
  return unsigned(dateTime.first.day());
}

int getHour(DateTime dateTime)
{
  return dateTime.second.hours().count();
}

int getMinute(DateTime dateTime)
{
  return dateTime.second.minutes().count();
}

int getSecond(DateTime dateTime)
{
  return dateTime.second.seconds().count();
}
*/
int getReferenceRunForRate(double rate)
{
  int result = 0;
  for (auto [maxRate, runNumber] : referenceRunsMap) {
    if (rate <= maxRate) {
      std::cout << "rate: " << rate << "  maxRate: " << maxRate << "  referenceRun: " << runNumber << std::endl;
      result = runNumber;
      break;
    }
  }
  return result;
}

void loadPlotsFromRootFiles(std::vector<std::shared_ptr<TFile>>& rootFiles, const PlotConfig& plotConfig,
    std::map<int, std::multimap<double, std::shared_ptr<MonitorObject>>>& monitorObjects)
{

  auto& ccdbManager = o2::ccdb::BasicCCDBManager::instance();

  for (auto rootFile : rootFiles) {
    std::cout << "Loading plot \"" << plotConfig.plotName << "\" from file " << rootFile->GetPath() << std::endl;
    auto moVector = GetMOMW(rootFile.get(), plotConfig);

    for (auto& mo : moVector) {
      int runNumber = mo->getActivity().mId;
      auto timestamp = mo->getValidity().getMax(); //(mo->getValidity().getMax() + mo->getValidity().getMin()) / 2;

      TH1* hist = dynamic_cast<TH1*>(mo->getObject());
      if (!hist) continue;

      std::cout << "Loaded MO from file " << rootFile
          << " and validity " << mo->getValidity().getMin()
          << " -> " << mo->getValidity().getMax() << std::endl;

      // check if a MO with the same validity was already loaded, in which case we add the
      // current one instead of adding a new entry in the map
      bool histAdded = false;
      if (monitorObjects.count(runNumber) > 0) {
        for (auto& [rate, moFromMap] : monitorObjects[runNumber]) {
          if ( moFromMap->getValidity() == mo->getValidity()) {
            TH1* histFromMap = dynamic_cast<TH1*>(moFromMap->getObject());
            if (!histFromMap) continue;

            histFromMap->Add(hist);
            histAdded = true;
            std::cout << "MO added to existing one" << std::endl;
            break;
          }
        }
      }

      // if the histogram was added to an existing one, we stop here
      if (histAdded) continue;

      double rate = getRateForMO(mo);
      std::cout << "Rate for run " << runNumber << " and timestamp " << timestamp << " and source \"" << CTPScalerSourceName << "\" is " << rate << " kHz" << std::endl;

      monitorObjects[runNumber].insert({rate, mo});
    }
  }
}

void populateRateIntervals(const std::map<int, std::multimap<double, std::shared_ptr<MonitorObject>>>& monitorObjects,
                           std::map<int, std::vector<std::shared_ptr<MonitorObject>>>& monitorObjectsInRateIntervals)
{
  //auto& ccdbManager = o2::ccdb::BasicCCDBManager::instance();

  for (auto& [runNumber, moMap] : monitorObjects) {
    for (auto& [rate, mo] : moMap) {
      int index = getRateIntervalIndex(rate);
      if (index < 0) continue;
      monitorObjectsInRateIntervals[index].push_back(mo);
    }
  }
}

void populateReferencePlots(const std::map<int, std::multimap<double, std::shared_ptr<MonitorObject>>>& monitorObjects)
{
  referencePlots.clear();

  for (auto& [runNumber, moMap] : monitorObjects) {
    for (auto& [rate, mo] : moMap) {
      TH1* hist = dynamic_cast<TH1*>(mo->getObject());
      if (!hist) continue;

      auto timestamp = mo->getValidity().getMax();

      int index = getRateIntervalIndex(rate);
      if (index < 0) continue;

      double referenceRate = rateIntervals[index].second;
      int refRunNumber = getReferenceRunForRate(referenceRate);
      std::cout << "Reference run for " << referenceRate << " [" << index << "] is " << refRunNumber << std::endl;
      if (refRunNumber != runNumber) continue;

      // update reference plot for this rate interval
      if (referencePlots.count(index) < 1) {
        std::cout << "Initializing reference plot \"" << hist->GetName() << "\" for " << referenceRate << " [" << index << "] from run " << refRunNumber << std::endl;
        // the reference plot for this rate interval was not yet initialized
        referencePlots[index].reset((TH1*)hist->Clone(TString::Format("%s_%d_%lu_%d_Ref", hist->GetName(), runNumber, timestamp, index)));
      } else {
        std::cout << "Adding reference plot \"" << hist->GetName() << "\" for run " << refRunNumber << std::endl;
        std::cout << "Exisitng reference plot: " << referencePlots[index].get() << std::endl;
        std::cout << "Exisitng reference plot: \"" << referencePlots[index]->GetName() << "\"" << std::endl;
        referencePlots[index]->Add(hist);
      }
    }
  }

  for (int index = 0; index < rateIntervals.size(); index++) {
    if (referencePlots.count(index) < 1) {
      std::cout << "No reference plot for " << rateIntervals[index].second << " [" << index << "]" << std::endl;
    }
  }
}

void plotRun(const PlotConfig& plotConfig, int runNumber, std::map<int, std::vector<std::shared_ptr<MonitorObject>>>& monitorObjectsInRateIntervals)
{
  int cW = 1800;
  int cH = 600;
  TCanvas c("c","c",cW,cH);
  c.SetRightMargin(0.3);

  std::string outputFileName = getPlotOutputFilePrefix(plotConfig) + std::format("-{}.pdf", runNumber);

  bool firstPage = true;
  for (auto& [index, moVec] : monitorObjectsInRateIntervals) {
    if (moVec.empty()) continue;

    auto legend = new TLegend(0.75,0.1,0.95,0.9);

    int lineColor = 1;
    int nPlots = 0;
    bool first = true;
    for (auto& mo : moVec) {
      TH1* histTemp = dynamic_cast<TH1*>(mo->getObject());
      //std::cout << "hist: " << hist << std::endl;
      if (!histTemp) continue;
      TH1* hist = (TH1*)histTemp->Clone("_clone");

      int moRunNumber = mo->getActivity().mId;
      if (moRunNumber != runNumber) {
        continue;
      }

      hist->Scale(1.0 / hist->Integral());
      hist->SetLineColor(lineColor);
      lineColor += 1;
      if (nPlots == 0) {
        hist->SetTitle(TString::Format("%s [%0.1f kHz, %0.1f kHz]", hist->GetTitle(), rateIntervals[index].first, rateIntervals[index].second));
        hist->Draw("H");
      }
      else hist->Draw("H same");
      nPlots += 1;

      TDatime daTime;
      daTime.Set(mo->getValidity().getMin()/1000);
      int hourMin = daTime.GetHour();
      int minuteMin = daTime.GetMinute();
      daTime.Set(mo->getValidity().getMax()/1000);
      int hourMax = daTime.GetHour();
      int minuteMax = daTime.GetMinute();
      /*
      auto validityMin = getLocalTime(mo->getValidity().getMin(), "Europe/Paris");
      auto hourMin = getHour(validityMin);
      auto minuteMin = getMinute(validityMin);
      auto secondMin = getSecond(validityMin);
      auto validityMax = getLocalTime(mo->getValidity().getMax(), "Europe/Paris");
      auto hourMax = getHour(validityMax);
      auto minuteMax = getMinute(validityMax);
      auto secondMax = getSecond(validityMax);
      */
      legend->AddEntry(hist,TString::Format("%d [%02d:%02d - %02d:%02d]", mo->getActivity().mId, hourMin, minuteMin, hourMax, minuteMax),"l");
    }

    if (nPlots < 1) continue;

    legend->Draw();

    if (firstPage) c.SaveAs((outputFileName + "(").c_str());
    else c.SaveAs(outputFileName.c_str());

    firstPage = false;
  }

  c.Clear();
  c.SaveAs((outputFileName + ")").c_str());
}

void plotAllRuns(const PlotConfig& plotConfig, std::map<int, std::vector<std::shared_ptr<MonitorObject>>>& monitorObjectsInRateIntervals)
{
  int cW = 1800;
  int cH = 600;
  TCanvas c("c","c",cW,cH);
  c.SetRightMargin(0.3);

  std::string outputFileName = getPlotOutputFilePrefix(plotConfig) + ".pdf";

  bool firstPage = true;
  for (auto& [index, moVec] : monitorObjectsInRateIntervals) {
    if (moVec.empty()) continue;

    auto legend = new TLegend(0.75,0.1,0.95,0.9);

    int lineColor = 51;
    bool first = true;
    for (auto& mo : moVec) {
      TH1* histTemp = dynamic_cast<TH1*>(mo->getObject());
      //std::cout << "hist: " << hist << std::endl;
      if (!histTemp) continue;
      TH1* hist = (TH1*)histTemp->Clone("_clone");

      hist->Scale(1.0 / hist->Integral());

      hist->SetLineColor(lineColor);
      lineColor += 1;
      if (lineColor >= 100) lineColor = 51;

      if (first) {
        hist->SetTitle(TString::Format("%s [%0.1f kHz, %0.1f kHz]", hist->GetTitle(), rateIntervals[index].first, rateIntervals[index].second));
        hist->Draw(plotConfig.drawOptions.c_str());
      }
      else hist->Draw((plotConfig.drawOptions + " same").c_str());
      first = false;

      TDatime daTime;
      daTime.Set(mo->getValidity().getMin()/1000);
      int hourMin = daTime.GetHour();
      int minuteMin = daTime.GetMinute();
      daTime.Set(mo->getValidity().getMax()/1000);
      int hourMax = daTime.GetHour();
      int minuteMax = daTime.GetMinute();
      /*
      auto validityMin = getLocalTime(mo->getValidity().getMin(), "Europe/Paris");
      auto hourMin = getHour(validityMin);
      auto minuteMin = getMinute(validityMin);
      auto validityMax = getLocalTime(mo->getValidity().getMax(), "Europe/Paris");
      auto hourMax = getHour(validityMax);
      auto minuteMax = getMinute(validityMax);
      */
      legend->AddEntry(hist,TString::Format("%d [%02d:%02d - %02d:%02d]", mo->getActivity().mId, hourMin, minuteMin, hourMax, minuteMax),"l");
    }
    legend->Draw();


    if (firstPage) c.SaveAs((outputFileName + "(").c_str());
    else c.SaveAs(outputFileName.c_str());

    firstPage = false;
  }
  c.Clear();
  c.SaveAs((outputFileName + ")").c_str());
}

void plotReferenceComparisonForAllRuns(const PlotConfig& plotConfig, std::map<int, std::vector<std::shared_ptr<MonitorObject>>>& monitorObjectsInRateIntervals)
{
  double checkRangeMin = plotConfig.checkRangeMin;
  double checkRangeMax = plotConfig.checkRangeMax;
  double checkThreshold = plotConfig.checkThreshold;
  double chekMaxBadBinsFrac = plotConfig.maxBadBinsFrac;

  int cW = 1800;
  int cH = 600;
  TCanvas c("c","c",cW,cH);
  c.SetRightMargin(0.3);

  std::string outputFileName = getPlotOutputFilePrefix(plotConfig) + "-refcomp.pdf";

  bool firstPage = true;
  for (auto& [index, moVec] : monitorObjectsInRateIntervals) {
    if (moVec.empty()) continue;

    double referenceRate = rateIntervals[index].second;
    int refRunNumber = getReferenceRunForRate(referenceRate);
    std::cout << "TOTO index: " << index << "  rate: " << referenceRate << "  referenceRun: " << refRunNumber << std::endl;

    if (referencePlots.count(index) < 1) continue;
    auto referenceHist = referencePlots[index];
    referenceHist->Scale(1.0 / referenceHist->Integral());

    auto legend = new TLegend(0.75,0.1,0.95,0.9);

    int lineColor = 51;
    bool first = true;
    for (auto& mo : moVec) {
      const TH1* hist = dynamic_cast<TH1*>(mo->getObject());
      //std::cout << "hist: " << hist << std::endl;
      if (!hist) continue;

      TH1* histRatio = (TH1*)hist->Clone("_Ratio");

      histRatio->Scale(1.0 / histRatio->Integral());
      histRatio->Divide(referenceHist.get());

      histRatio->SetLineColor(lineColor);
      lineColor += 1;
      if (lineColor >= 100) lineColor = 51;

      if (first) {
        histRatio->SetTitle(TString::Format("%s [%0.1f kHz, %0.1f kHz]", hist->GetTitle(), rateIntervals[index].first, rateIntervals[index].second));
        histRatio->Draw("H");
        histRatio->SetMinimum(0.8);
        histRatio->SetMaximum(1.2);
      }
      else histRatio->Draw("H same");
      first = false;

      // check quality
      double nBinsChecked = 0;
      double nBinsBad = 0;
      for (int bin = 1; bin <= histRatio->GetXaxis()->GetNbins(); bin++) {
        double xBin = histRatio->GetXaxis()->GetBinCenter(bin);
        if (checkRangeMin != checkRangeMax) {
          if (xBin < checkRangeMin || xBin > checkRangeMax) {
            continue;
          }
        }

        nBinsChecked += 1;
        double ratio = histRatio->GetBinContent(bin);
        double deviation = std::fabs(ratio - 1.0);
        if (deviation > checkThreshold) {
          nBinsBad += 1;
        }
      }
      double fracBad = (nBinsChecked > 0) ? (nBinsBad / nBinsChecked) : 0;

      TDatime daTime;
      daTime.Set(mo->getValidity().getMin()/1000);
      int hourMin = daTime.GetHour();
      int minuteMin = daTime.GetMinute();
      daTime.Set(mo->getValidity().getMax()/1000);
      int hourMax = daTime.GetHour();
      int minuteMax = daTime.GetMinute();
      /*
      auto validityMin = getLocalTime(mo->getValidity().getMin(), "Europe/Paris");
      auto hourMin = getHour(validityMin);
      auto minuteMin = getMinute(validityMin);
      auto validityMax = getLocalTime(mo->getValidity().getMax(), "Europe/Paris");
      auto hourMax = getHour(validityMax);
      auto minuteMax = getMinute(validityMax);
      */
      if (fracBad > chekMaxBadBinsFrac) {
        std::cout << "Bad run: " << TString::Format("%d [%02d:%02d - %02d:%02d]", mo->getActivity().mId, hourMin, minuteMin, hourMax, minuteMax).Data() << std::endl;
      }

      TLegendEntry* lentry = legend->AddEntry(histRatio,TString::Format("%d [%02d:%02d - %02d:%02d]", mo->getActivity().mId, hourMin, minuteMin, hourMax, minuteMax),"l");
      if (mo->getActivity().mId == refRunNumber) {
        lentry->SetTextColor(kGreen + 2);
      }
      if (fracBad > chekMaxBadBinsFrac) {
        lentry->SetTextColor(kRed);
      }
    }
    legend->Draw();

    if (firstPage) c.SaveAs((outputFileName + "(").c_str());
    else c.SaveAs(outputFileName.c_str());

    firstPage = false;
  }
  c.Clear();
  c.SaveAs((outputFileName + ")").c_str());
}

double getNornalizationFactor(TH1* hist, double xmin, double xmax)
{
  if (xmin != xmax) {
    int binMin = hist->GetXaxis()->FindBin(xmin);
    int binMax = hist->GetXaxis()->FindBin(xmax);
    double integral = hist->Integral(binMin, binMax);
    return ((integral == 0) ? 1.0 : 1.0 / integral);
  } else {
    double integral = hist->Integral();
    return ((integral == 0) ? 1.0 : 1.0 / integral);
  }
}

void normalizeHistogram(TH1* hist, double xmin, double xmax)
{
  hist->Scale(getNornalizationFactor(hist, xmin, xmax));
}

void plotAllRunsWithRatios(const PlotConfig& plotConfig, std::map<int, std::vector<std::shared_ptr<MonitorObject>>>& monitorObjectsInRateIntervals)
{
  double checkRangeMin = plotConfig.checkRangeMin;
  double checkRangeMax = plotConfig.checkRangeMax;
  double checkThreshold = plotConfig.checkThreshold;
  double checkDeviationNsigma = plotConfig.checkDeviationNsigma;
  double chekMaxBadBinsFrac = plotConfig.maxBadBinsFrac;
  bool logx = plotConfig.logx;
  bool logy = plotConfig.logy;
  auto projection = plotConfig.projection;

  int cW = 1800;
  int cH = 1200;
  float labelSize = 0.025;
  double topBottomRatio = 1;
  double topSize = topBottomRatio / (topBottomRatio + 1.0);
  double bottomSize = 1.0 / (topBottomRatio + 1.0);

  Canvas canvas;
  canvas.canvas = std::make_shared<TCanvas>("c","c",cW,cH);

  canvas.padTop = std::make_shared<TPad>("pad_top", "Top Pad", 0, 0, 2.0 / 3.0, 1);
  canvas.padTop->SetBottomMargin(bottomSize);
  canvas.padTop->SetRightMargin(0);
  canvas.padTop->SetFillStyle(4000); // transparent
  canvas.canvas->cd();
  canvas.padTop->Draw();

  canvas.padBottom = std::make_shared<TPad>("pad_bottom", "Bottom Pad", 0, 0, 2.0 / 3.0, 1);
  canvas.padBottom->SetTopMargin(topSize * 1.0);
  canvas.padBottom->SetRightMargin(0);
  canvas.padBottom->SetFillStyle(4000); // transparent
  canvas.canvas->cd();
  canvas.padBottom->Draw();

  canvas.padRight = std::make_shared<TPad>("pad_right", "Right Pad", 2.0 / 3.0, 0, 1, 1);
  canvas.padRight->SetFillStyle(4000); // transparent
  canvas.canvas->cd();
  canvas.padRight->Draw();

  std::string outputFileName = getPlotOutputFilePrefix(plotConfig) + ".pdf";

  bool firstPage = true;
  for (auto& [index, moVec] : monitorObjectsInRateIntervals) {
    if (moVec.empty()) continue;

    canvas.padTop->Clear();
    canvas.padBottom->Clear();
    canvas.padRight->Clear();

    double referenceRate = rateIntervals[index].second;
    int refRunNumber = getReferenceRunForRate(referenceRate);
    std::cout << "TOTO index: " << index << "  rate: " << referenceRate << "  referenceRun: " << refRunNumber << std::endl;

    // fill histogram with average of all histograms in the current IR interval
    TH1* averageHist{ nullptr };
    for (auto& mo : moVec) {
      TH1* histTemp = dynamic_cast<TH1*>(mo->getObject());
      if (!histTemp) continue;
      // skip empty histograms for the averaging
      if (histTemp->GetEntries() == 0) continue;

      if (projection == "x") {
        TH2* h2 = dynamic_cast<TH2*>(histTemp);
        if (h2) {
          histTemp = (TH1*)h2->ProjectionX();
        }
      }
      if (projection == "y") {
        TH2* h2 = dynamic_cast<TH2*>(histTemp);
        if (h2) {
          histTemp = (TH1*)h2->ProjectionY();
        }
      }

      if (!averageHist) {
        averageHist = (TH1*)histTemp->Clone("_average");
        normalizeHistogram(averageHist, checkRangeMin, checkRangeMax);
      } else {
        averageHist->Add(histTemp, getNornalizationFactor(histTemp, checkRangeMin, checkRangeMax));
      }
    }


    // get pointer to the reference histogram, if available
    std::shared_ptr<TH1> referenceHist;
    if (referencePlots.count(index) > 0) {
      referenceHist = referencePlots[index];
    }

    TH1* denominatorHist = referenceHist ? referenceHist.get() : averageHist;
    normalizeHistogram(denominatorHist, checkRangeMin, checkRangeMax);

    auto legend = new TLegend(0.05,0.1,0.95,0.9);

    int lineColor = 51;
    bool first = true;
    int nBadPlots = 0;
    for (auto& mo : moVec) {
      TH1* histTemp = dynamic_cast<TH1*>(mo->getObject());
      //std::cout << "histTemp: " << histTemp << "  entries: " << histTemp->GetEntries() << std::endl;
      if (!histTemp) continue;

      // Convert TProfile plots into histograms to get correct errors for the ratios
      if (dynamic_cast<TProfile*>(histTemp)) {
        TProfile* hp = dynamic_cast<TProfile*>(histTemp);
        histTemp = hp->ProjectionX((std::string(histTemp->GetName()) + "_px").c_str());
      }

      if (projection == "x") {
        TH2* h2 = dynamic_cast<TH2*>(histTemp);
        if (h2) {
          histTemp = (TH1*)h2->ProjectionX();
        }
      }
      if (projection == "y") {
        TH2* h2 = dynamic_cast<TH2*>(histTemp);
        if (h2) {
          histTemp = (TH1*)h2->ProjectionY();
        }
      }

      canvas.padTop->cd();

      // log scales
      if (logx) {
        canvas.padTop->SetLogx(kTRUE);
      } else {
        canvas.padTop->SetLogx(kFALSE);
      }
      if (logy) {
        canvas.padTop->SetLogy(kTRUE);
      } else {
        canvas.padTop->SetLogy(kFALSE);
      }

      TH1* hist = (TH1*)histTemp->Clone("_clone");
      normalizeHistogram(hist, checkRangeMin, checkRangeMax);

      hist->GetXaxis()->SetLabelSize(0);
      hist->GetXaxis()->SetTitleSize(0);
      hist->GetYaxis()->SetLabelSize(labelSize);
      hist->GetYaxis()->SetTitleSize(labelSize);
      if (plotConfig.normalize) {
        hist->GetYaxis()->SetTitle("A.U.");
      }

      hist->SetLineColor(lineColor);

      // draw a transparent copy of the reference histogram to set the axes
      if (first) {
        denominatorHist->SetTitle(TString::Format("%s [%0.1f kHz, %0.1f kHz]", hist->GetTitle(), rateIntervals[index].first, rateIntervals[index].second));
        denominatorHist->SetLineColorAlpha(kBlack, 0.0);
        denominatorHist->SetMarkerColorAlpha(kBlack, 0.0);
        denominatorHist->SetMinimum(1.0e-6);
        denominatorHist->Draw("H");
      }

      hist->Draw((plotConfig.drawOptions + " same").c_str());

      double fracBad = 0;
      if (denominatorHist) {
        canvas.padBottom->cd();

        // log scale
        if (logx) {
          canvas.padBottom->SetLogx(kTRUE);
        } else {
          canvas.padBottom->SetLogx(kFALSE);
        }

        TH1* histRatio = (TH1*)histTemp->Clone("_ratio");

        TH1* histReference = (TH1*)denominatorHist->Clone("_clone");
        if (dynamic_cast<TProfile*>(histReference)) {
          TProfile* hp = dynamic_cast<TProfile*>(histReference);
          histReference = hp->ProjectionX((std::string(histReference->GetName()) + "_px").c_str());
        }

        if (projection == "x") {
          TH2* h2 = dynamic_cast<TH2*>(histReference);
          if (h2) {
            histReference = (TH1*)h2->ProjectionX();
          }
        }
        if (projection == "y") {
          TH2* h2 = dynamic_cast<TH2*>(histReference);
          if (h2) {
            histReference = (TH1*)h2->ProjectionY();
          }
        }

        normalizeHistogram(histRatio, checkRangeMin, checkRangeMax);
        normalizeHistogram(histReference, checkRangeMin, checkRangeMax);
        histRatio->Divide(histReference);
        histRatio->SetTitle("");
        histRatio->SetTitleSize(0);
        histRatio->GetXaxis()->SetLabelSize(labelSize);
        histRatio->GetXaxis()->SetTitleSize(labelSize);
        histRatio->GetYaxis()->SetTitle("ratio");
        histRatio->GetYaxis()->CenterTitle(kTRUE);
        histRatio->GetYaxis()->SetNdivisions(5);
        histRatio->GetYaxis()->SetLabelSize(labelSize);
        histRatio->GetYaxis()->SetTitleSize(labelSize);

        histRatio->SetLineColor(lineColor);

        if (first) {
          //histRatio->SetTitle(TString::Format("%s [%0.1f kHz, %0.1f kHz]", hist->GetTitle(), rateIntervals[index].first, rateIntervals[index].second));
          histRatio->Draw("H");
          histRatio->SetMinimum(0.8 + 1.0e-3);
          histRatio->SetMaximum(1.2 - 1.0e-3);
        }
        else histRatio->Draw("H same");

        // check quality
        double nBinsChecked = 0;
        double nBinsBad = 0;
        for (int bin = 1; bin <= histRatio->GetXaxis()->GetNbins(); bin++) {
          double xBin = histRatio->GetXaxis()->GetBinCenter(bin);
          if (checkRangeMin != checkRangeMax) {
            if (xBin < checkRangeMin || xBin > checkRangeMax) {
              continue;
            }
          }

          nBinsChecked += 1;
          double ratio = histRatio->GetBinContent(bin);
          double error = histRatio->GetBinError(bin);
          double deviation = std::fabs(ratio - 1.0);
          double threshold = checkThreshold + error * checkDeviationNsigma;
          if (mo->getActivity().mId == 5598020) {
            std::cout << "[TOTO]: bin=" << bin
                << "  ratio=" << ratio
                << "  error num=" << hist->GetBinError(bin)
                << "  den=" << histReference->GetBinError(bin)
                << "  ratio=" << error << std::endl;
          std::cout << "[TOTO]: bin=" << bin
              << "  deviation=" << deviation
              << "  threshold=" << threshold
              << std::endl;
          }
          if (deviation > threshold) {
            if (mo->getActivity().mId == 5598020) {
            std::cout << "[TOTO]: bad bin" << std::endl;
            }
            nBinsBad += 1;
          }
        }
        fracBad = (nBinsChecked > 0) ? (nBinsBad / nBinsChecked) : 0;
      }

      lineColor += 1;
      if (lineColor >= 100) lineColor = 51;

      first = false;

      TDatime daTime;
      daTime.Set(mo->getValidity().getMin()/1000);
      int hourMin = daTime.GetHour();
      int minuteMin = daTime.GetMinute();
      int secondMin = daTime.GetSecond();
      daTime.Set(mo->getValidity().getMax()/1000);
      int hourMax = daTime.GetHour();
      int minuteMax = daTime.GetMinute();
      int secondMax = daTime.GetSecond();
      /*
      auto validityMin = getLocalTime(mo->getValidity().getMin(), "Europe/Paris");
      auto hourMin = getHour(validityMin);
      auto minuteMin = getMinute(validityMin);
      auto secondMin = getSecond(validityMin);
      auto validityMax = getLocalTime(mo->getValidity().getMax(), "Europe/Paris");
      auto hourMax = getHour(validityMax);
      auto minuteMax = getMinute(validityMax);
      auto secondMax = getSecond(validityMax);
      */
      if (fracBad > chekMaxBadBinsFrac) {
        std::cout << "Bad time interval for plot \"" << plotConfig.plotName << "\": "
            << TString::Format("%d [%02d:%02d:%02d - %02d:%02d:%02d]", mo->getActivity().mId, hourMin, minuteMin, secondMin, hourMax, minuteMax, secondMax).Data()
            << TString::Format(" - IR: [%0.1f kHz, %0.1f kHz]", rateIntervals[index].first, rateIntervals[index].second)
            << std::endl;

        badTimeIntervals[mo->getActivity().mId][plotConfig.plotName].insert(std::make_pair<long, long>(mo->getValidity().getMin(), mo->getValidity().getMax()));

        nBadPlots += 1;
      }

      if (mo->getActivity().mId == refRunNumber) {
        TLegendEntry* lentry = legend->AddEntry(hist,TString::Format("%d [%02d:%02d - %02d:%02d]", mo->getActivity().mId, hourMin, minuteMin, hourMax, minuteMax),"l");
        lentry->SetTextColor(kGreen + 2);
      }
      if (fracBad > chekMaxBadBinsFrac) {
        TLegendEntry* lentry = legend->AddEntry(hist,TString::Format("%d [%02d:%02d - %02d:%02d]", mo->getActivity().mId, hourMin, minuteMin, hourMax, minuteMax),"l");
        lentry->SetTextColor(kRed);
      }
    }

    if (denominatorHist) {
      canvas.padBottom->cd();

      double lineXmin = (checkRangeMin != checkRangeMax) ? checkRangeMin : denominatorHist->GetXaxis()->GetXmin();
      double lineXmax = (checkRangeMin != checkRangeMax) ? checkRangeMax : denominatorHist->GetXaxis()->GetXmax();
      TLine* lineMin = new TLine(lineXmin, 1.0 - checkThreshold, lineXmax, 1.0 - checkThreshold);
      lineMin->SetLineColor(kRed);
      lineMin->SetLineStyle(7);
      lineMin->SetLineWidth(2);
      TLine* lineMax = new TLine(lineXmin, 1.0 + checkThreshold, lineXmax, 1.0 + checkThreshold);
      lineMax->SetLineColor(kRed);
      lineMax->SetLineStyle(7);
      lineMax->SetLineWidth(2);

      lineMin->Draw();
      lineMax->Draw();
    }

    canvas.padRight->cd();
    if (nBadPlots > 0) {
      legend->SetHeader("Bad time intervals:");
      TLegendEntry *header = (TLegendEntry*)legend->GetListOfPrimitives()->First();
      header->SetTextColor(kRed);
      header->SetTextSize(.08);
    } else {
      legend->SetHeader("All plots are GOOD", "C");
      TLegendEntry *header = (TLegendEntry*)legend->GetListOfPrimitives()->First();
      header->SetTextColor(kGreen + 2);
      header->SetTextSize(.08);
   }
    legend->Draw();

    if (firstPage) canvas.canvas->SaveAs((outputFileName + "(").c_str());
    else canvas.canvas->SaveAs(outputFileName.c_str());

    firstPage = false;
  }
  canvas.canvas->Clear();
  canvas.canvas->SaveAs((outputFileName + ")").c_str());

  std::cout << "\n\n==================\nDetailed report\n==================\n";
  for (auto& [run, plotMap] : badTimeIntervals) {
    if (plotMap.empty()) {
      continue;
    }
    std::cout << "\nRun " << run << std::endl;
    for (auto& [plotName, intervalVec] : plotMap) {
      std::cout << "  Bad time intervals for plot \"" << plotName << "\"\n";
      for (auto& [min, max] : intervalVec) {
        TDatime daTime;
        daTime.Set(min/1000);
        int hourMin = daTime.GetHour();
        int minuteMin = daTime.GetMinute();
        int secondMin = daTime.GetSecond();
        daTime.Set(max/1000);
        int hourMax = daTime.GetHour();
        int minuteMax = daTime.GetMinute();
        int secondMax = daTime.GetSecond();
        /*
        auto validityMin = getLocalTime(min, "Europe/Paris");
        auto hourMin = getHour(validityMin);
        auto minuteMin = getMinute(validityMin);
        auto secondMin = getSecond(validityMin);
        auto validityMax = getLocalTime(max, "Europe/Paris");
        auto hourMax = getHour(validityMax);
        auto minuteMax = getMinute(validityMax);
        auto secondMax = getSecond(validityMax);
        */
        std::cout << TString::Format("    %ld - %ld [%02d:%02d:%02d - %02d:%02d:%02d]\n", min, max, hourMin, minuteMin, secondMin, hourMax, minuteMax, secondMax).Data();
      }
    }
  }
}

void trendAllRuns(const PlotConfig& plotConfig, std::map<int, std::multimap<double, std::shared_ptr<MonitorObject>>>& monitorObjects)
{
  int cW = 1800;
  int cH = 1200;
  TCanvas c("c","c",cW,cH);
  c.SetRightMargin(0.2);

  std::string outputFileName = getPlotOutputFilePrefix(plotConfig) + "-trend.pdf";

  TMultiGraph graphs;

  auto legend = new TLegend(0.82,0.1,0.95,0.9);

  int lineColor = 51;
  for (auto& [run, moMap] : monitorObjects) {
    if (moMap.empty()) continue;

    std::vector<double> rates;
    std::vector<double> values;
    //int lineColor = 51;
    bool first = true;
    for (auto& [rate, mo] : moMap) {
      TH1* hist = dynamic_cast<TH1*>(mo->getObject());
      std::cout << "run: " << run << "  rate: " << rate << "  hist: " << hist << std::endl;
      if (!hist) continue;

      rates.push_back(rate);
      values.push_back(hist->GetMean());
    }

    TGraph* graphForRun = new TGraph(rates.size(), rates.data(), values.data());

    graphs.Add(graphForRun, "l");
    graphForRun->SetLineColor(lineColor);
    lineColor += 1;
    if (lineColor >= 100) lineColor = 51;

    legend->AddEntry(graphForRun,TString::Format("%d", run),"l");
  }

  //c.cd();
  graphs.Draw("AL PMC PLC PFC");
  graphs.SetTitle(TString::Format("%s vs. IR", plotConfig.plotLabel.c_str()));
  graphs.GetXaxis()->SetTitle("IR (kHz)");
  graphs.GetYaxis()->SetTitle(TString::Format("%s (mean)", plotConfig.plotLabel.c_str()));

  legend->Draw();
  c.SaveAs(outputFileName.c_str());
}

void printReport()
{
  std::cout << "\n\n==================\nSummary report\n==================\n\n";
  for (auto& [run, plotMap] : badTimeIntervals) {
    if (plotMap.empty()) {
      continue;
    }
   std::cout << "Run " << run << std::endl;
    // aggregate bad intervals for each plot separately
    std::map<std::string, std::vector<std::pair<long, long>>> aggregatedIntervalsPerPlot;
    std::vector<std::pair<long, long>> intervalsToBeAggregated;
    for (auto& [plotName, intervalVec] : plotMap) {
      for (auto& [min, max] : intervalVec) {
        if (aggregatedIntervalsPerPlot.count(plotName) <= 0) {
          aggregatedIntervalsPerPlot[plotName].push_back(std::make_pair(min, max));
        } else {
          long lastMax = aggregatedIntervalsPerPlot[plotName].back().second;
          if (min <= lastMax) {
            // if the current interval overlaps or is adjacent with the currently aggregated one, we extend the aggregated interval if needed
            if (max > lastMax) {
              aggregatedIntervalsPerPlot[plotName].back().second = max;
            }
          } else {
            // otherwise we initialize a new aggregated interval
            aggregatedIntervalsPerPlot[plotName].push_back(std::make_pair(min, max));
          }
        }
      }

      for (auto& [min, max] : aggregatedIntervalsPerPlot[plotName]) {
        intervalsToBeAggregated.push_back(std::make_pair(min, max));

        if (false) {
          TDatime daTime;
          daTime.Set(min/1000);
          int hourMin = daTime.GetHour();
          int minuteMin = daTime.GetMinute();
          int secondMin = daTime.GetSecond();
          daTime.Set(max/1000);
          int hourMax = daTime.GetHour();
          int minuteMax = daTime.GetMinute();
          int secondMax = daTime.GetSecond();
          /*
          auto validityMin = getLocalTime(min, "Europe/Paris");
          auto hourMin = getHour(validityMin);
          auto minuteMin = getMinute(validityMin);
          auto secondMin = getSecond(validityMin);
          auto validityMax = getLocalTime(max, "Europe/Paris");
          auto hourMax = getHour(validityMax);
          auto minuteMax = getMinute(validityMax);
          auto secondMax = getSecond(validityMax);
          */
          std::cout << TString::Format("  Bad aggregated interval %ld - %ld [%02d:%02d:%02d - %02d:%02d:%02d] for plot \"%s\"\n",
              min, max, hourMin, minuteMin, secondMin, hourMax, minuteMax, secondMax, plotName.c_str()).Data();
        }
      }
    }

    // sort all intervals in ascending order
    std::sort(intervalsToBeAggregated.begin(), intervalsToBeAggregated.end());
    // aggregate all intervals together
    std::set<std::pair<long, long>> aggregatedIntervals;
    std::pair<long, long> currentInterval{ -1, -1 };
    for (auto& [min, max] : intervalsToBeAggregated) {
      if (currentInterval.first < 0) {
        currentInterval.first = min;
        currentInterval.second = max;
        continue;
      }

      if (min <= currentInterval.second && max >= currentInterval.first) {
        // the intervals are overlapping, we update the limits if needed
        if (min < currentInterval.first) currentInterval.first = min;
        if (max > currentInterval.second) currentInterval.second = max;
      } else {
        // the new interval does not overlap with the current one
        // we insert the current in the set of intervals and we re-initialize it with the new interval
        aggregatedIntervals.insert(currentInterval);
        currentInterval.first = min;
        currentInterval.second = max;
      }
    }

    if (currentInterval.first >= 0) {
      // as the final step, add the current interval to the set as well
      aggregatedIntervals.insert(currentInterval);
    }

    for (auto& [min, max] : aggregatedIntervals) {
      TDatime daTime;
      daTime.Set(min/1000);
      int hourMin = daTime.GetHour();
      int minuteMin = daTime.GetMinute();
      int secondMin = daTime.GetSecond();
      daTime.Set(max/1000);
      int hourMax = daTime.GetHour();
      int minuteMax = daTime.GetMinute();
      int secondMax = daTime.GetSecond();
      /*
      auto validityMin = getLocalTime(min, "Europe/Paris");
      auto hourMin = getHour(validityMin);
      auto minuteMin = getMinute(validityMin);
      auto secondMin = getSecond(validityMin);
      auto validityMax = getLocalTime(max, "Europe/Paris");
      auto hourMax = getHour(validityMax);
      auto minuteMax = getMinute(validityMax);
      auto secondMax = getSecond(validityMax);
      */
      std::cout << TString::Format("  Bad aggregated interval %ld - %ld [%02d:%02d:%02d - %02d:%02d:%02d]\n",
          min, max, hourMin, minuteMin, secondMin, hourMax, minuteMax, secondMax).Data();
    }
  }
}

void aqc_process(const char* runsConfig, const char* plotsConfig)
{
  gStyle->SetOptStat(0);
  gStyle->SetOptFit(1111);
  gStyle->SetPalette(57, 0);
  gStyle->SetNumberContours(40);

  std::ifstream fRunsConfig(runsConfig);
  auto jRunsConfig = json::parse(fRunsConfig);

  std::ifstream fPlotsConfig(plotsConfig);
  auto jPlotsConfig = json::parse(fPlotsConfig);

  //boost::property_tree::ptree ptRuns;
  //boost::property_tree::read_json(runsConfig, ptRuns);

  //boost::property_tree::ptree ptPlots;
  //boost::property_tree::read_json(plotsConfig, ptPlots);

  //sessionID = ptPlots.get<std::string>("id");
  sessionID = jPlotsConfig.at("id").get<std::string>();
  std::cout << "ID: " << sessionID << std::endl;

  //year = ptRuns.get<std::string>("year");
  //period = ptRuns.get<std::string>("period");
  //pass = ptRuns.get<std::string>("pass");
  //beamType = ptRuns.get<std::string>("beamType");
  year = jRunsConfig.at("year").get<std::string>();
  period = jRunsConfig.at("period").get<std::string>();
  pass = jRunsConfig.at("pass").get<std::string>();
  beamType = jRunsConfig.at("beamType").get<std::string>();

  // input runs
  std::vector<int> inputRuns = jRunsConfig.at("runs");
  std::vector<int> runNumbers;
  for (const auto& inputRun : inputRuns) {
    runNumbers.push_back(inputRun);
  }
  /*auto inputRuns = ptRuns.get_child_optional("runs");
  if (inputRuns.has_value()) {
    std::cout << "inputRuns.size(): " << inputRuns.value().size() << std::endl;
    for (const auto& inputRun : inputRuns.value()) {
      runNumbers.push_back(inputRun.second.get_value<int>());
    }
  }*/

  // reference runs
  if (jRunsConfig.count("referenceRuns") > 0) {
    auto referenceRuns = jRunsConfig.at("referenceRuns");
    for (const auto& referenceRun : referenceRuns) {
      auto run = referenceRun.at("number").get<int>();
      double rateMax = referenceRun.at("rateMax").get<double>();
      std::cout << std::format("reference run {} valid up to {} kHz\n", run, rateMax);
      referenceRunsMap[rateMax] = run;
      runNumbers.push_back(run);
    }
  } else {
    std::cout << "Key \"" << "referenceRuns" << "\" not found in configuration" << std::endl;
  }

  /*auto referenceRuns = ptRuns.get_child_optional("referenceRuns");
  if (referenceRuns.has_value()) {
    std::cout << "referenceRuns.size(): " << referenceRuns.value().size() << std::endl;
    for (const auto& referenceRun : referenceRuns.value()) {
      int run = referenceRun.second.get<int>("number");
      double rateMax = referenceRun.second.get<double>("rateMax");
      referenceRunsMap[rateMax] = run;
      runNumbers.push_back(run);
    }
  } else {
    std::cout << "Key \"" << "referenceRuns" << "\" not found in configuration" << std::endl;
  }*/

  // loading of ROOT files
  std::vector<std::string> rootFileNames;
  std::vector<std::shared_ptr<TFile>> rootFiles;
  for (auto runNumber : runNumbers) {
    std::cout << "  run " << runNumber << std::endl;
    std::string inputFilePath = std::string("inputs/") + year + "/" + period + "/" + pass + "/"
        + std::to_string(runNumber) + "/";
    TSystemDirectory inputDir("", inputFilePath.c_str());
    //std::cout << "Listing contents of " << inputFilePath << std::endl;
    TList* inputFiles = inputDir.GetListOfFiles();
    if (!inputFiles) {
      std::cout << "Input ROOT file not found for run " << runNumber << ": \"" << inputFilePath << "\"" << std::endl;
      continue;
    }
    for (TObject* inputFile : (*inputFiles)) {
      TString fname = inputFile->GetName();
      if (fname.EndsWith(".root")) {
        auto fullPath = inputFilePath + fname.Data();
        std::cout << "Loading ROOT file " << fullPath << std::endl;
        rootFileNames.push_back(fullPath);
        rootFiles.push_back(std::make_shared<TFile>(fullPath.c_str()));
      }
    }
  }

  //return;

  // Plot configuration
  std::vector<PlotConfig> plotConfigsVector;

  if (jPlotsConfig.count("plots") > 0) {
    auto plotConfigs = jPlotsConfig.at("plots");
    std::cout << "plotConfigs.size(): " << plotConfigs.size() << std::endl;
    for (const auto& config : plotConfigs) {
      auto detectorName = config.at("detector").get<std::string>();
      auto taskName = config.at("task").get<std::string>();
      auto plotName = config.at("name").get<std::string>();
      std::cout << "New plot: \"" << detectorName << "/" << taskName << "/" << plotName << "\"" << std::endl;
      plotConfigsVector.push_back({ detectorName, taskName, plotName,
                        config.value("label", ""),
                        config.value("projection", ""),
                        config.value("drawOptions", "H"),
                        config.value("logx", false),
                        config.value("logy", false),
                        config.value("checkRangeMin", double(0.0)),
                        config.value("checkRangeMax", double(0.0)),
                        config.value("checkThreshold", double(0.1)),
                        config.value("checkDeviationNsigma", double(2.0)),
                        config.value("maxBadBinsFrac", double(0.1)),
                        config.value("normalize", true)
      });
    }
  } else {
    std::cout << "Key \"" << "plots" << "\" not found in configuration" << std::endl;
  }

  /*auto plotConfigsTree = ptPlots.get_child_optional("plots");
  if (plotConfigsTree.has_value()) {
    std::cout << "plotConfigsTree.size(): " << plotConfigsTree.value().size() << std::endl;
    for (const auto& plotConfig : plotConfigsTree.value()) {
      std::cout << "New plot: \"" << plotConfig.second.get<std::string>("detector") << "/" << plotConfig.second.get<std::string>("task") << "/" << plotConfig.second.get<std::string>("name") << "\"" << std::endl;
      plotConfigs.push_back({ plotConfig.second.get<std::string>("detector"),
                        plotConfig.second.get<std::string>("task"),
                        plotConfig.second.get<std::string>("name"),
                        plotConfig.second.get<std::string>("label", ""),
                        plotConfig.second.get<std::string>("projection", ""),
                        plotConfig.second.get<std::string>("drawOptions", "H"),
                        plotConfig.second.get<bool>("logx", 0),
                        plotConfig.second.get<bool>("logy", 0),
                        plotConfig.second.get<double>("checkRangeMin", 0),
                        plotConfig.second.get<double>("checkRangeMax", 0),
                        plotConfig.second.get<double>("checkThreshold", 0.1),
                        plotConfig.second.get<double>("maxBadBinsFrac", 0.1),
                        plotConfig.second.get<bool>("normalize", 1) });
    }
  } else {
    std::cout << "Key \"" << "plots" << "\" not found in configuration" << std::endl;
  }*/

  // Plot configuration
  std::vector<PlotConfig> trendConfigsVector;

  if (jPlotsConfig.count("trends") > 0) {
    auto trendConfigs = jPlotsConfig.at("trends");
    std::cout << "trendConfigs.size(): " << trendConfigs.size() << std::endl;
    for (const auto& config : trendConfigs) {
      auto detectorName = config.at("detector").get<std::string>();
      auto taskName = config.at("task").get<std::string>();
      auto plotName = config.at("name").get<std::string>();
      std::cout << "New plot: \"" << detectorName << "/" << taskName << "/" << plotName << "\"" << std::endl;
      trendConfigsVector.push_back({ detectorName, taskName, plotName,
                        config.value("label", ""),
                        config.value("projection", ""),
                        config.value("drawOptions", ""),
                        config.value("logx", false),
                        config.value("logy", false),
                        config.value("checkRangeMin", double(0.0)),
                        config.value("checkRangeMax", double(0.0)),
                        config.value("checkThreshold", double(0.1)),
                        config.value("checkDeviationNsigma", double(2.0)),
                        config.value("maxBadBinsFrac", double(0.1)),
                        config.value("normalize", true)
      });
    }
  } else {
    std::cout << "Key \"" << "trends" << "\" not found in configuration" << std::endl;
  }

  /*auto trendConfigsTree = ptPlots.get_child_optional("trends");
  if (trendConfigsTree.has_value()) {
    std::cout << "trendConfigsTree.size(): " << trendConfigsTree.value().size() << std::endl;
    for (const auto& trendConfig : trendConfigsTree.value()) {
      std::cout << "New trend: \"" << trendConfig.second.get<std::string>("detector") << "/" << trendConfig.second.get<std::string>("task") << "/" << trendConfig.second.get<std::string>("name") << "\"" << std::endl;
      trendConfigsVector.push_back({ trendConfig.second.get<std::string>("detector"),
                        trendConfig.second.get<std::string>("task"),
                        trendConfig.second.get<std::string>("name"),
                        trendConfig.second.get<std::string>("label"),
                        trendConfig.second.get<std::string>("projection", ""),
                        trendConfig.second.get<std::string>("drawOptions", ""),
                        trendConfig.second.get<bool>("logx", 0),
                        trendConfig.second.get<bool>("logy", 0),
                        trendConfig.second.get<double>("checkRangeMin", 0),
                        trendConfig.second.get<double>("checkRangeMax", 0),
                        trendConfig.second.get<double>("checkThreshold", 0.1),
                        trendConfig.second.get<double>("maxBadBinsFrac", 0.1) });
    }
  } else {
    std::cout << "Key \"" << "trends" << "\" not found in configuration" << std::endl;
  }*/

  //return;


  //std::array<std::string, 4> plotPathSplitted{ "mw", detectorName, taskName, plotName };
  //splitPlotPath(plotName, plotPathSplitted);

  auto& ccdbManager = o2::ccdb::BasicCCDBManager::instance();
  ccdbManager.setURL("https://alice-ccdb.cern.ch");

  double rateMax = 0;
  double rateMin = 0;
  double rateDelta = 0;
  if (beamType == "Pb-Pb") {
    rateMax = 50;
    rateMin = 5;
    rateDelta = 0.1;
    CTPScalerSourceName = "ZNC-hadronic";
  } else if (beamType == "pp") {
    rateMax = 1000;
    rateMin = 100;
    rateDelta = 0.1;
    CTPScalerSourceName = "T0VTX";
  }
  double rate = rateMax;
  while (rate > rateMin) {
    double rate2 = (1.0 - rateDelta) * rate;
    rateIntervals.emplace_back(std::make_pair(rate2, rate));
    rate = rate2;
  }

  for (const auto& plot : plotConfigsVector) {
    std::map<int, std::multimap<double, std::shared_ptr<Plot>>> plots;
    std::map<int, std::multimap<double, std::shared_ptr<MonitorObject>>> monitorObjects;
    std::map<int, std::vector<std::shared_ptr<MonitorObject>>> monitorObjectsInRateIntervals;

    loadPlotsFromRootFiles(rootFiles, plot, monitorObjects);
    //loadPlotsFromRootFiles(rootFileNames, plot, plots);

    populateRateIntervals(monitorObjects, monitorObjectsInRateIntervals);
    populateReferencePlots(monitorObjects);

    //for (auto& [runNumber, moMap] : monitorObjects) {
    //  plotRun(plot, runNumber, monitorObjectsInRateIntervals);
    //}

    plotAllRunsWithRatios(plot, monitorObjectsInRateIntervals);

    //plotReferenceComparisonForAllRuns(plot, monitorObjectsInRateIntervals);
  }

  for (const auto& plot : trendConfigsVector) {
    std::map<int, std::multimap<double, std::shared_ptr<MonitorObject>>> monitorObjects;
    std::map<int, std::vector<std::shared_ptr<MonitorObject>>> monitorObjectsInRateIntervals;

    loadPlotsFromRootFiles(rootFiles, plot, monitorObjects);
    populateRateIntervals(monitorObjects, monitorObjectsInRateIntervals);
    populateReferencePlots(monitorObjects);

    trendAllRuns(plot, monitorObjects);
  }

  printReport();
}
