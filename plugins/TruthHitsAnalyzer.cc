// -*- C++ -*-
//
// Package:    Analyzers/TruthHitsAnalyzer
// Class:      TruthHitsAnalyzer
//
// Reads the maps written by AllTrackToTruthBranchAssociatorsProducer and
// TruthBranchTracksterAssociatorsProducer and prints, for every track and trackster,
// all its truth-branch matches with the matched graph particles, plus a truth-side summary.
//
// Original Author:  Mark Nicholas Matthewman
//         Created:  Mon, 20 Jul 2026 15:07:41 GMT
//

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/InputTag.h"

#include "DataFormats/HGCalReco/interface/Trackster.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "SimDataFormats/Associations/interface/TICLAssociationMap.h"
#include "SimDataFormats/TruthInfo/interface/Graph.h"

#include "DataFormats/GEMRecHit/interface/GEMSegmentCollection.h"

class TruthHitsAnalyzer : public edm::one::EDAnalyzer<> {
public:
  explicit TruthHitsAnalyzer(edm::ParameterSet const&);
  static void fillDescriptions(edm::ConfigurationDescriptions&);

private:
  // The producer declares this same type for both tracks and tracksters
  // (TruthAssociationTraits<...>::MapType).
  using MapType = ticl::TICLAssociationMap<ticl::mapWithSharedEnergyAndScore>;
  // = ticl::TICLAssociationMap<ticl::mapWithSharedHitsAndScore>;
  void analyze(edm::Event const&, edm::EventSetup const&) override;

  // Prints every reco object of one domain with its truth matches.
  // describeReco(i) returns a one-line summary of reco object i; payloadName names the
  // first payload slot (shared hits for tracks, shared energy for tracksters).
  template <typename Collection, typename Describe>
  void printRecoToTruth(std::string const& domain,
                        Collection const& collection,
                        MapType const& recoToTruth,
                        truth::Graph const& graph,
                        std::string const& payloadName,
                        Describe describeReco) const;

  void printTruthSummary(std::string const& domain, MapType const& truthToReco) const;

  static std::string describeParticle(truth::Graph const& graph, unsigned int id);

  const unsigned int maxMatchesPrinted_;

  const edm::EDGetTokenT<truth::Graph> graphToken_;
  const edm::EDGetTokenT<reco::TrackCollection> tracksToken_;
  const edm::EDGetTokenT<std::vector<ticl::Trackster>> trackstersToken_;
  const edm::EDGetTokenT<MapType> trackRecoToTruthToken_;
  const edm::EDGetTokenT<MapType> trackTruthToRecoToken_;
  const edm::EDGetTokenT<MapType> tracksterRecoToTruthToken_;
  const edm::EDGetTokenT<MapType> tracksterTruthToRecoToken_;
  const edm::EDGetTokenT<GEMSegmentCollection> gemSegmentToken_;
  const edm::EDGetTokenT<MapType> gemSegmentRecoToTruthToken_;
  const edm::EDGetTokenT<MapType> gemSegmentTruthToRecoToken_;
};

TruthHitsAnalyzer::TruthHitsAnalyzer(edm::ParameterSet const& cfg)
    : maxMatchesPrinted_(cfg.getParameter<unsigned int>("maxMatchesPrinted")),
      graphToken_(consumes<truth::Graph>(cfg.getParameter<edm::InputTag>("src"))),
      tracksToken_(consumes<reco::TrackCollection>(cfg.getParameter<edm::InputTag>("tracks"))),
      trackstersToken_(consumes<std::vector<ticl::Trackster>>(cfg.getParameter<edm::InputTag>("tracksters"))),
      trackRecoToTruthToken_(consumes<MapType>(cfg.getParameter<edm::InputTag>("trackRecoToTruth"))),
      trackTruthToRecoToken_(consumes<MapType>(cfg.getParameter<edm::InputTag>("trackTruthToReco"))),
      tracksterRecoToTruthToken_(consumes<MapType>(cfg.getParameter<edm::InputTag>("tracksterRecoToTruth"))),
      tracksterTruthToRecoToken_(consumes<MapType>(cfg.getParameter<edm::InputTag>("tracksterTruthToReco"))),
      gemSegmentToken_(consumes<GEMSegmentCollection>(cfg.getParameter<edm::InputTag>("gemSegments"))),
      gemSegmentRecoToTruthToken_(consumes<MapType>(cfg.getParameter<edm::InputTag>("gemSegmentRecoToTruth"))),
      gemSegmentTruthToRecoToken_(consumes<MapType>(cfg.getParameter<edm::InputTag>("gemSegmentTruthToReco"))) {}

std::string TruthHitsAnalyzer::describeParticle(truth::Graph const& graph, unsigned int id) {
  std::ostringstream out;
  if (id >= graph.nParticles()) {
    out << "particle " << id << " (outside the graph: map and graph come from different jobs?)";
    return out.str();
  }
  auto const& p = graph.particles()[id];
  out << "particle " << id << " pdgId=" << graph.particle(id).pdgId() << " charge=" << graph.particle(id).charge()
      << " pt=" << p.momentum.pt() << " eta=" << p.momentum.eta() << " phi=" << p.momentum.phi()
      << " E=" << p.momentum.energy() << (p.isSignal() ? " signal" : " pileup") << (p.hasGen() ? " gen" : " sim");
  return out.str();
}

template <typename Collection, typename Describe>
void TruthHitsAnalyzer::printRecoToTruth(std::string const& domain,
                                         Collection const& collection,
                                         MapType const& recoToTruth,
                                         truth::Graph const& graph,
                                         std::string const& payloadName,
                                         Describe describeReco) const {
  // Row i of RecoToTruth is object i of the associated collection. A size mismatch means
  // the collection tag is not the one the associator was configured with.
  if (recoToTruth.size() != collection.size()) {
    throw cms::Exception("Configuration") << domain << " RecoToTruth map has " << recoToTruth.size()
                                          << " rows but the collection has " << collection.size() << " entries" << std::endl;
  }

  for (unsigned int i = 0; i < collection.size(); ++i) {
    auto const& row = recoToTruth[i];
    std::cout  << domain << " " << i << " " << describeReco(i) << ": " << row.size()
                                       << (row.size() == 1 ? " match" : " matches") << std::endl;

    // Rows are ordered best first: assignable (detector) particles by ascending score,
    // then the barred roots. 1 - score is the reco purity.
    const std::size_t nPrinted = std::min<std::size_t>(row.size(), maxMatchesPrinted_);
    for (std::size_t m = 0; m < nPrinted; ++m) {
      auto const& match = row[m];
  //     std::span<const truth::LogicalGraphHitIndex::Hit> direct =
  //     hitIndex.directHits(HitChannel::HGCalCalo, match.index());
  // std::span<const truth::LogicalGraphHitIndex::Hit> subgraph =
  //     hitIndex.subgraphHits(HitChannel::HGCalCalo, match.index());
  // std::span<const truth::LogicalGraphHitIndex::Hit> trk =
  //     hitIndex.subgraphHits(HitChannel::Tracker, match.index());
    

      std::cout  << "    [" << m << "] " << describeParticle(graph, match.index()) << " | "
                                         << payloadName << "=" << match.sharedEnergy()
                                         << " score=" << match.score() << " purity=" << 1.f - match.score() << std::endl;

                                         
      
    }
    if (row.size() > nPrinted) {
      std::cout  << "    ... " << row.size() - nPrinted << " more" << std::endl;
    }
  }
}

void TruthHitsAnalyzer::printTruthSummary(std::string const& domain, MapType const& truthToReco) const {
  // TruthToReco rows are indexed by graph particle id.
  unsigned int matchedBranches = 0;
  for (unsigned int id = 0; id < truthToReco.size(); ++id) {
    matchedBranches += truthToReco[id].empty() ? 0 : 1;
  }
  std::cout  << matchedBranches << " of " << truthToReco.size()
                                     << " truth branches have at least one " << domain << std::endl;
}

void TruthHitsAnalyzer::analyze(edm::Event const& event, edm::EventSetup const&) {
  auto const& graph = event.get(graphToken_);
  auto const& tracks = event.get(tracksToken_);
  auto const& tracksters = event.get(trackstersToken_);
  auto const& trackRecoToTruth = event.get(trackRecoToTruthToken_);
  auto const& trackTruthToReco = event.get(trackTruthToRecoToken_);
  auto const& tracksterRecoToTruth = event.get(tracksterRecoToTruthToken_);
  auto const& tracksterTruthToReco = event.get(tracksterTruthToRecoToken_);
  auto const& gemSegments = event.get(gemSegmentToken_);
  auto const& gemSegmentRecoToTruth = event.get(gemSegmentRecoToTruthToken_);
  auto const& gemSegmentTruthToReco = event.get(gemSegmentTruthToRecoToken_);

  // auto const& hitIndex = iEvent.get(hitIndexToken_);  // truth::LogicalGraphHitIndex
  // using truth::HitChannel;

  std::cout  << "=== run " << event.id().run() << " event " << event.id().event() << ": "
                                     << tracks.size() << " tracks, " << tracksters.size() << " tracksters, "
                                     << graph.nParticles() << " graph particles" << std::endl;

  // Tracks: the first payload slot is the shared hit count.
  printRecoToTruth("track", tracks, trackRecoToTruth, graph, "sharedHits", [&tracks](unsigned int i) {
    std::ostringstream out;
    out << "pt=" << tracks[i].pt() << " eta=" << tracks[i].eta() << " hits=" << tracks[i].numberOfValidHits() << std::endl;
    return out.str();
  });

  // Tracksters: the first payload slot is the shared energy in GeV.
  printRecoToTruth(
      "trackster", tracksters, tracksterRecoToTruth, graph, "sharedEnergy", [&tracksters](unsigned int i) {
        auto const& t = tracksters[i];
        std::ostringstream out;
        out << "E=" << t.raw_energy() << " eta=" << t.barycenter().eta() << " layerClusters=" << t.vertices().size() << std::endl;
        return out.str();
      });


  // printRecoToTruth(
  //   "gemSegment", gemSegments, gemSegmentRecoToTruth, graph, "sharedHits", [&gemSegments](unsigned int i) {
  //     auto const& s = gemSegments[i];
  //     std::ostringstream out;
  //     out << " eta=" << s.momentum().eta() << " phi=" << s.momentum().phi();
  //     return out.str();
  //   }
  // );

  int i = 0;
  for (auto const& segment : gemSegments) {
    auto const& row = gemSegmentRecoToTruth[i];
    const std::size_t nPrinted = std::min<std::size_t>(row.size(), 10000);
    for (std::size_t m = 0; m < nPrinted; ++m) {
      auto const& match = row[m];    
      std::cout << "GEM Segment " << i << std::endl;
      std::cout  << "    [" << m << "] " << describeParticle(graph, match.index()) << " | " << "=" << match.sharedEnergy()
      << " score=" << match.score() << " purity=" << 1.f - match.score() << std::endl;  
    }
    i++;
  }
  printTruthSummary("track", trackTruthToReco);
  printTruthSummary("trackster", tracksterTruthToReco);
}

void TruthHitsAnalyzer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("src", edm::InputTag("truthLogicalGraphProducer"));
  desc.add<unsigned int>("maxMatchesPrinted", 5)->setComment("Matches printed per reco object; the rest are counted");
  // Instance labels are <collection key>RecoToTruth<working point> and
  // <collection key>TruthToReco, where the key is label[_instance] of the collection tag.
  desc.add<edm::InputTag>("tracks", edm::InputTag("hltGeneralTracks"));
  desc.add<edm::InputTag>("trackRecoToTruth",
                          edm::InputTag("hltTrackToTruthBranchAssociators", "hltGeneralTracksRecoToTruthFixed"));
  desc.add<edm::InputTag>("trackTruthToReco",
                          edm::InputTag("hltTrackToTruthBranchAssociators", "hltGeneralTracksTruthToReco"));
  desc.add<edm::InputTag>("tracksters", edm::InputTag("ticlTrackstersCLUE3DHigh"));
  desc.add<edm::InputTag>(
      "tracksterRecoToTruth",
      edm::InputTag("truthBranchTracksterAssociators", "ticlTrackstersCLUE3DHighRecoToTruthFixed"));
  desc.add<edm::InputTag>(
      "tracksterTruthToReco",
      edm::InputTag("truthBranchTracksterAssociators", "ticlTrackstersCLUE3DHighTruthToReco"));
  desc.add<edm::InputTag>("gemSegments", edm::InputTag("gemSegments"));
  desc.add<edm::InputTag>("gemSegmentRecoToTruth",
                          edm::InputTag("gemSegmentsRecoToTruth", "gemSegmentsRecoToTruthFixed"));
  desc.add<edm::InputTag>("gemSegmentTruthToReco",
                          edm::InputTag("gemSegmentsRecoToTruth", "gemSegmentsTruthToReco"));
  descriptions.addWithDefaultLabel(desc);
}

DEFINE_FWK_MODULE(TruthHitsAnalyzer);