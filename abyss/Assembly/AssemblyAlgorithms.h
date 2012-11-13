#ifndef ASSEMBLYALGORITHMS_H
#define ASSEMBLYALGORITHMS_H 1

#include "BranchGroup.h"
#include "BranchRecord.h"
#include "FastaWriter.h"
#include "SequenceCollection.h"
#include <ostream>
#include <vector>

class Histogram;

/** A summary of the in- and out-degree of a vertex. */
enum SeqContiguity
{
	SC_ISLAND, // sequence is completely isolated
	SC_ENDPOINT, // one end of the sequence is open
	SC_CONTIGUOUS // the sequence is closed on both ends
};

/** De Bruijn graph assembly algorithms. */
namespace AssemblyAlgorithms
{

// Read a sequence file and load them into the collection
void loadSequences(ISequenceCollection* seqCollection,
		std::string inFile);

/** Generate the adjacency information for all the sequences in the
 * collection. This is required before any other algorithm can run.
 */
void generateAdjacency(ISequenceCollection* seqCollection);

Histogram coverageHistogram(const ISequenceCollection& c);
void setCoverageParameters(const Histogram& h);

/* Erosion. Remove k-mer from the ends of blunt contigs. */
size_t erodeEnds(ISequenceCollection* seqCollection);
size_t erode(ISequenceCollection* c,
		const ISequenceCollection::value_type& seq);
size_t getNumEroded();

size_t removeMarked(ISequenceCollection* pSC);

// Check whether a sequence can be trimmed
SeqContiguity checkSeqContiguity(
		const ISequenceCollection::value_type& seq,
		extDirection& outDir, bool considerMarks = false);

// process a terminated branch for trimming
bool processTerminatedBranchTrim(
		ISequenceCollection* seqCollection, BranchRecord& branch);

bool extendBranch(BranchRecord& branch, Kmer& kmer, SeqExt ext);

// Process the extensions of the current sequence for trimming
bool processLinearExtensionForBranch(BranchRecord& branch,
		Kmer& currSeq, ExtensionRecord extensions, int multiplicity,
		unsigned maxLength, bool addKmer = true);

/** Populate the branch group with the initial extensions to this
 * sequence. */
void initiateBranchGroup(BranchGroup& group, const Kmer& seq,
		const SeqExt& extension);

// process an a branch group extension
bool processBranchGroupExtension(BranchGroup& group,
		size_t branchIndex, const Kmer& seq,
		ExtensionRecord extensions, int multiplicity,
		unsigned maxLength);

void openBubbleFile(std::ofstream& out);
void writeBubble(std::ostream& out, const BranchGroup& group,
		unsigned id);
void collapseJoinedBranches(
		ISequenceCollection* seqCollection, BranchGroup& group);

/* Split the remaining ambiguous nodes to allow for a non-redundant
 * assembly. Remove extensions to/from ambiguous sequences to avoid
 * generating redundant/wrong contigs.
 */
size_t markAmbiguous(ISequenceCollection* seqCollection);
size_t splitAmbiguous(ISequenceCollection* seqCollection);

size_t assembleContig(ISequenceCollection* seqCollection,
		FastaWriter* writer, BranchRecord& branch, unsigned id);

void removeSequenceAndExtensions(ISequenceCollection* seqCollection,
		const ISequenceCollection::value_type& seq);
void removeExtensionsToSequence(ISequenceCollection* seqCollection,
		const ISequenceCollection::value_type& seq, extDirection dir);

void generateSequencesFromExtension(const Kmer& currSeq,
		extDirection dir, SeqExt extension,
		std::vector<Kmer>& outseqs);

/* Non-distributed graph algorithms. */

void performTrim(SequenceCollectionHash* seqCollection);
size_t popBubbles(SequenceCollectionHash* pSC, std::ostream& out);
size_t assemble(SequenceCollectionHash* seqCollection,
		FastaWriter* fileWriter = NULL);

};

#endif
