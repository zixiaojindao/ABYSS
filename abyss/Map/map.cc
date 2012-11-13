#include "BitUtil.h"
#include "DataLayer/Options.h"
#include "FMIndex.h"
#include "FastaIndex.h"
#include "FastaInterleave.h"
#include "FastaReader.h"
#include "IOUtil.h"
#include "MemoryUtil.h"
#include "SAM.h"
#include "StringUtil.h"
#include "Uncompress.h"
#include <algorithm>
#include <cassert>
#include <cctype> // for toupper
#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <sstream>
#include <stdint.h>
#include <string>
#include <utility>
#if _OPENMP
# include <omp.h>
#endif

using namespace std;

#define PROGRAM "abyss-map"

static const char VERSION_MESSAGE[] =
PROGRAM " (" PACKAGE_NAME ") " VERSION "\n"
"Written by Shaun Jackman.\n"
"\n"
"Copyright 2012 Canada's Michael Smith Genome Science Centre\n";

static const char USAGE_MESSAGE[] =
"Usage: " PROGRAM " [OPTION]... QUERY... TARGET\n"
"Map the sequences of the files QUERY to those of the file TARGET.\n"
"The index files TARGET.fai and TARGET.fm will be used if present.\n"
"\n"
"  -l, --min-align=N       find matches at least N bp [1]\n"
"  -j, --threads=N         use N parallel threads [1]\n"
"  -s, --sample=N          sample the suffix array [1]\n"
"  -d, --dup               identify and print duplicate sequence\n"
"                          IDs between QUERY and TARGET\n"
"  -v, --verbose           display verbose output\n"
"      --help              display this help and exit\n"
"      --version           output version information and exit\n"
"\n"
"Report bugs to <" PACKAGE_BUGREPORT ">.\n";

namespace opt {
	/** Find matches at least k bp. */
	static unsigned k;

	/** Sample the suffix array. */
	static unsigned sampleSA;

	/** The number of parallel threads. */
	static unsigned threads = 1;

	/** Identify duplicate and subsumed sequences. */
	static bool dup = false;

	/** Verbose output. */
	static int verbose;
}

static const char shortopts[] = "j:k:l:s:dv";

enum { OPT_HELP = 1, OPT_VERSION };

static const struct option longopts[] = {
	{ "sample", required_argument, NULL, 's' },
	{ "min-align", required_argument, NULL, 'l' },
	{ "dup", no_argument, NULL, 'd' },
	{ "threads", required_argument, NULL, 'j' },
	{ "verbose", no_argument, NULL, 'v' },
	{ "help", no_argument, NULL, OPT_HELP },
	{ "version", no_argument, NULL, OPT_VERSION },
	{ NULL, 0, NULL, 0 }
};

/** Counts. */
static struct {
	unsigned unique;
	unsigned multimapped;
	unsigned unmapped;
} g_count;

typedef FMIndex::Match Match;

/** Return a SAM record of the specified match. */
static SAMRecord toSAM(const FastaIndex& faIndex,
		const FMIndex& fmIndex, const Match& m, bool rc,
		unsigned qlength)
{
	SAMRecord a;
	if (m.size() == 0) {
		// No hit.
		a.rname = "*";
		a.pos = -1;
		a.flag = SAMAlignment::FUNMAP;
		a.mapq = 0;
		a.cigar = "*";
	} else {
		FastaIndex::SeqPos seqPos = faIndex[fmIndex[m.l]];
		a.rname = seqPos.get<0>().id;
		a.pos = seqPos.get<1>();
		a.flag = rc ? SAMAlignment::FREVERSE : 0;

		// Set the mapq to the alignment score.
		assert(m.qstart < m.qend);
		unsigned matches = m.qend - m.qstart;
		a.mapq = m.size() > 1 ? 0 : min(matches, 255U);

		ostringstream ss;
		if (m.qstart > 0)
			ss << m.qstart << 'S';
		ss << matches << 'M';
		if (m.qend < qlength)
			ss << qlength - m.qend << 'S';
		a.cigar = ss.str();
	}
	a.mrnm = "*";
	a.mpos = -1;
	a.isize = 0;
	return a;
}

/** Return the position of the current contig. */
static size_t getMyPos(const Match& m, const FastaIndex& faIndex,
		const FMIndex& fmIndex, const string& id)
{
	for (size_t i = m.l; i < m.u; i++) {
		if (faIndex[fmIndex[i]].get<0>().id == id)
			return fmIndex[i];
	}
	return fmIndex[m.l];
}

/** Return the earlies position of all contigs in m. */
static size_t getMinPos(const Match& m, size_t maxLen,
		const FastaIndex& faIndex, const FMIndex& fmIndex)
{
	size_t minPos = numeric_limits<size_t>::max();
	for (size_t i = m.l; i < m.u; i++) {
		size_t pos = fmIndex[i];
		if (faIndex[pos].get<0>().size == maxLen && pos < minPos)
			minPos = fmIndex[i];
	}
	return minPos;
}

/** Return the largest length of all contig in m. */
static size_t getMaxLen(const Match& m, const FastaIndex& faIndex,
		const FMIndex& fmIndex)
{
	size_t maxLen = 0;
	for (size_t i = m.l; i < m.u; i++) {
		size_t len = faIndex[fmIndex[i]].get<0>().size;
		if (len > maxLen)
			maxLen = len;
	}
	return maxLen;
}

/** Print the current contig id if it is not the lartest and earliest
 * contig in m. */
static void printDuplicates(const Match& m, const Match& rcm,
		const FastaIndex& faIndex, const FMIndex& fmIndex,
		const FastqRecord& rec)
{
	size_t myLen = m.qspan();
	size_t maxLen = max(getMaxLen(m, faIndex, fmIndex),
			getMaxLen(rcm, faIndex, fmIndex));
	if (myLen < maxLen) {
#pragma omp atomic
		g_count.multimapped++;
#pragma omp critical(cout)
		{
			cout << rec.id << '\n';
			assert_good(cout, "stdout");
		}
		return;
	}
	size_t myPos = getMyPos(m, faIndex, fmIndex, rec.id);
	size_t minPos = min(getMinPos(m, maxLen, faIndex, fmIndex),
			getMinPos(rcm, maxLen, faIndex, fmIndex));
	if (myPos > minPos) {
#pragma omp atomic
		g_count.multimapped++;
#pragma omp critical(cout)
		{
			cout << rec.id << '\n';
			assert_good(cout, "stdout");
		}
	}
#pragma omp atomic
	g_count.unique++;
	return;
}


/** Return the mapping of the specified sequence. */
static void find(const FastaIndex& faIndex, const FMIndex& fmIndex,
		const FastqRecord& rec)
{
	if (rec.seq.empty()) {
		cerr << PROGRAM ": error: "
			"the sequence `" << rec.id << "' is empty\n";
		exit(EXIT_FAILURE);
	}

	Match m = fmIndex.find(rec.seq,
			opt::dup ? rec.seq.length() : opt::k);

	string rcqseq = reverseComplement(rec.seq);
	Match rcm = fmIndex.find(rcqseq,
			opt::dup ? rcqseq.length() : m.qspan());

	if (opt::dup) {
		printDuplicates(m, rcm, faIndex, fmIndex, rec);
		return;
	}

	bool rc = rcm.qspan() > m.qspan();

	SAMRecord sam = toSAM(faIndex, fmIndex, rc ? rcm : m, rc,
			rec.seq.size());
	sam.qname = rec.id;
#if SAM_SEQ_QUAL
	sam.seq = rc ? rcqseq : rec.seq;
	sam.qual = rec.qual.empty() ? "*" : rec.qual;
	if (rc)
		reverse(sam.qual.begin(), sam.qual.end());
#endif

	if (m.qstart == rec.seq.size() - rcm.qend
			&& m.qspan() == rcm.qspan()) {
		// This matching sequence maps to both strands.
		sam.mapq = 0;
	}

#pragma omp critical(cout)
	{
		cout << sam << '\n';
		assert_good(cout, "stdout");
	}

	if (sam.isUnmapped())
#pragma omp atomic
		g_count.unmapped++;
	else if (sam.mapq == 0)
#pragma omp atomic
		g_count.multimapped++;
	else
#pragma omp atomic
		g_count.unique++;
}

/** Map the sequences of the specified file. */
static void find(const FastaIndex& faIndex, const FMIndex& fmIndex,
		FastaInterleave& in)
{
#pragma omp parallel
	for (FastqRecord rec;;) {
		bool good;
#pragma omp critical(in)
		good = in >> rec;
		if (good)
			find(faIndex, fmIndex, rec);
		else
			break;
	}
	assert(in.eof());
}

/** Build an FM index of the specified file. */
static void buildFMIndex(FMIndex& fm, const char* path)
{
	if (opt::verbose > 0)
		std::cerr << "Reading `" << path << "'...\n";
	std::vector<FMIndex::value_type> s;
	readFile(path, s);

	size_t MAX_SIZE = numeric_limits<FMIndex::sais_size_type>::max();
	if (s.size() > MAX_SIZE) {
		std::cerr << PROGRAM << ": `" << path << "', "
			<< toSI(s.size())
			<< "B, must be smaller than "
			<< toSI(MAX_SIZE) << "B\n";
		exit(EXIT_FAILURE);
	}

	transform(s.begin(), s.end(), s.begin(), ::toupper);
	fm.setAlphabet("-ACGT");
	fm.assign(s.begin(), s.end());
}

/** Return the size of the specified file. */
static streampos fileSize(const string& path)
{
	std::ifstream in(path.c_str());
	assert_good(in, path);
	in.seekg(0, std::ios::end);
	assert_good(in, path);
	return in.tellg();
}

/** Check that the indexes are up to date. */
static void checkIndexes(const string& path,
		const FMIndex& fmIndex, const FastaIndex& faIndex)
{
	size_t fastaFileSize = fileSize(path);
	if (fmIndex.size() != fastaFileSize) {
		cerr << PROGRAM ": `" << path << "': "
			"The size of the FM-index, "
			<< fmIndex.size()
			<< " B, does not match the size of the FASTA file, "
			<< fastaFileSize << " B. The index is likely stale.\n";
		exit(EXIT_FAILURE);
	}
	if (faIndex.fileSize() != fastaFileSize) {
		cerr << PROGRAM ": `" << path << "': "
			"The size of the FASTA index, "
			<< faIndex.fileSize()
			<< " B, does not match the size of the FASTA file, "
			<< fastaFileSize << " B. The index is likely stale.\n";
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char** argv)
{
	checkPopcnt();

	string commandLine;
	{
		ostringstream ss;
		char** last = argv + argc - 1;
		copy(argv, last, ostream_iterator<const char *>(ss, " "));
		ss << *last;
		commandLine = ss.str();
	}

	bool die = false;
	for (int c; (c = getopt_long(argc, argv,
					shortopts, longopts, NULL)) != -1;) {
		istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
			case '?': die = true; break;
			case 'j': arg >> opt::threads; break;
			case 'k': case 'l':
				arg >> opt::k;
				break;
			case 's': arg >> opt::sampleSA; break;
			case 'd': opt::dup = true; break;
			case 'v': opt::verbose++; break;
			case OPT_HELP:
				cout << USAGE_MESSAGE;
				exit(EXIT_SUCCESS);
			case OPT_VERSION:
				cout << VERSION_MESSAGE;
				exit(EXIT_SUCCESS);
		}
		if (optarg != NULL && !arg.eof()) {
			cerr << PROGRAM ": invalid option: `-"
				<< (char)c << optarg << "'\n";
			exit(EXIT_FAILURE);
		}
	}

	if (argc - optind < 2) {
		cerr << PROGRAM ": missing arguments\n";
		die = true;
	}

	if (die) {
		cerr << "Try `" << PROGRAM
			<< " --help' for more information.\n";
		exit(EXIT_FAILURE);
	}

#if _OPENMP
	if (opt::threads > 0)
		omp_set_num_threads(opt::threads);
#endif

	const char* targetFile(argv[--argc]);
	ostringstream ss;
	ss << targetFile << ".fm";
	string fmPath(ss.str());
	ss.str("");
	ss << targetFile << ".fai";
	string faiPath(ss.str());

	ifstream in;

	// Read the FASTA index.
	FastaIndex faIndex;
	in.open(faiPath.c_str());
	if (in) {
		if (opt::verbose > 0)
			cerr << "Reading `" << faiPath << "'...\n";
		in >> faIndex;
		assert(in.eof());
		in.close();
	} else {
		if (opt::verbose > 0)
			cerr << "Reading `" << targetFile << "'...\n";
		faIndex.index(targetFile);
	}
	if (opt::verbose > 0) {
		ssize_t bytes = getMemoryUsage();
		if (bytes > 0)
			cerr << "Using " << toSI(bytes) << "B of memory and "
				<< setprecision(3) << (float)bytes / faIndex.size()
				<< " B/sequence.\n";
	}

	// Read the FM index.
	FMIndex fmIndex;
	in.open(fmPath.c_str());
	if (in) {
		if (opt::verbose > 0)
			cerr << "Reading `" << fmPath << "'...\n";
		assert_good(in, fmPath);
		in >> fmIndex;
		assert_good(in, fmPath);
		in.close();
	} else
		buildFMIndex(fmIndex, targetFile);
	if (opt::sampleSA > 1)
		fmIndex.sampleSA(opt::sampleSA);

	if (opt::verbose > 0) {
		size_t bp = fmIndex.size();
		cerr << "Read " << toSI(bp) << "B in "
			<< faIndex.size() << " contigs.\n";
		ssize_t bytes = getMemoryUsage();
		if (bytes > 0)
			cerr << "Using " << toSI(bytes) << "B of memory and "
				<< setprecision(3) << (float)bytes / bp << " B/bp.\n";
	}

	// Check that the indexes are up to date.
	checkIndexes(targetFile, fmIndex, faIndex);

	if (!opt::dup) {
		// Write the SAM header.
		cout << "@HD\tVN:1.4\n"
			"@PG\tID:" PROGRAM "\tPN:" PROGRAM "\tVN:" VERSION "\t"
			"CL:" << commandLine << '\n';
		faIndex.writeSAMHeader(cout);
		cout.flush();
		assert_good(cout, "stdout");
	} else if (opt::verbose > 0)
		cerr << "Identifying duplicates.\n";

	opt::chastityFilter = false;
	opt::trimMasked = false;
	FastaInterleave fa(argv + optind, argv + argc,
			FastaReader::FOLD_CASE);
	find(faIndex, fmIndex, fa);

	if (opt::verbose > 0) {
		size_t unique = g_count.unique;
		size_t mapped = unique + g_count.multimapped;
		size_t total = mapped + g_count.unmapped;
		cerr << "Mapped " << mapped << " of " << total << " reads ("
			<< (float)100 * mapped / total << "%)\n"
			<< "Mapped " << unique << " of " << total
			<< " reads uniquely (" << (float)100 * unique / total
			<< "%)\n";
	}

	cout.flush();
	assert_good(cout, "stdout");
	return 0;
}
