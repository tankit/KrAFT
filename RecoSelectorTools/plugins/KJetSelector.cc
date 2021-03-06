#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/EDFilter.h"

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "DataFormats/Candidate/interface/Candidate.h"
#include "DataFormats/PatCandidates/interface/Jet.h"
#include "DataFormats/PatCandidates/interface/MET.h"

#include "DataFormats/Common/interface/View.h"

#include "CondFormats/JetMETObjects/interface/FactorizedJetCorrector.h"
#include "CondFormats/JetMETObjects/interface/JetCorrectionUncertainty.h"
#include "PhysicsTools/SelectorUtils/interface/PFJetIDSelectionFunctor.h"
//#include "CommonTools/UtilAlgos/interface/StringCutObjectSelector.h"
#include "CommonTools/Utils/interface/PtComparator.h"
#include "DataFormats/Math/interface/deltaR.h"

#include "KrAFT/GeneratorTools/interface/Types.h"

#include <memory>

#include "FWCore/ServiceRegistry/interface/Service.h"
#include "CommonTools/UtilAlgos/interface/TFileService.h"
#include <TH1F.h>
#include <TH2F.h>

using namespace edm;
using namespace std;

class KJetSelector : public edm::EDFilter
{
public:
  KJetSelector(const edm::ParameterSet& pset);
  ~KJetSelector() {};

private:
  void beginJob() {};
  bool filter(edm::Event& event, const edm::EventSetup& eventSetup);
  void endJob() {};

  double overlapDeltaR_;
  unsigned int minNumber_;
  unsigned int maxNumber_;

  edm::InputTag jetLabel_, metLabel_;
  std::vector<edm::InputTag> overlapCandLabels_;

  JetCorrectionUncertainty *jecUncCalculator_;
  PFJetIDSelectionFunctor* isGoodJet_;
  double minPt_, maxEta_;

  int cleanMethod_;

  bool isMC_;

private:
  void getJERFactor(const double jetEta, double& cJER, double& cJERUp, double& cJERDn)
  {
    if      ( jetEta < 0.5 ) { cJER = 1.052; cJERUp = 1.115; cJERDn = 0.990; }
    else if ( jetEta < 1.1 ) { cJER = 1.057; cJERUp = 1.114; cJERDn = 1.001; }
    else if ( jetEta < 1.7 ) { cJER = 1.096; cJERUp = 1.161; cJERDn = 1.032; }
    else if ( jetEta < 2.3 ) { cJER = 1.134; cJERUp = 1.228; cJERDn = 1.042; }
    else if ( jetEta < 5.0 ) { cJER = 1.288; cJERUp = 1.488; cJERDn = 1.089; }
    else { cJER = cJERUp = cJERDn = 1; }
  }
  bool isInAcceptance(const pat::Jet& jetP4)
  {
    if ( jetP4.pt() < minPt_ ) return false;
    if ( std::abs(jetP4.eta()) > maxEta_ ) return false;

    return true;
  }

};

KJetSelector::KJetSelector(const edm::ParameterSet& pset)
{
  isMC_ = pset.getParameter<bool>("isMC");

  jetLabel_ = pset.getParameter<edm::InputTag>("jet");
  metLabel_ = pset.getParameter<edm::InputTag>("met");

  // Selection cuts
  edm::ParameterSet selectionPSet = pset.getParameter<edm::ParameterSet>("selection");
  isGoodJet_ = new PFJetIDSelectionFunctor(selectionPSet.getParameter<edm::ParameterSet>("jetId"));
  minPt_ = selectionPSet.getParameter<double>("minPt");
  maxEta_ = selectionPSet.getParameter<double>("maxEta");

  // Cleaning
  edm::ParameterSet cleanPSet = pset.getParameter<edm::ParameterSet>("cleaning");
  overlapDeltaR_ = cleanPSet.getParameter<double>("overlapDeltaR");
  overlapCandLabels_ = cleanPSet.getParameter<std::vector<edm::InputTag> >("overlapCands");
  const std::string cleanMethodName = cleanPSet.getParameter<std::string>("cleanMethod");
  // Cleaning methods:
  //  subtract    =  1: Check acceptance cut after lepton p4 subtraction and store subtacted jet
  //  subtractAll =  2: Check acceptance cut after lepton p4 subtraction but keep original 4 momentum
  //  cleanAll    =  0: Jet cleaning by deltaR
  //  Default     = -1: No jet cleaning
  if ( cleanMethodName == "subtract" ) cleanMethod_ = 2; 
  else if ( cleanMethodName == "subtractAll" ) cleanMethod_ = 1; 
  else if ( cleanMethodName == "cleanAll" ) cleanMethod_ = 0; 
  else cleanMethod_ = -1; 
  if ( overlapCandLabels_.empty() ) cleanMethod_ = -1;

  minNumber_ = pset.getParameter<unsigned int>("minNumber");
  maxNumber_ = pset.getParameter<unsigned int>("maxNumber");

  // JEC correction
  edm::FileInPath jecFilePathRD(pset.getParameter<string>("jecFileRD"));
  edm::FileInPath jecFilePathMC(pset.getParameter<string>("jecFileMC"));
  if ( isMC_ ) jecUncCalculator_ = new JetCorrectionUncertainty(jecFilePathMC.fullPath());
  else jecUncCalculator_ = new JetCorrectionUncertainty(jecFilePathRD.fullPath());

  produces<std::vector<pat::Jet> >();
  produces<std::vector<pat::Jet> >("up");
  produces<std::vector<pat::Jet> >("dn");
  produces<std::vector<pat::MET> >();
  produces<std::vector<pat::MET> >("up");
  produces<std::vector<pat::MET> >("dn");
  if ( isMC_ )
  {
    produces<std::vector<pat::Jet> >("resUp");
    produces<std::vector<pat::Jet> >("resDn");
    produces<std::vector<pat::MET> >("resUp");
    produces<std::vector<pat::MET> >("resDn");
  }
}

bool KJetSelector::filter(edm::Event& event, const edm::EventSetup& eventSetup)
{
  edm::Handle<std::vector<pat::Jet> > jetHandle;
  event.getByLabel(jetLabel_, jetHandle);

  edm::Handle<std::vector<pat::MET> > metHandle;
  event.getByLabel(metLabel_, metHandle);

  std::auto_ptr<std::vector<pat::Jet> > corrJets(new std::vector<pat::Jet>());
  std::auto_ptr<std::vector<pat::Jet> > corrJetsUp(new std::vector<pat::Jet>());
  std::auto_ptr<std::vector<pat::Jet> > corrJetsDn(new std::vector<pat::Jet>());
  std::auto_ptr<std::vector<pat::Jet> > corrJetsResUp(new std::vector<pat::Jet>());
  std::auto_ptr<std::vector<pat::Jet> > corrJetsResDn(new std::vector<pat::Jet>());

  std::auto_ptr<std::vector<pat::MET> > corrMets(new std::vector<pat::MET>());
  std::auto_ptr<std::vector<pat::MET> > corrMetsUp(new std::vector<pat::MET>());
  std::auto_ptr<std::vector<pat::MET> > corrMetsDn(new std::vector<pat::MET>());
  std::auto_ptr<std::vector<pat::MET> > corrMetsResUp(new std::vector<pat::MET>());
  std::auto_ptr<std::vector<pat::MET> > corrMetsResDn(new std::vector<pat::MET>());

  pat::MET met = metHandle->at(0);
  double metX = met.px(), metY = met.py();
  double metUpX = metX, metUpY = metY;
  double metDnX = metX, metDnY = metY;
  double metResUpX = metX, metResUpY = metY;
  double metResDnX = metX, metResDnY = metY;

  std::vector<const reco::Candidate*> overlapCands;
  if ( cleanMethod_ != -1 )
  {
    for ( int iLabel=0, nLabel=overlapCandLabels_.size(); iLabel<nLabel; ++iLabel )
    {
      edm::Handle<edm::View<reco::Candidate> > overlapCandHandle;
      event.getByLabel(overlapCandLabels_.at(iLabel), overlapCandHandle);

      //if ( !overlapCandHandle.isValid() ) continue;

      for ( int i=0, n=overlapCandHandle->size(); i<n; ++i )
      {
        overlapCands.push_back(&(overlapCandHandle->at(i)));
      }
    }
  }

  for ( int i=0, n=jetHandle->size(); i<n; ++i )
  {
    pat::Jet jet = jetHandle->at(i);
    if ( !(*isGoodJet_)(jet) ) continue;

    reco::Candidate::LorentzVector jetP4 = jet.p4();

    // JES and uncertainties
    pat::Jet jetUp = jet, jetDn = jet;
    jecUncCalculator_->setJetPt(jetP4.pt());
    jecUncCalculator_->setJetEta(jetP4.eta());
    const double jecUncUp = jecUncCalculator_->getUncertainty(true);
    jecUncCalculator_->setJetPt(jetP4.pt());
    jecUncCalculator_->setJetEta(jetP4.eta());
    const double jecUncDn = jecUncCalculator_->getUncertainty(false);

    math::XYZTLorentzVector jetUpP4 = jetP4*(1+jecUncUp);
    math::XYZTLorentzVector jetDnP4 = jetP4*(1-jecUncDn);

    metUpX += jetP4.px() - jetUpP4.px();
    metUpY += jetP4.py() - jetUpP4.py();
    metDnX += jetP4.px() - jetDnP4.px();
    metDnY += jetP4.py() - jetDnP4.py();

    // JER and uncertainties
    pat::Jet jetResUp = jet, jetResDn = jet;
    math::XYZTLorentzVector jetResUpP4, jetResDnP4;
    double ptScale = 1, ptScaleUp = 1, ptScaleDn = 1;
    if ( isMC_ )
    {
      const reco::GenJet* genJet = jet.genJet();
      if ( genJet and genJet->pt() > 10 )
      {
        const math::XYZTLorentzVector& rawJetP4 = jet.correctedP4(0);

        const double genJetPt = genJet->pt();
        const double jetPt = jetP4.pt();
        const double dPt = jetPt-genJetPt;

        const double jetEta = std::abs(jet.eta());
        double cJER, cJERUp, cJERDn;
        getJERFactor(jetEta, cJER, cJERUp, cJERDn);

        ptScale   = max(1e-9, (genJetPt+dPt*cJER  )/jetPt);
        ptScaleUp = max(1e-9, (genJetPt+dPt*cJERUp)/jetPt);
        ptScaleDn = max(1e-9, (genJetPt+dPt*cJERDn)/jetPt);

        const double metDx   = rawJetP4.px()*(1-ptScale  );
        const double metDxUp = rawJetP4.px()*(1-ptScaleUp);
        const double metDxDn = rawJetP4.px()*(1-ptScaleDn);

        const double metDy   = rawJetP4.py()*(1-ptScale  );
        const double metDyUp = rawJetP4.py()*(1-ptScaleUp);
        const double metDyDn = rawJetP4.py()*(1-ptScaleDn);

        // Correct Jet
        jetResUp.setP4(jetP4*ptScaleUp);
        jetResDn.setP4(jetP4*ptScaleDn);
        jetP4   *= ptScale;
        jetUpP4 *= ptScale;
        jetDnP4 *= ptScale;

        // Correct MET
        metX += metDx;
        metY += metDy;
        metResUpX += metDxUp;
        metResUpY += metDyUp;
        metResDnX += metDxDn;
        metResDnY += metDyDn;
      }
    }

    jet.setP4(jetP4);
    jetUp.setP4(jetUpP4);
    jetDn.setP4(jetDnP4);

    bool isOverlap = false;
    for ( int j=0, m=overlapCands.size(); j<m; ++j )
    {
      if ( deltaR(jet.p4(), overlapCands.at(j)->p4()) < overlapDeltaR_ )
      {
        isOverlap = true;
        if ( cleanMethod_ == 0 ) break;
        else if ( cleanMethod_ == 1 or cleanMethod_ == 2 )
        {
          jetP4 -= overlapCands.at(j)->p4();
          jetUpP4 -= overlapCands.at(j)->p4();
          jetDnP4 -= overlapCands.at(j)->p4();
          if ( isMC_ )
          {
            jetResUpP4 -= overlapCands.at(j)->p4();
            jetResDnP4 -= overlapCands.at(j)->p4();
          }
        }
      }
    }

    if ( isOverlap )
    {
      if ( cleanMethod_ == 0 ) continue;
      else
      {
        if ( cleanMethod_ == 1 )
        {
          jet.setP4(jetP4);
          jetUp.setP4(jetUpP4);
          jetDn.setP4(jetDnP4);
          if ( isMC_ )
          {
            jetResUp.setP4(jetResUpP4);
            jetResDn.setP4(jetResDnP4);
          }
        }
        if ( isInAcceptance(jet) ) corrJets->push_back(jet);
        if ( isInAcceptance(jetUp) ) corrJetsUp->push_back(jetUp);
        if ( isInAcceptance(jetDn) ) corrJetsDn->push_back(jetDn);
        if ( isMC_ )
        {
          if ( isInAcceptance(jetResUp) ) corrJetsResUp->push_back(jetResUp);
          if ( isInAcceptance(jetResDn) ) corrJetsResDn->push_back(jetResDn);
        }
      }
    }
    else 
    {
      if ( isInAcceptance(jet) ) corrJets->push_back(jet);
      if ( isInAcceptance(jetUp) ) corrJetsUp->push_back(jetUp);
      if ( isInAcceptance(jetDn) ) corrJetsDn->push_back(jetDn);
      if ( isMC_ )
      {
        if ( isInAcceptance(jetResUp) ) corrJetsResUp->push_back(jetResUp);
        if ( isInAcceptance(jetResDn) ) corrJetsResDn->push_back(jetResDn);
      }
    }
  }

  const unsigned int nCleanJet = corrJets->size();
  std::sort(corrJets->begin(), corrJets->end(), GreaterByPt<pat::Jet>());
  std::sort(corrJetsUp->begin(), corrJetsUp->end(), GreaterByPt<pat::Jet>());
  std::sort(corrJetsDn->begin(), corrJetsDn->end(), GreaterByPt<pat::Jet>());

  pat::MET metUp, metDn;
  metUp.setP4(reco::Candidate::LorentzVector(metUpX, metUpY, 0, hypot(metUpX, metUpY)));
  metDn.setP4(reco::Candidate::LorentzVector(metDnX, metDnY, 0, hypot(metDnX, metDnY)));
  corrMets->push_back(met);
  corrMetsUp->push_back(metUp);
  corrMetsDn->push_back(metDn);

  event.put(corrJets);
  event.put(corrJetsUp, "up");
  event.put(corrJetsDn, "dn");
  event.put(corrMets);
  event.put(corrMetsUp, "up");
  event.put(corrMetsDn, "dn");

  if ( isMC_ )
  {
    std::sort(corrJetsResUp->begin(), corrJetsResUp->end(), GreaterByPt<pat::Jet>());
    std::sort(corrJetsResDn->begin(), corrJetsResDn->end(), GreaterByPt<pat::Jet>());

    pat::MET metResUp, metResDn;
    metResUp.setP4(reco::Candidate::LorentzVector(metResUpX, metResUpY, 0, hypot(metResUpX, metResUpY)));
    metResDn.setP4(reco::Candidate::LorentzVector(metResDnX, metResDnY, 0, hypot(metResDnX, metResDnY)));
    corrMetsResUp->push_back(metResUp);
    corrMetsResDn->push_back(metResDn);

    event.put(corrJetsResUp, "resUp");
    event.put(corrJetsResDn, "resDn");
    event.put(corrMetsResUp, "resUp");
    event.put(corrMetsResDn, "resDn");
  }

  if ( nCleanJet < minNumber_ or nCleanJet > maxNumber_ ) return false;

  return true;
}

DEFINE_FWK_MODULE(KJetSelector);

