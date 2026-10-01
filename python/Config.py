# Read-only config for TruthHitsAnalyzer: the truth graph, the track collection and the
# track-to-truth-branch maps are all read from the input file; nothing is rebuilt.
#
#   cmsRun truthHitsAnalyzer_read_cfg.py step3.root -n 10

import FWCore.ParameterSet.Config as cms
from argparse import ArgumentParser

parser = ArgumentParser()
parser.add_argument("inputFile", nargs='?', default="file:/eos/home-f/fnenna/cmsweek0926_truthGraphHackaton/CMSSW_20_1_X_graph/src/38620.0_SingleMuPt1000Extended+Run4D128PU/step3_noOutOfTime.root", metavar='FILE',
                    help="input file, default=%(default)r")
parser.add_argument('-n', "--maxevts", type=int, default=-1,
                    help="maximum number of events to process, default=%(default)s")
parser.add_argument("--tracks", default="hltGeneralTracks",
                    help="track collection the maps were built for, label or label:instance, default=%(default)r")
parser.add_argument("--associator", default="hltTrackToTruthBranchAssociators",
                    help="module label of the associator that wrote the maps, default=%(default)r")
parser.add_argument("--workingPoint", default="Fixed",
                    help="working point of the RecoToTruth map to read, default=%(default)r")
parser.add_argument("--tracksters", default="ticlTrackstersCLUE3DHigh",
                    help="trackster collection the maps were built for, default=%(default)r")
parser.add_argument("--segments", default="gemSegments",
                    help="GEM segment collection the maps were built for, default=%(default)r")
parser.add_argument("--tracksterAssociator", default="truthBranchTracksterAssociators",
                    help="module label of the trackster associator, default=%(default)r")
parser.add_argument("--gemSegmentAssociator", default="gemSegmentsRecoToTruth")
parser.add_argument("--maxMatches", type=int, default=5,
                    help="matches printed per reco object, default=%(default)s")
parser.add_argument("--graph", default="truthLogicalGraphProducer",
                    help="label of the truth::Graph product, default=%(default)r")
args = parser.parse_args()
 
if '/' not in args.inputFile and ':' not in args.inputFile:
    args.inputFile = 'file:' + args.inputFile
 
process = cms.Process("TRUTHANA")
 
process.load("FWCore.MessageService.MessageLogger_cfi")
process.MessageLogger.cerr.FwkReport.reportEvery = 100
 
process.source = cms.Source("PoolSource", fileNames=cms.untracked.vstring(args.inputFile))
process.maxEvents = cms.untracked.PSet(input=cms.untracked.int32(args.maxevts))
 
# The associators name their products <key>RecoToTruth<WP> and <key>TruthToReco, where
# the key is the collection tag's label, plus "_instance" when it has one.

def collectionKey(tag):
    key = tag.getModuleLabel()
    if tag.getProductInstanceLabel():
        key += "_" + tag.getProductInstanceLabel()
    return key
 
tracksTag = cms.InputTag(args.tracks)
trackstersTag = cms.InputTag(args.tracksters)
trackKey = collectionKey(tracksTag)
tracksterKey = collectionKey(trackstersTag)
gemSegmentTag = cms.InputTag(args.segments)
gemSegmentKey = collectionKey(gemSegmentTag)
 
process.demo = cms.EDAnalyzer("TruthHitsAnalyzer",
    src=cms.InputTag(args.graph),
    maxMatchesPrinted=cms.uint32(args.maxMatches),
    tracks=tracksTag,
    trackRecoToTruth=cms.InputTag(args.associator, trackKey + "RecoToTruth" + args.workingPoint),
    trackTruthToReco=cms.InputTag(args.associator, trackKey + "TruthToReco"),
    tracksters=trackstersTag,
    tracksterRecoToTruth=cms.InputTag(args.tracksterAssociator, tracksterKey + "RecoToTruth" + args.workingPoint),
    tracksterTruthToReco=cms.InputTag(args.tracksterAssociator, tracksterKey + "TruthToReco"),
    gemSegments =cms.InputTag(args.segments),
    gemSegmentRecoToTruth=cms.InputTag("allGEMSegmentToTruthBranchAssociators", gemSegmentKey + "RecoToTruth" + args.workingPoint),
    gemSegmentTruthToReco=cms.InputTag("allGEMSegmentToTruthBranchAssociators", gemSegmentKey + "TruthToReco"),
    debugParticles = cms.vuint32(22649, 15211, 85800, 60650)
)
 
process.p = cms.Path(process.demo)
