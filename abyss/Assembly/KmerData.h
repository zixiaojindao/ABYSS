#ifndef KMERDATA_H
#define KMERDATA_H 1

#include "Sense.h"
#include "SeqExt.h"
#include <cassert>
#include <stdint.h>
#include <ostream>

enum SeqFlag
{
	SF_MARK_SENSE = 0x1,
	SF_MARK_ANTISENSE = 0x2,
	SF_DELETE = 0x4,
};

static inline SeqFlag complement(SeqFlag flag)
{
	unsigned out = 0;
	if (flag & SF_MARK_SENSE)
		out |= SF_MARK_ANTISENSE;
	if (flag & SF_MARK_ANTISENSE)
		out |= SF_MARK_SENSE;
	if (flag & SF_DELETE)
		out |= SF_DELETE;
	return SeqFlag(out);
}

/** A pair of SeqExt; one for out edges and one for in edges. */
struct ExtensionRecord
{
	SeqExt dir[2];
	ExtensionRecord operator ~() const
	{
		ExtensionRecord o;
		o.dir[SENSE] = dir[ANTISENSE].complement();
		o.dir[ANTISENSE] = dir[SENSE].complement();
		return o;
	}
};

/**
 * The data associated with a Kmer, including its coverage, flags
 * and adjacent Kmer.
 */
class KmerData
{
/** Maximum value of k-mer coverage. */
#define COVERAGE_MAX 32767U

  public:
	KmerData() : m_flags(0)
	{
		m_multiplicity[SENSE] = 1;
		m_multiplicity[ANTISENSE] = 0;
	}

	KmerData(extDirection dir, unsigned multiplicity) : m_flags(0)
	{
		assert(multiplicity <= COVERAGE_MAX);
		m_multiplicity[dir] = multiplicity;
		m_multiplicity[!dir] = 0;
	}

	KmerData(unsigned multiplicity, ExtensionRecord ext)
		: m_flags(0), m_ext(ext)
	{
		setMultiplicity(multiplicity);
	}

	unsigned getMultiplicity(extDirection dir) const
	{
		return m_multiplicity[dir];
	}

	unsigned getMultiplicity() const
	{
		return m_multiplicity[SENSE] + m_multiplicity[ANTISENSE];
	}

	void addMultiplicity(extDirection dir, unsigned n = 1)
	{
		m_multiplicity[dir]
			= std::min(m_multiplicity[dir] + n, COVERAGE_MAX);
		assert(m_multiplicity[dir] > 0);
	}

	/** Set the multiplicity (not strand specific). */
	void setMultiplicity(unsigned multiplicity)
	{
		assert(multiplicity <= 2*COVERAGE_MAX);
		// Split the multiplicity over both senses.
		m_multiplicity[SENSE] = (multiplicity + 1) / 2;
		m_multiplicity[ANTISENSE] = multiplicity / 2;
		assert(getMultiplicity() == multiplicity);
	}

	void setFlag(SeqFlag flag) { m_flags |= flag; }
	bool isFlagSet(SeqFlag flag) const { return m_flags & flag; }
	void clearFlag(SeqFlag flag) { m_flags &= ~flag; }

	/** Return true if the specified sequence is deleted. */
	bool deleted() const { return isFlagSet(SF_DELETE); }

	/** Return true if the specified sequence is marked. */
	bool marked(extDirection sense) const
	{
		return isFlagSet(sense == SENSE
				? SF_MARK_SENSE : SF_MARK_ANTISENSE);
	}

	/** Return true if the specified sequence is marked. */
	bool marked() const
	{
		return isFlagSet(SeqFlag(SF_MARK_SENSE | SF_MARK_ANTISENSE));
	}

	ExtensionRecord extension() const { return m_ext; }

	SeqExt getExtension(extDirection dir) const
	{
		return m_ext.dir[dir];
	}

	void setBaseExtension(extDirection dir, uint8_t base)
	{
		m_ext.dir[dir].setBase(base);
	}

	void removeExtension(extDirection dir, SeqExt ext)
	{
		m_ext.dir[dir].clear(ext);
	}

	bool hasExtension(extDirection dir) const
	{
		return m_ext.dir[dir].hasExtension();
	}

	bool isAmbiguous(extDirection dir) const
	{
		return m_ext.dir[dir].isAmbiguous();
	}

	/** Return the complement of this data. */
	KmerData operator~() const
	{
		KmerData o;
		o.m_flags = complement(SeqFlag(m_flags));
		o.m_multiplicity[0] = m_multiplicity[1];
		o.m_multiplicity[1] = m_multiplicity[0];
		o.m_ext = ~m_ext;
		return o;
	}

	friend std::ostream& operator<<(
			std::ostream& out, const KmerData& o)
	{
		return out << "C="
			<< o.m_multiplicity[0] + o.m_multiplicity[1];
	}

  protected:
	uint8_t m_flags;
	uint16_t m_multiplicity[2];
	ExtensionRecord m_ext;
};

#endif
