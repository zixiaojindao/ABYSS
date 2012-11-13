#ifndef NETWORKSEQUENCECOLLECTION_H
#define NETWORKSEQUENCECOLLECTION_H 1

#include "SequenceCollection.h"
#include "BranchGroup.h"
#include "BranchRecord.h"
#include "CommLayer.h"
#include "FastaWriter.h"
#include "MessageBuffer.h"
#include "Timer.h"
#include <ostream>
#include <set>
#include <utility>

enum NetworkAssemblyState
{
	NAS_LOADING, // loading sequences
	NAS_LOAD_COMPLETE, // loading is complete
	NAS_GEN_ADJ, // generating the sequence data
	NAS_ADJ_COMPLETE, // adjacency generation is complete
	NAS_ERODE, // erode the branch ends one sequence at a time
	NAS_ERODE_WAITING,
	NAS_ERODE_COMPLETE,
	NAS_TRIM, // trimming the data
	NAS_REMOVE_MARKED, // remove marked sequences
	NAS_COVERAGE, // remove low-coverage contigs
	NAS_COVERAGE_COMPLETE,
	NAS_DISCOVER_BUBBLES, // discover read errors/SNPs
	NAS_POPBUBBLE, // remove read errors/SNPs
	NAS_MARK_AMBIGUOUS, // mark ambiguous branches
	NAS_SPLIT_AMBIGUOUS, // split ambiguous branches
	NAS_CLEAR_FLAGS, // clear the flags
	NAS_ASSEMBLE, // assembling the data
	NAS_ASSEMBLE_COMPLETE, // assembling is complete
	NAS_WAITING, // non-control process is waiting
	NAS_DONE // finished, clean up and exit
};

typedef std::map<uint64_t, BranchGroup> BranchGroupMap;

/** A distributed map of Kmer to KmerData. */
class NetworkSequenceCollection : public ISequenceCollection
{
	public:
		NetworkSequenceCollection()
			: m_state(NAS_WAITING), m_trimStep(0),
			m_numPopped(0), m_numAssembled(0) { }

		size_t performNetworkTrim(ISequenceCollection* seqCollection);

		size_t performNetworkDiscoverBubbles(ISequenceCollection* c);
		size_t performNetworkPopBubbles(std::ostream& out);

		size_t controlErode();
		size_t controlTrimRound(unsigned trimLen);
		void controlTrim(unsigned start = 1);
		size_t controlRemoveMarked();
		void controlCoverage();
		size_t controlDiscoverBubbles();
		size_t controlPopBubbles(std::ostream& out);
		size_t controlMarkAmbiguous();
		size_t controlSplitAmbiguous();
		size_t controlSplit();

		// Perform a network assembly
		std::pair<size_t, size_t> performNetworkAssembly(
				ISequenceCollection* seqCollection,
				FastaWriter* fileWriter = NULL);

		void add(const Kmer& seq, unsigned coverage = 1);
		void remove(const Kmer& seq);
		void setFlag(const Kmer& seq, SeqFlag flag);

		/** Return true if this container is empty. */
		bool empty() const { return m_data.empty(); }

		void printLoad() const { m_data.printLoad(); }

		void removeExtension(const Kmer& seq, extDirection dir,
				SeqExt ext);
		bool setBaseExtension(const Kmer& seq, extDirection dir,
				uint8_t base);

		// Receive and dispatch packets.
		size_t pumpNetwork();
		size_t pumpFlushReduce();

		void completeOperation();

		// run the assembly
		void run();

		// run the assembly from the controller's point of view
		void runControl();

		// test if the checkpoint has been reached
		bool checkpointReached() const;
		bool checkpointReached(unsigned numRequired) const;

		void handle(int senderID, const SeqAddMessage& message);
		void handle(int senderID, const SeqRemoveMessage& message);
		void handle(int senderID, const SetBaseMessage& message);
		void handle(int senderID, const SetFlagMessage& message);
		void handle(int senderID, const RemoveExtensionMessage& m);
		void handle(int senderID, const SeqDataRequest& message);
		void handle(int senderID, const SeqDataResponse& message);

		// Observer pattern, not implemented.
		void attach(SeqObserver f) { (void)f; }
		void detach(SeqObserver f) { (void)f; }

		/** Load this collection from disk. */
		void load(const char *path)
		{
			m_data.load(path);
		}

		/** Indicate that this is a colour-space collection. */
		void setColourSpace(bool flag)
		{
			m_data.setColourSpace(flag);
			m_comm.broadcast(flag);
		}

		iterator begin() { return m_data.begin(); }
		const_iterator begin() const { return m_data.begin(); }
		iterator end() { return m_data.end(); }
		const_iterator end() const { return m_data.end(); }

	private:
		// Observer pattern
		void notify(const Kmer& seq);

		void loadSequences();

		std::pair<size_t, size_t> processBranchesAssembly(
				ISequenceCollection* seqCollection,
				FastaWriter* fileWriter, unsigned currContigID);
		size_t processBranchesTrim();
		bool processBranchesDiscoverBubbles();

		void generateExtensionRequest(
				uint64_t groupID, uint64_t branchID, const Kmer& seq);
		void generateExtensionRequests(uint64_t groupID,
				BranchGroup::const_iterator first,
				BranchGroup::const_iterator last);
		void processSequenceExtension(
				uint64_t groupID, uint64_t branchID, const Kmer& seq,
				const ExtensionRecord& extRec, int multiplicity);
		void processLinearSequenceExtension(
				uint64_t groupID, uint64_t branchID, const Kmer& seq,
				const ExtensionRecord& extRec, int multiplicity,
				unsigned maxLength);
		void processSequenceExtensionPop(
				uint64_t groupID, uint64_t branchID, const Kmer& seq,
				const ExtensionRecord& extRec, int multiplicity,
				unsigned maxLength);

		void assembleContig(ISequenceCollection* seqCollection,
				FastaWriter* fileWriter,
				BranchRecord& branch, unsigned id);

		// Check if a branch is redundant with a previously output
		// branch.
		bool isBranchRedundant(const BranchRecord& branch);

		void parseControlMessage(int source);

		bool isLocal(const Kmer& seq) const;
		int computeNodeID(const Kmer& seq) const;

		void EndState();

		// Set the state of the network assembly
		void SetState(NetworkAssemblyState newState);

		SequenceCollectionHash m_data;

		// The communications layer implements the functions over the
		// network.
		MessageBuffer m_comm;

		// The number of nodes in the network
		unsigned m_numDataNodes;

		// the state of the assembly
		NetworkAssemblyState m_state;

		// The number of processes that have sent a checkpoint reached
		// message, this is used by the control process to determine
		// the state flow.
		unsigned m_numReachedCheckpoint;

		/** The sum of the values returned by the slave nodes in their
		 * checkpoint messages.
		 */
		size_t m_checkpointSum;

		// the number of bases of adjacency set
		size_t m_numBasesAdjSet;

		// the current length to trim on (comes from the control node)
		unsigned m_trimStep;

		/** The number of low-coverage contigs removed. */
		size_t m_lowCoverageContigs;

		/** The number of low-coverage k-mer removed. */
		size_t m_lowCoverageKmer;

		/** The number of bubbles popped so far. */
		size_t m_numPopped;

		// the number of sequences assembled so far
		size_t m_numAssembled;

		// The current branches that are active
		BranchGroupMap m_activeBranchGroups;

		/** Bubbles, which are branch groups that have joined. */
		BranchGroupMap m_bubbles;

		// List of IDs of finished groups, used for sanity checking
		// during bubble popping.
		std::set<uint64_t> m_finishedGroups;

		static const size_t MAX_ACTIVE = 50;
		static const size_t LOW_ACTIVE = 10;
};

#endif
