#include <boost/foreach.hpp>

#include "RecoTauTag/RecoTau/interface/TauDiscriminationProducerBase.h"
#include "FWCore/Utilities/interface/InputTag.h"

#include "CommonTools/Utils/interface/StringObjectFunction.h"
#include "DataFormats/Math/interface/deltaR.h"

#include "DataFormats/TauReco/interface/PFRecoTauChargedHadron.h"
#include "DataFormats/TauReco/interface/PFRecoTauChargedHadronFwd.h"

namespace {
// Apply a hypothesis on the mass of the strips.
math::XYZTLorentzVector applyMassConstraint(
    const math::XYZTLorentzVector& vec,double mass) {
  double factor = sqrt(vec.energy()*vec.energy()-mass*mass)/vec.P();
  return math::XYZTLorentzVector(
      vec.px()*factor,vec.py()*factor,vec.pz()*factor,vec.energy());
}
}

class PFRecoTauDiscriminationByHPSSelection
  : public PFTauDiscriminationProducerBase {
  public:
    explicit PFRecoTauDiscriminationByHPSSelection(const edm::ParameterSet&);
    ~PFRecoTauDiscriminationByHPSSelection();
    double discriminate(const reco::PFTauRef&);

  private:
    typedef StringObjectFunction<reco::PFTau> TauFunc;

    struct DecayModeCuts 
    {
      DecayModeCuts()
	: maxMass_(0)
      {}
      ~DecayModeCuts() {} // CV: maxMass object gets deleted by PFRecoTauDiscriminationByHPSSelection destructor
      unsigned nTracksMin_;
      unsigned nChargedPFCandsMin_;
      double minMass_;
      TauFunc* maxMass_;
      double minPi0Mass_;
      double maxPi0Mass_;
      double assumeStripMass_;
    };

    typedef std::pair<unsigned int, unsigned int> IntPair;
    typedef std::pair<double, double> DoublePair;
    typedef std::map<IntPair, DecayModeCuts> DecayModeCutMap;

    TauFunc signalConeFun_;
    DecayModeCutMap decayModeCuts_;
    double matchingCone_;
    double minPt_;
};

PFRecoTauDiscriminationByHPSSelection::PFRecoTauDiscriminationByHPSSelection(const edm::ParameterSet& pset)
  : PFTauDiscriminationProducerBase(pset),
    signalConeFun_(pset.getParameter<std::string>("coneSizeFormula")) 
{
  // Get the matchign cut
  matchingCone_ = pset.getParameter<double>("matchingCone");
  minPt_ = pset.getParameter<double>("minTauPt");
  // Get the mass cuts for each decay mode
  typedef std::vector<edm::ParameterSet> VPSet;
  const VPSet& decayModes = pset.getParameter<VPSet>("decayModes");
  BOOST_FOREACH( const edm::ParameterSet &decayMode, decayModes ) {
    // The mass window(s)
    DecayModeCuts cuts;
    if ( decayMode.exists("nTracksMin") ) {
      cuts.nTracksMin_ = decayMode.getParameter<unsigned>("nTracksMin");
    } else {
      cuts.nTracksMin_ = 0;
    }
    if ( decayMode.exists("nChargedPFCandsMin") ) {
      cuts.nChargedPFCandsMin_ = decayMode.getParameter<unsigned>("nChargedPFCandsMin");
    } else {
      cuts.nChargedPFCandsMin_ = 0;
    }
    cuts.minMass_ = decayMode.getParameter<double>("minMass");
    cuts.maxMass_ = new TauFunc(decayMode.getParameter<std::string>("maxMass"));
    if ( decayMode.exists("minPi0Mass") ) {
      cuts.minPi0Mass_ = decayMode.getParameter<double>("minPi0Mass");
      cuts.maxPi0Mass_ = decayMode.getParameter<double>("maxPi0Mass");
    } else {
      cuts.minPi0Mass_ = -1.e3;
      cuts.maxPi0Mass_ = 1.e9;
    }
    if ( decayMode.exists("assumeStripMass") ) {
      cuts.assumeStripMass_ = decayMode.getParameter<double>("assumeStripMass");
    } else {
      cuts.assumeStripMass_ = -1.0;
    }
    decayModeCuts_.insert(std::make_pair(
            // The decay mode as a key
            std::make_pair(
                decayMode.getParameter<uint32_t>("nCharged"),
                decayMode.getParameter<uint32_t>("nPiZeros")),
            cuts
          ));
  }
}

PFRecoTauDiscriminationByHPSSelection::~PFRecoTauDiscriminationByHPSSelection()
{
  for ( DecayModeCutMap::iterator it = decayModeCuts_.begin();
	it != decayModeCuts_.end(); ++it ) {
    delete it->second.maxMass_;
  }
}

double
PFRecoTauDiscriminationByHPSSelection::discriminate(const reco::PFTauRef& tau) 
{
  //std::cout << "<PFRecoTauDiscriminationByHPSSelection::discriminate>:" << std::endl;
  //std::cout << " nCharged = " << tau->signalTauChargedHadronCandidates().size() << std::endl;
  //std::cout << " nPiZeros = " << tau->signalPiZeroCandidates().size() << std::endl;

  // Check if we pass the min pt
  if ( tau->pt() < minPt_ ) {
    return 0.0;
  }

  // See if we select this decay mode
  DecayModeCutMap::const_iterator massWindowIter =
      decayModeCuts_.find(std::make_pair(tau->signalTauChargedHadronCandidates().size(),
                                         tau->signalPiZeroCandidates().size()));
  // Check if decay mode is supported
  if ( massWindowIter == decayModeCuts_.end() ) {
    return 0.0;
  }
  const DecayModeCuts& massWindow = massWindowIter->second;

  //std::cout << "nTracksMin = " << massWindow.nTracksMin_ << std::endl;
  if ( massWindow.nTracksMin_ > 0 ) {
    unsigned int nTracks = 0;
    const std::vector<reco::PFRecoTauChargedHadron>& chargedHadrons = tau->signalTauChargedHadronCandidates();
    for ( std::vector<reco::PFRecoTauChargedHadron>::const_iterator chargedHadron = chargedHadrons.begin();
	  chargedHadron != chargedHadrons.end(); ++chargedHadron ) {
      if ( chargedHadron->algoIs(reco::PFRecoTauChargedHadron::kChargedPFCandidate) || chargedHadron->algoIs(reco::PFRecoTauChargedHadron::kTrack) ) {
	++nTracks;
      }
    }
    //std::cout << "nTracks = " << nTracks << std::endl;
    if ( nTracks < massWindow.nTracksMin_ ) {
      return 0.0;
    }
  }
  //std::cout << "nChargedPFCandsMin = " << massWindow.nChargedPFCandsMin_ << std::endl;
  if ( massWindow.nChargedPFCandsMin_ > 0 ) {
    unsigned int nChargedPFCands = 0;
    const std::vector<reco::PFRecoTauChargedHadron>& chargedHadrons = tau->signalTauChargedHadronCandidates();
    for ( std::vector<reco::PFRecoTauChargedHadron>::const_iterator chargedHadron = chargedHadrons.begin();
	  chargedHadron != chargedHadrons.end(); ++chargedHadron ) {
      if ( chargedHadron->algoIs(reco::PFRecoTauChargedHadron::kChargedPFCandidate) ) {
	++nChargedPFCands;
      }
    }
    //std::cout << "nChargedPFCands = " << nChargedPFCands << std::endl;
    if ( nChargedPFCands < massWindow.nChargedPFCandsMin_ ) {
      return 0.0;
    }
  }

  math::XYZTLorentzVector tauP4 = tau->p4();
  //std::cout << "tau: Pt = " << tauP4.pt() << ", eta = " << tauP4.eta() << ", phi = " << tauP4.phi() << ", mass = " << tauP4.mass() << std::endl;
  // Find the total pizero p4
  reco::Candidate::LorentzVector stripsP4;
  BOOST_FOREACH(const reco::RecoTauPiZero& cand, 
      tau->signalPiZeroCandidates()){
    math::XYZTLorentzVector candP4 = cand.p4();
    stripsP4 += candP4;
  }

  // Apply strip mass assumption corrections
  if (massWindow.assumeStripMass_ >= 0) {
    BOOST_FOREACH(const reco::RecoTauPiZero& cand, 
        tau->signalPiZeroCandidates()){
      math::XYZTLorentzVector uncorrected = cand.p4();
      math::XYZTLorentzVector corrected = 
        applyMassConstraint(uncorrected, massWindow.assumeStripMass_);
      math::XYZTLorentzVector correction = corrected - uncorrected;
      tauP4 += correction;
      stripsP4 += correction;
    }
  }
  //std::cout << "strips: Pt = " << stripsP4.pt() << ", eta = " << stripsP4.eta() << ", phi = " << stripsP4.phi() << ", mass = " << stripsP4.mass() << std::endl;

  // Check if tau fails mass cut
  double maxMass_value = (*massWindow.maxMass_)(*tau);
  if ( tauP4.M() > maxMass_value || tauP4.M() < massWindow.minMass_ ) {
    return 0.0;
  }

  // Check if it fails the pi 0 IM cut
  if ( stripsP4.M() > massWindow.maxPi0Mass_ ||
       stripsP4.M() < massWindow.minPi0Mass_ ) {
    return 0.0;
  }

  // Check if tau passes matching cone cut
  //std::cout << "dR(tau, jet) = " << deltaR(tauP4, tau->jetRef()->p4()) << std::endl;
  if ( deltaR(tauP4, tau->jetRef()->p4()) > matchingCone_ ) {
    return 0.0;
  }

  // Check if tau passes cone cut
  double cone_size = signalConeFun_(*tau);
  // Check if any charged objects fail the signal cone cut
  BOOST_FOREACH(const reco::PFRecoTauChargedHadron& cand,
                tau->signalTauChargedHadronCandidates()) {
    //std::cout << "dR(tau, signalPFChargedHadr) = " << deltaR(cand.p4(), tauP4) << std::endl;
    if ( deltaR(cand.p4(), tauP4) > cone_size )
      return 0.0;
  }
  // Now check the pizeros
  BOOST_FOREACH(const reco::RecoTauPiZero& cand,
                tau->signalPiZeroCandidates()) {
    //std::cout << "dR(tau, signalPiZero) = " << deltaR(cand.p4(), tauP4) << std::endl;
    if ( deltaR(cand.p4(), tauP4) > cone_size )
      return 0.0;
  }

  // Otherwise, we pass!
  return 1.0;
}

DEFINE_FWK_MODULE(PFRecoTauDiscriminationByHPSSelection);
