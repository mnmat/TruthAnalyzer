// -*- C++ -*-
//
// Package:    Analyzers/TruthHitsAnalyzer
// Class:      TruthHitsAnalyzer
//
// Reads the maps written by AllTrackToTruthBranchAssociatorsProducer and
// TruthBranchTracksterAssociatorsProducer and prints, for every track and trackster,
// all its truth-branch matches with the matched graph particles, plus a truth-side summary.
// For GEM segments it also checks every rechit against the truth hits of the hit index.
//
// Original Author:  Mark Nicholas Matthewman
//         Created:  Mon, 20 Jul 2026 15:07:41 GMT
//

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <tuple>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

#include "DataFormats/Common/interface/DetSetVector.h"
#include "DataFormats/DetId/interface/DetId.h"
#include "DataFormats/GEMRecHit/interface/GEMSegmentCollection.h"
#include "DataFormats/HGCalReco/interface/Trackster.h"
#include "DataFormats/MuonDetId/interface/GEMDetId.h"
#include "DataFormats/MuonDetId/interface/MuonSubdetId.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "SimDataFormats/Associations/interface/TICLAssociationMap.h"
#include "PhysicsTools/TruthInfo/interface/Branch.h"
#include "SimDataFormats/TrackerDigiSimLink/interface/StripDigiSimLink.h"
#include "SimDataFormats/TruthInfo/interface/Graph.h"
#include "SimDataFormats/TruthInfo/interface/LogicalGraphHitIndex.h"

namespace {
  // The hit-index channel that carries the GEM truth. Set this to the channel your hit
  // index producer fills with GEM hits.
  constexpr truth::HitChannel kGemChannel = truth::HitChannel::Muon;

  // Raw id of a hit-index hit, whether Hit exposes detId as a data member or an accessor,
  // and whether it is a DetId or a plain uint32_t.
  template <typename H>
  uint32_t rawIdOf(H const& hit) {
    if constexpr (requires { hit.detId(); })
      return static_cast<uint32_t>(hit.detId());
    else
      return static_cast<uint32_t>(hit.detId);
  }

  // The GEM roll a hit-index key refers to, or 0 if the key is not a GEM DetId. A key that
  // encodes more than a DetId (a strip-level cell key, say) also comes back as 0.
  uint32_t gemRollOf(uint32_t raw) {
    const DetId id(raw);
    if (id.det() != DetId::Muon || id.subdetId() != MuonSubdetId::GEM)
      return 0;
    return raw;
  }
}  // namespace

class TruthHitsAnalyzer : public edm::one::EDAnalyzer<> {
public:
  explicit TruthHitsAnalyzer(edm::ParameterSet const&);
  static void fillDescriptions(edm::ConfigurationDescriptions&);

private:
  // The producer declares this same type for tracks, tracksters and GEM segments
  // (TruthAssociationTraits<...>::MapType).
  using MapType = ticl::TICLAssociationMap<ticl::mapWithSharedEnergyAndScore>;
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

  using GemSimLinks = edm::DetSetVector<StripDigiSimLink>;

  // For every GEM segment rechit: which truth particles deposited in its roll, whether
  // the segment's best-matched branch did, and, from the digi-sim links, why a rechit
  // with no truth has none. links may be null when the input carries no GEM links.
  void checkGemSegments(GEMSegmentCollection const& segments,
                        MapType const& recoToTruth,
                        truth::LogicalGraphHitIndex const& hitIndex,
                        truth::Graph const& graph,
                        GemSimLinks const* links) const;

  static std::string describeParticle(truth::Graph const& graph, unsigned int id);

  const unsigned int maxMatchesPrinted_;

  const edm::EDGetTokenT<truth::Graph> graphToken_;
  const edm::EDGetTokenT<truth::LogicalGraphHitIndex> hitIndexToken_;
  const edm::EDGetTokenT<reco::TrackCollection> tracksToken_;
  const edm::EDGetTokenT<std::vector<ticl::Trackster>> trackstersToken_;
  const edm::EDGetTokenT<MapType> trackRecoToTruthToken_;
  const edm::EDGetTokenT<MapType> trackTruthToRecoToken_;
  const edm::EDGetTokenT<MapType> tracksterRecoToTruthToken_;
  const edm::EDGetTokenT<MapType> tracksterTruthToRecoToken_;
  const edm::EDGetTokenT<GEMSegmentCollection> gemSegmentToken_;
  const edm::EDGetTokenT<MapType> gemSegmentRecoToTruthToken_;
  const edm::EDGetTokenT<MapType> gemSegmentTruthToRecoToken_;
  const edm::EDGetTokenT<GemSimLinks> gemSimLinksToken_;

  const std::vector<unsigned int> debugParticles_;
};

TruthHitsAnalyzer::TruthHitsAnalyzer(edm::ParameterSet const& cfg)
    : maxMatchesPrinted_(cfg.getParameter<unsigned int>("maxMatchesPrinted")),
      graphToken_(consumes<truth::Graph>(cfg.getParameter<edm::InputTag>("src"))),
      hitIndexToken_(consumes<truth::LogicalGraphHitIndex>(cfg.getParameter<edm::InputTag>("hitIndex"))),
      tracksToken_(consumes<reco::TrackCollection>(cfg.getParameter<edm::InputTag>("tracks"))),
      trackstersToken_(consumes<std::vector<ticl::Trackster>>(cfg.getParameter<edm::InputTag>("tracksters"))),
      trackRecoToTruthToken_(consumes<MapType>(cfg.getParameter<edm::InputTag>("trackRecoToTruth"))),
      trackTruthToRecoToken_(consumes<MapType>(cfg.getParameter<edm::InputTag>("trackTruthToReco"))),
      tracksterRecoToTruthToken_(consumes<MapType>(cfg.getParameter<edm::InputTag>("tracksterRecoToTruth"))),
      tracksterTruthToRecoToken_(consumes<MapType>(cfg.getParameter<edm::InputTag>("tracksterTruthToReco"))),
      gemSegmentToken_(consumes<GEMSegmentCollection>(cfg.getParameter<edm::InputTag>("gemSegments"))),
      gemSegmentRecoToTruthToken_(consumes<MapType>(cfg.getParameter<edm::InputTag>("gemSegmentRecoToTruth"))),
      gemSegmentTruthToRecoToken_(consumes<MapType>(cfg.getParameter<edm::InputTag>("gemSegmentTruthToReco"))),
      gemSimLinksToken_(consumes<GemSimLinks>(cfg.getParameter<edm::InputTag>("gemSimLinks"))),
      debugParticles_(cfg.getParameter<std::vector<unsigned int>>("debugParticles")) {}

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
                                          << " rows but the collection has " << collection.size() << " entries";
  }

  for (unsigned int i = 0; i < collection.size(); ++i) {
    auto const& row = recoToTruth[i];
    std::cout << domain << " " << i << " " << describeReco(i) << ": " << row.size()
              << (row.size() == 1 ? " match" : " matches") << std::endl;

    // Rows are ordered best first: assignable (detector) particles by ascending score,
    // then the barred roots. 1 - score is the reco purity.
    const std::size_t nPrinted = std::min<std::size_t>(row.size(), maxMatchesPrinted_);
    for (std::size_t m = 0; m < nPrinted; ++m) {
      auto const& match = row[m];
      std::cout << "    [" << m << "] " << describeParticle(graph, match.index()) << " | " << payloadName << "="
                << match.sharedEnergy() << " score=" << match.score() << " purity=" << 1.f - match.score()
                << std::endl;
    }
    if (row.size() > nPrinted) {
      std::cout << "    ... " << row.size() - nPrinted << " more" << std::endl;
    }
  }
}

void TruthHitsAnalyzer::printTruthSummary(std::string const& domain, MapType const& truthToReco) const {
  // TruthToReco rows are indexed by graph particle id.
  unsigned int matchedBranches = 0;
  for (unsigned int id = 0; id < truthToReco.size(); ++id) {
    matchedBranches += truthToReco[id].empty() ? 0 : 1;
  }
  std::cout << matchedBranches << " of " << truthToReco.size() << " truth branches have at least one " << domain
            << std::endl;
}

void TruthHitsAnalyzer::checkGemSegments(GEMSegmentCollection const& segments,
                                         MapType const& recoToTruth,
                                         truth::LogicalGraphHitIndex const& hitIndex,
                                         truth::Graph const& graph,
                                         GemSimLinks const* links) const {
  // Does the graph carry out-of-time pileup at all? If the links below show bx != 0
  // signal and this is zero, the graph was built from the in-time crossing only.
  unsigned int outOfTimeParticles = 0;
  for (unsigned int p = 0; p < graph.nParticles(); ++p) {
    if (!truth::Branch(&graph, p).isInTime())
      ++outOfTimeParticles;
  }
  std::cout << "graph: " << outOfTimeParticles << " of " << graph.nParticles() << " particles are out of time"
            << std::endl;
  if (links == nullptr) {
    std::cout << "GEM check: no GEM digi-sim links in the input, so a rechit with no truth cannot be classified"
              << std::endl;
  }

  if (!hitIndex.hasChannel(kGemChannel)) {
    std::cout << "GEM check: the hit index has no GEM channel, so no GEM rechit can be found in the truth"
              << std::endl;
    return;
  }

  // Which particles deposited DIRECTLY in each roll. Direct, not subgraph: a delta ray
  // owns its own hit, so this lists who actually made it, while the best-branch check
  // below asks the subgraph question the associator asks.
  std::unordered_map<uint32_t, std::vector<unsigned int>> rollOwners;
  unsigned int gemKeys = 0;
  unsigned int otherKeys = 0;
  for (unsigned int p = 0; p < graph.nParticles(); ++p) {
    for (auto const& hit : hitIndex.directHits(kGemChannel, p)) {
      const uint32_t roll = gemRollOf(rawIdOf(hit));
      if (roll == 0) {
        ++otherKeys;
        continue;
      }
      ++gemKeys;
      auto& owners = rollOwners[roll];
      if (owners.empty() || owners.back() != p)
        owners.push_back(p);
    }
  }

  for (const unsigned int p : debugParticles_) {
    if (p >= graph.nParticles())
      continue;
    std::cout << "debug " << describeParticle(graph, p) << std::endl;
    for (auto const& hit : hitIndex.directHits(kGemChannel, p)) {
      const uint32_t raw = rawIdOf(hit);
      std::cout << "    direct hit in ";
      if (gemRollOf(raw))
        std::cout << GEMDetId(raw);
      else
        std::cout << "non-GEM key 0x" << std::hex << raw << std::dec << " det=" << DetId(raw).det()
                  << " subdet=" << DetId(raw).subdetId();
      std::cout << std::endl;
    }
  }

  std::cout << "GEM truth: " << gemKeys << " hits keyed by a GEM DetId in " << rollOwners.size() << " rolls, "
            << otherKeys << " hits with other keys" << std::endl;
  if (gemKeys == 0 && otherKeys > 0) {
    std::cout << "  WARNING: the GEM channel keys are not plain GEM DetIds (strip-level cell keys?). Decode them "
                 "with the same key function the hit index producer uses, in gemRollOf()."
              << std::endl;
  }

  unsigned int nRecHits = 0;
  unsigned int nWithTruth = 0;
  unsigned int nInBestBranch = 0;
  // Rechits with no truth, by what the digi-sim links say made them.
  unsigned int nNoLink = 0;
  unsigned int nNeighbourLink = 0;
  unsigned int nOutOfTime = 0;
  unsigned int nInTimeMissing = 0;
  std::map<int, unsigned int> missingByBx;
  // In-time (event, track) pairs that made a rechit but are absent from the hit index.
  std::set<std::pair<int, unsigned int>> missingInTimeTracks;
  unsigned int i = 0;
  for (auto const& segment : segments) {
    // Row i is segment i in the collection's storage order, the order the producer used.
    auto const* row = i < recoToTruth.size() ? &recoToTruth[i] : nullptr;
    const bool hasMatch = row != nullptr && !row->empty();

    std::cout << "GEM segment " << i << " " << GEMDetId(segment.geographicalId()) << ", " << segment.nRecHits() << " rechits, ";
    std::unordered_set<uint32_t> bestRolls;
    if (hasMatch) {
      auto const& best = (*row)[0];
      std::cout << "best match " << describeParticle(graph, best.index()) << " sharedHits=" << best.sharedEnergy()
                << " score=" << best.score() << std::endl;
      // The rolls the best match's whole branch deposited in, as the associator sees it.
      for (auto const& hit : hitIndex.subgraphHits(kGemChannel, best.index())) {
        if (const uint32_t roll = gemRollOf(rawIdOf(hit)))
          bestRolls.insert(roll);
      }
    } else {
      std::cout << "no truth match" << std::endl;
    }

    for (auto const& rh : segment.specificRecHits()) {
      ++nRecHits;
      const GEMDetId roll = rh.gemId();
      const auto owners = rollOwners.find(roll.rawId());
      const bool found = owners != rollOwners.end();
      const bool inBest = bestRolls.count(roll.rawId()) > 0;
      nWithTruth += found ? 1 : 0;
      nInBestBranch += inBest ? 1 : 0;

      std::cout << "    rechit " << roll << " strips " << rh.firstClusterStrip() << "-"
                << rh.firstClusterStrip() + rh.clusterSize() - 1 << ": ";
      if (!found) {
        std::cout << "NOT in the truth";
      } else {
        std::cout << "truth particles";
        const std::size_t nShown = std::min<std::size_t>(owners->second.size(), maxMatchesPrinted_);
        for (std::size_t k = 0; k < nShown; ++k)
          std::cout << " " << owners->second[k];
        if (owners->second.size() > nShown)
          std::cout << " (+" << owners->second.size() - nShown << ")";
      }
      if (hasMatch && found)
        std::cout << (inBest ? ", in best branch" : ", NOT in best branch");
      else if (hasMatch && inBest)
        std::cout << ", in best branch but no direct owner: hit index inconsistent";

      // What the digitizer linked to the rechit's strips: (bx, event, SimTrack id).
      if (links != nullptr) {
        const int first = rh.firstClusterStrip();
        const int last = first + rh.clusterSize() - 1;
        std::set<std::tuple<int, int, unsigned int>> onStrips;
        bool onNeighbour = false;
        const auto detSet = links->find(roll.rawId());
        if (detSet != links->end()) {
          for (auto const& link : *detSet) {
            const int strip = static_cast<int>(link.channel());
            if (strip >= first && strip <= last) {
              onStrips.emplace(link.eventId().bunchCrossing(), link.eventId().event(), link.SimTrackId());
            } else if (strip == first - 1 || strip == last + 1) {
              onNeighbour = true;
            }
          }
        }

        std::cout << " | links:";
        if (onStrips.empty()) {
          std::cout << (onNeighbour ? " only on a neighbouring strip (strip numbering offset?)"
                                    : " none (digitizer background or noise)");
        } else {
          std::size_t shown = 0;
          for (auto const& [bx, ev, track] : onStrips) {
            if (shown++ == maxMatchesPrinted_) {
              std::cout << " (+" << onStrips.size() - maxMatchesPrinted_ << ")";
              break;
            }
            std::cout << " [bx " << bx << " ev " << ev << " trk " << track << "]";
          }
        }

        if (!found) {
          if (onStrips.empty()) {
            ++(onNeighbour ? nNeighbourLink : nNoLink);
          } else {
            const bool anyInTime =
                std::any_of(onStrips.begin(), onStrips.end(), [](auto const& l) { return std::get<0>(l) == 0; });
            for (auto const& l : onStrips)
              ++missingByBx[std::get<0>(l)];
            if (anyInTime) {
              ++nInTimeMissing;
              for (auto const& [bx, ev, track] : onStrips)
                if (bx == 0)
                  missingInTimeTracks.emplace(ev, track);
              std::cout << " -> IN-TIME sim hit missing from the hit index";
            } else {
              ++nOutOfTime;
              std::cout << " -> out-of-time pileup only";
            }
          }
        }
      }
      std::cout << std::endl;
    }
    ++i;
  }

  if (i != recoToTruth.size()) {
    std::cout << "  WARNING: " << i << " GEM segments but " << recoToTruth.size()
              << " map rows: the segment tag is not the collection the associator ran on" << std::endl;
  }
  std::cout << "GEM check: " << nWithTruth << " of " << nRecHits << " rechits are in a roll with truth hits, "
            << nInBestBranch << " in the roll of their segment's best branch" << std::endl;
  if (links != nullptr) {
    std::cout << "GEM rechits with no truth: " << nNoLink << " no sim link, " << nNeighbourLink
              << " link on a neighbouring strip only, " << nOutOfTime << " out-of-time pileup only, " << nInTimeMissing
              << " in-time but missing from the hit index" << std::endl;
    std::cout << "  links on those rechits by bx:";
    for (auto const& [bx, n] : missingByBx)
      std::cout << " bx" << bx << "=" << n;
    std::cout << std::endl;
    if (!missingInTimeTracks.empty()) {
      std::cout << "  in-time (event, SimTrack) pairs missing from the hit index:";
      for (auto const& [ev, track] : missingInTimeTracks)
        std::cout << " (" << ev << "," << track << ")";
      std::cout << std::endl;
    }
  }
}

void TruthHitsAnalyzer::analyze(edm::Event const& event, edm::EventSetup const&) {
  auto const& graph = event.get(graphToken_);
  auto const& hitIndex = event.get(hitIndexToken_);
  auto const& tracks = event.get(tracksToken_);
  auto const& tracksters = event.get(trackstersToken_);
  auto const& trackRecoToTruth = event.get(trackRecoToTruthToken_);
  auto const& trackTruthToReco = event.get(trackTruthToRecoToken_);
  auto const& tracksterRecoToTruth = event.get(tracksterRecoToTruthToken_);
  auto const& tracksterTruthToReco = event.get(tracksterTruthToRecoToken_);
  auto const& gemSegments = event.get(gemSegmentToken_);
  auto const& gemSegmentRecoToTruth = event.get(gemSegmentRecoToTruthToken_);
  auto const& gemSegmentTruthToReco = event.get(gemSegmentTruthToRecoToken_);

  std::cout << "=== run " << event.id().run() << " event " << event.id().event() << ": " << tracks.size()
            << " tracks, " << tracksters.size() << " tracksters, " << graph.nParticles() << " graph particles"
            << std::endl;

  // // Tracks: the first payload slot is the shared hit count.
  // printRecoToTruth("track", tracks, trackRecoToTruth, graph, "sharedHits", [&tracks](unsigned int i) {
  //   std::ostringstream out;
  //   out << "pt=" << tracks[i].pt() << " eta=" << tracks[i].eta() << " hits=" << tracks[i].numberOfValidHits();
  //   return out.str();
  // });

  // // Tracksters: the first payload slot is the shared energy in GeV.
  // printRecoToTruth(
  //     "trackster", tracksters, tracksterRecoToTruth, graph, "sharedEnergy", [&tracksters](unsigned int i) {
  //       auto const& t = tracksters[i];
  //       std::ostringstream out;
  //       out << "E=" << t.raw_energy() << " eta=" << t.barycenter().eta() << " layerClusters=" << t.vertices().size();
  //       return out.str();
  //     });

  // Optional: GEM digi-sim links are not in every event content.
  auto const gemSimLinks = event.getHandle(gemSimLinksToken_);
  checkGemSegments(
      gemSegments, gemSegmentRecoToTruth, hitIndex, graph, gemSimLinks.isValid() ? gemSimLinks.product() : nullptr);

  printTruthSummary("track", trackTruthToReco);
  printTruthSummary("trackster", tracksterTruthToReco);
  printTruthSummary("GEM segment", gemSegmentTruthToReco);
}

void TruthHitsAnalyzer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("src", edm::InputTag("truthLogicalGraphProducer"));
  desc.add<edm::InputTag>("hitIndex", edm::InputTag("truthLogicalGraphHitIndexProducer"))
      ->setComment("Must be the hit index the associators ran on");
  desc.add<unsigned int>("maxMatchesPrinted", 5)
      ->setComment("Matches (and truth owners per GEM rechit) printed per object; the rest are counted");
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
  desc.add<edm::InputTag>("gemSimLinks", edm::InputTag("simMuonGEMDigis", "GEM"))
      ->setComment("GEM StripDigiSimLinks; optional, the rechit classification is skipped without them");
  desc.add<std::vector<unsigned int>>("debugParticles", {})
  ->setComment("Particles whose GEM hits are dumped roll by roll (ids are per event: run with -n 1)");

  descriptions.addWithDefaultLabel(desc);
}

DEFINE_FWK_MODULE(TruthHitsAnalyzer);