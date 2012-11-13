#include "MessageBuffer.h"
#include "Common/Options.h"
#include <iostream>

using namespace std;

MessageBuffer::MessageBuffer()
	: m_msgQueues(opt::numProc)
{
	for (unsigned i = 0; i < m_msgQueues.size(); i++)
		m_msgQueues[i].reserve(MAX_MESSAGES);
}

void MessageBuffer::sendSeqAddMessage(int nodeID, const Kmer& seq)
{
	queueMessage(nodeID, new SeqAddMessage(seq), SM_BUFFERED);
}

void MessageBuffer::sendSeqRemoveMessage(int nodeID, const Kmer& seq)
{
	queueMessage(nodeID, new SeqRemoveMessage(seq), SM_BUFFERED);
}

// Send a set flag message
void MessageBuffer::sendSetFlagMessage(int nodeID,
		const Kmer& seq, SeqFlag flag)
{
	queueMessage(nodeID, new SetFlagMessage(seq, flag), SM_BUFFERED);
}

// Send a remove extension message
void MessageBuffer::sendRemoveExtension(int nodeID,
		const Kmer& seq, extDirection dir, SeqExt ext)
{
	queueMessage(nodeID, new RemoveExtensionMessage(seq, dir, ext),
			SM_BUFFERED);
}

// Send a sequence data request
void MessageBuffer::sendSeqDataRequest(int nodeID,
		IDType group, IDType id, const Kmer& seq)
{
	queueMessage(nodeID,
			new SeqDataRequest(seq, group, id), SM_IMMEDIATE);
}

// Send a sequence data response
void MessageBuffer::sendSeqDataResponse(int nodeID,
		IDType group, IDType id, const Kmer& seq,
		ExtensionRecord extRec, int multiplicity)
{
	queueMessage(nodeID,
			new SeqDataResponse(seq, group, id, extRec, multiplicity),
			SM_IMMEDIATE);
}

// Send a set base message
void MessageBuffer::sendSetBaseExtension(int nodeID,
		const Kmer& seq, extDirection dir, uint8_t base)
{
	queueMessage(nodeID,
			new SetBaseMessage(seq, dir, base), SM_BUFFERED);
}

void MessageBuffer::queueMessage(
		int nodeID, Message* message, SendMode mode)
{
	if (opt::verbose >= 9)
		cout << opt::rank << " to " << nodeID << ": " << *message;
	m_msgQueues[nodeID].push_back(message);
	checkQueueForSend(nodeID, mode);
}

void MessageBuffer::checkQueueForSend(int nodeID, SendMode mode)
{
	size_t numMsgs = m_msgQueues[nodeID].size();
	// check if the messages should be sent
	if ((numMsgs == MAX_MESSAGES || mode == SM_IMMEDIATE)
			&& numMsgs > 0) {
		// Calculate the total size of the message
		size_t totalSize = 0;
		for(size_t i = 0; i < numMsgs; i++)
		{
			totalSize += m_msgQueues[nodeID][i]->getNetworkSize();
		}

		// Generate a buffer for all the messages
		char* buffer = new char[totalSize];

		// Copy the messages into the buffer
		size_t offset = 0;
		for(size_t i = 0; i < numMsgs; i++)
			offset += m_msgQueues[nodeID][i]->serialize(
					buffer + offset);

		assert(offset == totalSize);
		sendBufferedMessage(nodeID, buffer, totalSize);

		delete [] buffer;
		clearQueue(nodeID);

		m_txPackets++;
		m_txMessages += numMsgs;
		m_txBytes += totalSize;
	}
}

// Clear a queue of messages
void MessageBuffer::clearQueue(int nodeID)
{
	size_t numMsgs = m_msgQueues[nodeID].size();
	for(size_t i = 0; i < numMsgs; i++)
	{
		// Delete the messages
		delete m_msgQueues[nodeID][i];
		m_msgQueues[nodeID][i] = 0;
	}
	m_msgQueues[nodeID].clear();
}

// Flush the message buffer by sending all messages that are queued
void MessageBuffer::flush()
{
	// Send all messages in all queues
	for(size_t id = 0; id < m_msgQueues.size(); ++id)
	{
		// force the queue to send any pending messages
		checkQueueForSend(id, SM_IMMEDIATE);
	}
}

// Check if all the queues are empty
bool MessageBuffer::transmitBufferEmpty() const
{
	bool isEmpty = true;
	for (MessageQueues::const_iterator it = m_msgQueues.begin();
			it != m_msgQueues.end(); ++it) {
		if (!it->empty()) {
			cerr
				<< opt::rank << ": error: tx buffer should be empty: "
				<< it->size() << " messages from "
				<< opt::rank << " to " << it - m_msgQueues.begin()
				<< '\n';
			for (MsgBuffer::const_iterator j = it->begin();
					j != it->end(); ++j)
				cerr << **j << '\n';
			isEmpty = false;
		}
	}
	return isEmpty;
}
