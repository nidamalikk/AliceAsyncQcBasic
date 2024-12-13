#include <QualityControl/MonitorObject.h>
#include <QualityControl/MonitorObjectCollection.h>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string>

//#include <DataFormatsCTP/CTPRateFetcher.h>
#include "./CTPRateFetcher.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

using namespace o2::quality_control::core;

std::string sessionID;

//std::string CTPScalerSourceName{ "T0VTX" };
std::string CTPScalerSourceName{ "ZNC-hadronic" };

std::map<int, std::shared_ptr<o2::ctp::CTPRateFetcher>> ctpRateFatchers;

std::vector<std::pair<double, double>> rateIntervals;

//std::vector<std::pair<int, double>> referenceRunsMap{ {560034, 29}, {560033, 50} };
std::map<double, int> referenceRunsMap; //{ {15, 560070}, {29, 560034}, {40, 560033}, {50, 560031} };

std::map<int, std::shared_ptr<TH1>> referencePlots;

using namespace o2::quality_control::core;

struct PlotConfig
{
  std::string detectorName;
  std::string taskName;
  std::string plotName;
  std::string drawOptions;
  double checkRangeMin;
  double checkRangeMax;
  double checkThreshold;
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
  std::string outputFileName = std::string("outputs/") + sessionID + "/" + plotConfig.detectorName + "-" + plotConfig.taskName + "-" + plotNameWithDashes;

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
  dir = GetDir(dir, plotConfig.detectorName.c_str());
  dir = GetDir(dir, plotConfig.taskName.c_str());
  auto listOfKeys = dir->GetListOfKeys();
  for (int i = 0 ; i < listOfKeys->GetEntries(); ++i) {
    //std::cout<< "i: " << i << "  " << listOfKeys->At(i)->GetName() << std::endl;
    auto* moc = dynamic_cast<o2::quality_control::core::MonitorObjectCollection*>(dir->Get(listOfKeys->At(i)->GetName()));
    if (!moc) continue;
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
    //TFile* f = new TFile(rootFile.c_str());
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

void loadPlotsFromRootFiles(std::vector<std::string>& rootFileNames, const PlotConfig& plotConfig,
    std::map<int, std::multimap<double, std::shared_ptr<Plot>>>& plots)
{

  auto& ccdbManager = o2::ccdb::BasicCCDBManager::instance();

  for (auto rootFileName : rootFileNames) {
    std::cout << "Loading plot \"" << plotConfig.plotName << "\" from file " << rootFileName << std::endl;
    //std::shared_ptr<TFile> f = std::make_shared<TFile>(rootFileName.c_str());
    TFile* f = new TFile(rootFileName.c_str());
    auto moVector = GetMOMW(f, plotConfig);

    for (auto& mo : moVector) {
      int runNumber = mo->getActivity().mId;
      auto timestamp = mo->getValidity().getMax(); //(mo->getValidity().getMax() + mo->getValidity().getMin()) / 2;

      TH1* hist = dynamic_cast<TH1*>(mo->getObject());
      if (!hist) continue;

      std::cout << "Loaded MO from file " << rootFileName
          << " and validity " << mo->getValidity().getMin()
          << " -> " << mo->getValidity().getMax() << std::endl;

      // check if a MO with the same validity was already loaded, in which case we add the
      // current one instead of adding a new entry in the map
      bool histAdded = false;
      if (false && plots.count(runNumber) > 0) {
        for (auto& [rate, plotFromMap] : plots[runNumber]) {
          if ( plotFromMap->validity == mo->getValidity()) {
            //plotFromMap->histogram->Add(hist);
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

      TH1* histClone = (TH1*)hist->Clone(TString::Format("%s_%d_%lu", hist->GetName(), mo->getActivity().mId, mo->getValidity().getMax()));
      histClone->SetDirectory(nullptr);
      std::shared_ptr<Plot> plot = std::make_shared<Plot>(histClone, mo->getActivity(), mo->getValidity());

      //std::shared_ptr<TH1> plotHistogram;
      //Plot plot{ std::shared_ptr<TH1>(), mo->getActivity(), mo->getValidity() };
      //plot.histogram.reset((TH1*)hist->Clone(TString::Format("%s_%d_%lu", hist->GetName(), mo->getActivity().mId, mo->getValidity().getMax())));

      plots[runNumber].insert({rate, plot});
    }

    std::cout << "Before deleting TFile" << std::endl;
    delete f;
    std::cout << "After deleting TFile" << std::endl;
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

void plotAllRunsWithRatios(const PlotConfig& plotConfig, std::map<int, std::vector<std::shared_ptr<MonitorObject>>>& monitorObjectsInRateIntervals)
{
  double checkRangeMin = plotConfig.checkRangeMin;
  double checkRangeMax = plotConfig.checkRangeMax;
  double checkThreshold = plotConfig.checkThreshold;
  double chekMaxBadBinsFrac = plotConfig.maxBadBinsFrac;

  int cW = 1800;
  int cH = 1200;
  //TCanvas c("c","c",cW,cH);
  //c.SetRightMargin(0.3);

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

    std::shared_ptr<TH1> referenceHist;
    if (referencePlots.count(index) > 0) {
      referenceHist = referencePlots[index];
      if (checkRangeMin != checkRangeMax) {
        int binMin = referenceHist->GetXaxis()->FindBin(checkRangeMin);
        int binMax = referenceHist->GetXaxis()->FindBin(checkRangeMax);
        referenceHist->Scale(1.0 / referenceHist->Integral(binMin, binMax));
      } else {
        referenceHist->Scale(1.0 / referenceHist->Integral());
      }
    }

    auto legend = new TLegend(0.05,0.1,0.95,0.9);

    int lineColor = 51;
    bool first = true;
    for (auto& mo : moVec) {
      TH1* histTemp = dynamic_cast<TH1*>(mo->getObject());
      //std::cout << "hist: " << hist << std::endl;
      if (!histTemp) continue;

      canvas.padTop->cd();
      TH1* hist = (TH1*)histTemp->Clone("_clone");

      if (checkRangeMin != checkRangeMax) {
        int binMin = hist->GetXaxis()->FindBin(checkRangeMin);
        int binMax = hist->GetXaxis()->FindBin(checkRangeMax);
        hist->Scale(1.0 / hist->Integral(binMin, binMax));
      } else {
        hist->Scale(1.0 / hist->Integral());
      }
      hist->SetMinimum(1.0e-6);

      hist->GetXaxis()->SetLabelSize(0);
      hist->GetXaxis()->SetTitleSize(0);
      hist->GetYaxis()->SetLabelSize(labelSize);
      hist->GetYaxis()->SetTitleSize(labelSize);
      if (plotConfig.normalize) {
        hist->GetYaxis()->SetTitle("A.U.");
      }

      hist->SetLineColor(lineColor);

      if (first) {
        hist->SetTitle(TString::Format("%s [%0.1f kHz, %0.1f kHz]", hist->GetTitle(), rateIntervals[index].first, rateIntervals[index].second));
        hist->Draw(plotConfig.drawOptions.c_str());
      }
      else hist->Draw((plotConfig.drawOptions + " same").c_str());

      double fracBad = 0;
      if (referenceHist) {
        canvas.padBottom->cd();
        TH1* histRatio = (TH1*)histTemp->Clone("_ratio");

        if (checkRangeMin != checkRangeMax) {
          int binMin = histRatio->GetXaxis()->FindBin(checkRangeMin);
          int binMax = histRatio->GetXaxis()->FindBin(checkRangeMax);
          histRatio->Scale(1.0 / histRatio->Integral(binMin, binMax));
        } else {
          histRatio->Scale(1.0 / histRatio->Integral());
        }
        histRatio->Divide(referenceHist.get());

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
          double deviation = std::fabs(ratio - 1.0);
          if (deviation > checkThreshold) {
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

      if (fracBad > chekMaxBadBinsFrac) {
        std::cout << "Bad time interval for plot \"" << plotConfig.plotName << "\": " << TString::Format("%d [%02d:%02d:%02d - %02d:%02d:%02d]", mo->getActivity().mId, hourMin, minuteMin, secondMin, hourMax, minuteMax, secondMax).Data() << std::endl;
      }

      TLegendEntry* lentry = legend->AddEntry(hist,TString::Format("%d [%02d:%02d - %02d:%02d]", mo->getActivity().mId, hourMin, minuteMin, hourMax, minuteMax),"l");
      if (mo->getActivity().mId == refRunNumber) {
        lentry->SetTextColor(kGreen + 2);
      }
      if (fracBad > chekMaxBadBinsFrac) {
        lentry->SetTextColor(kRed);
      }

      //legend->AddEntry(hist,TString::Format("%d [%02d:%02d - %02d:%02d]", mo->getActivity().mId, hourMin, minuteMin, hourMax, minuteMax),"l");
    }

    if (referenceHist) {
      canvas.padBottom->cd();

      double lineXmin = (checkRangeMin != checkRangeMax) ? checkRangeMin : referenceHist->GetXaxis()->GetXmin();
      double lineXmax = (checkRangeMin != checkRangeMax) ? checkRangeMax : referenceHist->GetXaxis()->GetXmax();
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
    legend->Draw();

    if (firstPage) canvas.canvas->SaveAs((outputFileName + "(").c_str());
    else canvas.canvas->SaveAs(outputFileName.c_str());

    firstPage = false;
  }
  canvas.canvas->Clear();
  canvas.canvas->SaveAs((outputFileName + ")").c_str());
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

  bool firstGraph = true;
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

    /*if (firstGraph) {
      graphForRun->Draw("al");
    } else {
      graphForRun->Draw("l same");
    }*/
    firstGraph = false;

    legend->AddEntry(graphForRun,TString::Format("%d", run),"l");
  }

  //c.cd();
  graphs.Draw("AL PMC PLC PFC");
  graphs.SetTitle("ROF size vs. IR");
  graphs.GetXaxis()->SetTitle("IR (kHz)");
  graphs.GetYaxis()->SetTitle("ROF Size (mean)");

  legend->Draw();
  c.SaveAs(outputFileName.c_str());
}

void aqc_process(const char* rootFileList, const char* config)
{
  gStyle->SetOptStat(0);
  gStyle->SetOptFit(1111);
  gStyle->SetPalette(57, 0);
  gStyle->SetNumberContours(40);

  boost::property_tree::ptree jsontree;
  boost::property_tree::read_json(config, jsontree);

  sessionID = jsontree.get<std::string>("id");
  std::cout << "ID: " << sessionID << std::endl;

  // reference runs
  auto referenceRuns = jsontree.get_child_optional("referenceRuns");
  if (referenceRuns.has_value()) {
    std::cout << "referenceRuns.size(): " << referenceRuns.value().size() << std::endl;
    for (const auto& referenceRun : referenceRuns.value()) {
      int run = referenceRun.second.get<int>("number");
      double rateMax = referenceRun.second.get<double>("rateMax");
      referenceRunsMap[rateMax] = run;
    }
  } else {
    std::cout << "Key \"" << "referenceRuns" << "\" not found in configuration" << std::endl;
  }


  // Plot configuration
  std::vector<PlotConfig> plotConfigs;
  auto plotConfigsTree = jsontree.get_child_optional("plots");
  if (plotConfigsTree.has_value()) {
    std::cout << "plotConfigsTree.size(): " << plotConfigsTree.value().size() << std::endl;
    for (const auto& plotConfig : plotConfigsTree.value()) {
      std::cout << "New plot: \"" << plotConfig.second.get<std::string>("detector") << "/" << plotConfig.second.get<std::string>("task") << "/" << plotConfig.second.get<std::string>("name") << "\"" << std::endl;
      plotConfigs.push_back({ plotConfig.second.get<std::string>("detector"),
                        plotConfig.second.get<std::string>("task"),
                        plotConfig.second.get<std::string>("name"),
                        plotConfig.second.get<std::string>("drawOptions", "H"),
                        plotConfig.second.get<double>("checkRangeMin", 0),
                        plotConfig.second.get<double>("checkRangeMax", 0),
                        plotConfig.second.get<double>("checkThreshold", 0.1),
                        plotConfig.second.get<double>("maxBadBinsFrac", 0.1),
                        plotConfig.second.get<bool>("normalize", 1) });
    }
  } else {
    std::cout << "Key \"" << "plots" << "\" not found in configuration" << std::endl;
  }

  // Plot configuration
  std::vector<PlotConfig> trendConfigs;
  auto trendConfigsTree = jsontree.get_child_optional("trends");
  if (trendConfigsTree.has_value()) {
    std::cout << "trendConfigsTree.size(): " << trendConfigsTree.value().size() << std::endl;
    for (const auto& trendConfig : trendConfigsTree.value()) {
      std::cout << "New trend: \"" << trendConfig.second.get<std::string>("detector") << "/" << trendConfig.second.get<std::string>("task") << "/" << trendConfig.second.get<std::string>("name") << "\"" << std::endl;
      trendConfigs.push_back({ trendConfig.second.get<std::string>("detector"),
                        trendConfig.second.get<std::string>("task"),
                        trendConfig.second.get<std::string>("name"),
                        trendConfig.second.get<std::string>("drawOptions", ""),
                        trendConfig.second.get<double>("checkRangeMin", 0),
                        trendConfig.second.get<double>("checkRangeMax", 0),
                        trendConfig.second.get<double>("checkThreshold", 0.1),
                        trendConfig.second.get<double>("maxBadBinsFrac", 0.1) });
    }
  } else {
    std::cout << "Key \"" << "trends" << "\" not found in configuration" << std::endl;
  }



  std::ifstream infile(rootFileList);
  std::string rootFilePath;
  std::vector<std::string> rootFileNames;
  std::vector<std::shared_ptr<TFile>> rootFiles;
  while (infile >> rootFilePath)
  {
    rootFileNames.push_back(rootFilePath);
    rootFiles.push_back(std::make_shared<TFile>(rootFilePath.c_str()));
  }

  //std::array<std::string, 4> plotPathSplitted{ "mw", detectorName, taskName, plotName };
  //splitPlotPath(plotName, plotPathSplitted);

  auto& ccdbManager = o2::ccdb::BasicCCDBManager::instance();
  ccdbManager.setURL("https://alice-ccdb.cern.ch");

  double rateMax = 50;
  double rateMin = 5;
  double rateDelta = 0.1;
  double rate = rateMax;
  while (rate > rateMin) {
    double rate2 = (1.0 - rateDelta) * rate;
    rateIntervals.emplace_back(std::make_pair(rate2, rate));
    rate = rate2;
  }

  for (const auto& plot : plotConfigs) {
    std::map<int, std::multimap<double, std::shared_ptr<Plot>>> plots;
    std::map<int, std::multimap<double, std::shared_ptr<MonitorObject>>> monitorObjects;
    std::map<int, std::vector<std::shared_ptr<MonitorObject>>> monitorObjectsInRateIntervals;

    loadPlotsFromRootFiles(rootFiles, plot, monitorObjects);
    //loadPlotsFromRootFiles(rootFileNames, plot, plots);

    populateRateIntervals(monitorObjects, monitorObjectsInRateIntervals);
    populateReferencePlots(monitorObjects);

    for (auto& [runNumber, moMap] : monitorObjects) {
      plotRun(plot, runNumber, monitorObjectsInRateIntervals);
    }

    plotAllRunsWithRatios(plot, monitorObjectsInRateIntervals);

    plotReferenceComparisonForAllRuns(plot, monitorObjectsInRateIntervals);
  }

  for (const auto& plot : trendConfigs) {
    std::map<int, std::multimap<double, std::shared_ptr<MonitorObject>>> monitorObjects;
    std::map<int, std::vector<std::shared_ptr<MonitorObject>>> monitorObjectsInRateIntervals;

    loadPlotsFromRootFiles(rootFiles, plot, monitorObjects);
    populateRateIntervals(monitorObjects, monitorObjectsInRateIntervals);
    populateReferencePlots(monitorObjects);

    trendAllRuns(plot, monitorObjects);
  }
}
