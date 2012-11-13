/**
 * Identify and pop simple bubbles.
 * Written by Shaun Jackman <sjackman@bcgsc.ca>.
 */

#include "dialign.h"
#include "config.h"
#include "Common/Options.h"
#include "ConstString.h"
#include "ContigPath.h"
#include "ContigProperties.h"
#include "FastaReader.h"
#include "IOUtil.h"
#include "Sequence.h"
#include "Uncompress.h"
#include "alignGlobal.h"
#include "Graph/ContigGraph.h"
#include "Graph/ContigGraphAlgorithms.h"
#include "Graph/DepthFirstSearch.h"
#include "Graph/DirectedGraph.h"
#include "Graph/GraphIO.h"
#include "Graph/GraphUtil.h"
#include "Graph/PopBubbles.h"
#include <boost/lambda/bind.hpp>
#include <boost/lambda/lambda.hpp>
#include <algorithm>
#include <climits> // for UINT_MAX
#include <fstream>
#include <functional>
#include <getopt.h>
#include <map>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#if _OPENMP
# include <omp.h>
#endif

using namespace std;
using namespace boost::lambda;
using boost::tie;
#if !__GXX_EXPERIMENTAL_CXX0X__
using boost::cref;
#endif

#define PROGRAM "PopBubbles"

static const char VERSION_MESSAGE[] =
PROGRAM " (" PACKAGE_NAME ") " VERSION "\n"
"Written by Shaun Jackman.\n"
"\n"
"Copyright 2012 Canada's Michael Smith Genome Science Centre\n";

static const char USAGE_MESSAGE[] =
"Usage: " PROGRAM " [OPTION]... FASTA ADJ\n"
"Identify and pop simple bubbles.\n"
"  FASTA  contigs in FASTA format\n"
"  ADJ    contig adjacency graph\n"
"\n"
"  -k, --kmer=N          k-mer size\n"
"  -a, --branches=N      maximum number of branches, default: 2\n"
"  -b, --bubble-length=N pop bubbles shorter than N bp\n"
"                        default is 10000\n"
"  -p, --identity=REAL   minimum identity, default: 0.9\n"
"  -c, --coverage=REAL   remove contigs with mean k-mer coverage\n"
"                        less than this threshold [0]\n"
"      --scaffold        scaffold over bubbles that have\n"
"                        insufficient identity\n"
"      --no-scaffold     disable scaffolding [default]\n"
"  -g, --graph=FILE      write the contig adjacency graph to FILE\n"
"      --dot             output bubbles in dot format\n"
"  -j, --threads=N       use N parallel threads [1]\n"
"  -v, --verbose         display verbose output\n"
"      --help            display this help and exit\n"
"      --version         output version information and exit\n"
"\n"
"Report bugs to <" PACKAGE_BUGREPORT ">.\n";

namespace opt {
	unsigned k; // used by ContigProperties

	/** Maximum number of branches. */
	static unsigned maxBranches = 2;

	/** Pop bubbles shorter than this threshold. */
	static unsigned maxLength = 10000;

	/** Minimum identity. */
	static float identity = 0.9;

	/** Minimum mean k-mer coverage. */
	static float minCoverage;

	/** Scaffold over bubbles that have insufficient identity. */
	static int scaffold;

	/** Write the contig adjacency graph to this file. */
	static string graphPath;

	/** Output bubbles in dot format. */
	static int dot;

	int format; // used by ContigProperties

	/** Number of threads. */
	static int threads = 1;
	static int dialign_debug;
	static string dialign_score;
	static string dialign_prob;
}

static const char shortopts[] = "a:b:c:g:j:k:p:vD:M:P:";

enum { OPT_HELP = 1, OPT_VERSION };

static const struct option longopts[] = {
	{ "branches",      required_argument, NULL, 'a' },
	{ "bubble-length", required_argument, NULL, 'b' },
	{ "coverage",      required_argument, NULL, 'c' },
	{ "dot",           no_argument,       &opt::dot, 1, },
	{ "graph",         required_argument, NULL, 'g' },
	{ "kmer",          required_argument, NULL, 'k' },
	{ "identity",      required_argument, NULL, 'p' },
	{ "scaffold",      no_argument,       &opt::scaffold, 1},
	{ "no-scaffold",   no_argument,       &opt::scaffold, 0},
	{ "threads",       required_argument, NULL, 'j' },
	{ "verbose",       no_argument,       NULL, 'v' },
	{ "help",          no_argument,       NULL, OPT_HELP },
	{ "version",       no_argument,       NULL, OPT_VERSION },
	{ "dialign-d",   required_argument, NULL, 'D' },
	{ "dialign-m",   required_argument, NULL, 'M' },
	{ "dialign-p",   required_argument, NULL, 'P' },
	{ NULL, 0, NULL, 0 }
};

/** Popped branches. */
static vector<ContigID> g_popped;

/** Contig adjacency graph. */
typedef ContigGraph<DirectedGraph<ContigProperties, Distance> > Graph;
typedef Graph::vertex_descriptor vertex_descriptor;
typedef Graph::adjacency_iterator adjacency_iterator;

/** Return the distance from vertex u to v. */
static int getDistance(const Graph& g,
		vertex_descriptor u, vertex_descriptor v)
{
	typedef graph_traits<Graph>::edge_descriptor edge_descriptor;
	pair<edge_descriptor, bool> e = edge(u, v, g);
	assert(e.second);
	return g[e.first].distance;
}

struct CompareCoverage {
	const Graph& g;
	CompareCoverage(const Graph& g) : g(g) { }
	bool operator()(vertex_descriptor u, vertex_descriptor v)
	{
		return g[u].coverage > g[v].coverage;
	}
};

/** Pop the bubble between vertices v and tail. */
static void popBubble(Graph& g,
		vertex_descriptor v, vertex_descriptor tail)
{
	unsigned nbranches = g.out_degree(v);
	assert(nbranches > 1);
	assert(nbranches == g.in_degree(tail));
	vector<vertex_descriptor> sorted(nbranches);
	pair<adjacency_iterator, adjacency_iterator>
		adj = g.adjacent_vertices(v);
	copy(adj.first, adj.second, sorted.begin());
	sort(sorted.begin(), sorted.end(), CompareCoverage(g));
	if (opt::dot)
#pragma omp critical(cout)
	{
		cout << '"' << get(vertex_name, g, v) << "\" -> {";
		for (vector<vertex_descriptor>::const_iterator
				it = sorted.begin(); it != sorted.end(); ++it)
			cout << " \"" << get(vertex_name, g, *it) << '"';
		cout << " } -> \"" << get(vertex_name, g, tail) << "\"\n";
	}
#pragma omp critical(g_popped)
	transform(sorted.begin() + 1, sorted.end(),
			back_inserter(g_popped),
			mem_fun_ref(&ContigNode::contigIndex));
}

static struct {
	unsigned bubbles;
	unsigned popped;
	unsigned scaffold;
	unsigned notSimple;
	unsigned tooLong;
	unsigned tooMany;
	unsigned dissimilar;
} g_count;

/** Contig sequences. */
typedef vector<const_string> Contigs;
static Contigs g_contigs;

/** Return the sequence of vertex u. */
static string getSequence(const Graph* g, vertex_descriptor u)
{
	size_t i = get(vertex_contig_index, *g, u);
	assert(i < g_contigs.size());
	string seq(g_contigs[i]);
	return get(vertex_sense, *g, u) ? reverseComplement(seq) : seq;
}

/** Return the length of vertex v. */
static unsigned getLength(const Graph* g, vertex_descriptor v)
{
	return (*g)[v].length;
}

/** Align the specified pair of sequences.
 * @return the number of matches and size of the consensus
 */
static pair<unsigned, unsigned> alignPair(
		const string& seqa, const string& seqb)
{
	NWAlignment alignment;
	unsigned matches = alignGlobal(seqa, seqb, alignment);
	if (opt::verbose > 2)
#pragma omp critical(cerr)
		cerr << alignment;
	return make_pair(matches, alignment.size());
}

/** Align the specified sequences.
 * @return the number of matches and size of the consensus
 */
static pair<unsigned, unsigned> alignMulti(const vector<string>& seqs)
{
	string alignment;
	unsigned matches;
	string consensus = dialign(seqs, alignment, matches);
	if (opt::verbose > 2)
#pragma omp critical(cerr)
		cerr << alignment << consensus << '\n';
	return make_pair(matches, consensus.size());
}

/** Align the specified sequences.
 * @return the number of matches and size of the consensus
 */
static pair<unsigned, unsigned> align(const vector<string>& seqs)
{
	assert(seqs.size() > 1);
	if (seqs.size() == 2)
		return alignPair(seqs[0], seqs[1]);
	else
		return alignMulti(seqs);
}

/** Align the sequences of [first,last).
 * @param t the vertex to the left of the bubble
 * @param v the vertex to the right of the bubble
 * @return the identity of the global alignment
 */
template <typename It>
static float getAlignmentIdentity(const Graph& g,
		vertex_descriptor t, vertex_descriptor v,
		It first, It last)
{
	unsigned nbranches = distance(first, last);
	vector<int> inDists(nbranches);
	transform(first, last, inDists.begin(),
			bind(getDistance, cref(g), t, _1));
	vector<int> outDists(nbranches);
	transform(first, last, outDists.begin(),
			bind(getDistance, cref(g), _1, v));
	vector<int> insertLens(nbranches);
	transform(first, last, insertLens.begin(),
			bind(getDistance, cref(g), t, _1)
				+ bind(getLength, &g, _1)
				+ bind(getDistance, cref(g), _1, v));

	int max_in_overlap = -(*min_element(inDists.begin(),
			inDists.end()));
	assert(max_in_overlap >= 0);
	int max_out_overlap = -(*min_element(outDists.begin(),
			outDists.end()));
	assert(max_out_overlap >= 0);
	int min_insert_len = *min_element(insertLens.begin(),
			insertLens.end());
	int max_insert_len = *max_element(insertLens.begin(),
			insertLens.end());

	float max_identity =
		(float)(min_insert_len + max_in_overlap + max_out_overlap) /
		(max_insert_len + max_in_overlap + max_out_overlap);
	if (min_insert_len <= 0 || max_identity < opt::identity)
		return max_identity;

	vector<string> seqs(nbranches);
	transform(first, last, seqs.begin(), bind(getSequence, &g, _1));
	for (unsigned i = 0; i < seqs.size(); i++) {
		// Remove the overlapping sequence.
		int n = seqs[i].size();
		int l = -inDists[i], r = -outDists[i];
		assert(n > l + r);
		seqs[i] = seqs[i].substr(l, n - l - r);
	}

	unsigned matches, consensusSize;
	tie(matches, consensusSize) = align(seqs);
	return (float)(matches + max_in_overlap + max_out_overlap) /
		(consensusSize + max_in_overlap + max_out_overlap);
}

/** Pop the specified bubble if it is a simple bubble.
 * @return whether the bubble is popped
 */
static bool popSimpleBubble(Graph* pg, vertex_descriptor v)
{
	Graph& g = *pg;
	unsigned nbranches = g.out_degree(v);
	assert(nbranches >= 2);
	vertex_descriptor v1 = *g.adjacent_vertices(v).first;
	if (g.out_degree(v1) != 1) {
#pragma omp atomic
		g_count.notSimple++;
		return false;
	}
	vertex_descriptor tail = *g.adjacent_vertices(v1).first;
	if (v == get(vertex_complement, g, tail) // Palindrome
			|| g.in_degree(tail) != nbranches) {
#pragma omp atomic
		g_count.notSimple++;
		return false;
	}

	// Check that every branch is simple and ends at the same node.
	pair<adjacency_iterator, adjacency_iterator>
		adj = g.adjacent_vertices(v);
	for (adjacency_iterator it = adj.first; it != adj.second; ++it) {
		if (g.out_degree(*it) != 1 || g.in_degree(*it) != 1) {
#pragma omp atomic
			g_count.notSimple++;
			return false;
		}
		if (*g.adjacent_vertices(*it).first != tail) {
			// The branches do not merge back to the same node.
#pragma omp atomic
			g_count.notSimple++;
			return false;
		}
	}

	if (opt::verbose > 2)
#pragma omp critical(cerr)
	{
		cerr << "\n* " << get(vertex_name, g, v) << " ->";
		for (adjacency_iterator it = adj.first;
				it != adj.second; ++it)
			cerr << ' ' << get(vertex_name, g, *it);
		cerr << " -> " << get(vertex_name, g, tail) << '\n';
	}

	if (nbranches > opt::maxBranches) {
		// Too many branches.
#pragma omp atomic
		g_count.tooMany++;
		if (opt::verbose > 1)
#pragma omp critical(cerr)
			cerr << nbranches << " paths (too many)\n";
		return false;
	}

	vector<unsigned> lengths(nbranches);
	transform(adj.first, adj.second, lengths.begin(),
			bind1st(ptr_fun(getLength), &g));
	unsigned minLength = *min_element(lengths.begin(), lengths.end());
	unsigned maxLength = *max_element(lengths.begin(), lengths.end());
	if (maxLength >= opt::maxLength) {
		// This branch is too long.
#pragma omp atomic
		g_count.tooLong++;
		if (opt::verbose > 1)
#pragma omp critical(cerr)
			cerr << minLength << '\t' << maxLength
				<< "\t0\t(too long)\n";
		return false;
	}

	float identity = opt::identity == 0 ? 0
		: getAlignmentIdentity(g, v, tail, adj.first, adj.second);
	bool dissimilar = identity < opt::identity;
	if (opt::verbose > 1)
#pragma omp critical(cerr)
		cerr << minLength << '\t' << maxLength << '\t' << identity
			<< (dissimilar ? "\t(dissimilar)" : "") << '\n';
	if (dissimilar) {
		// Insufficient identity.
#pragma omp atomic
		g_count.dissimilar++;
		return false;
	}

#pragma omp atomic
	g_count.popped++;
	popBubble(g, v, tail);
	return true;
}

/** Add distances to a path. */
static ContigPath addDistance(const Graph& g, const ContigPath& path)
{
	ContigPath out;
	out.reserve(path.size());
	ContigNode u = path.front();
	out.push_back(u);
	for (ContigPath::const_iterator it = path.begin() + 1;
			it != path.end(); ++it) {
		ContigNode v = *it;
		int distance = getDistance(g, u, v);
		if (distance >= 0) {
			int numN = distance + opt::k - 1; // by convention
			assert(numN >= 0);
			numN = max(numN, 1);
			out.push_back(ContigNode(numN, 'N'));
		}
		out.push_back(v);
		u = v;
	}
	return out;
}

/** Return the length of the longest path through the bubble. */
static int longestPath(const Graph& g, const Bubble& topo)
{
	typedef graph_traits<Graph>::edge_descriptor E;
	typedef graph_traits<Graph>::out_edge_iterator Eit;
	typedef graph_traits<Graph>::vertex_descriptor V;

	EdgeWeightMap<Graph> weight(g);
	map<ContigNode, int> distance;
	distance[topo.front()] = 0;
	for (Bubble::const_iterator it = topo.begin();
			it != topo.end(); ++it) {
		V u = *it;
		Eit eit, elast;
		for (tie(eit, elast) = out_edges(u, g); eit != elast; ++eit) {
			E e = *eit;
			V v = target(e, g);
			distance[v] = max(distance[v], distance[u] + weight[e]);
		}
	}
	V v = topo.back();
	return distance[v] - g[v].length;
}

/** Scaffold over the bubble between vertices u and w.
 * Add an edge (u,w) with the distance property set to the length of
 * the largest branch of the bubble.
 */
static void scaffoldBubble(Graph& g, const Bubble& bubble)
{
	typedef graph_traits<Graph>::adjacency_iterator Ait;
	typedef graph_traits<Graph>::vertex_descriptor V;
	assert(opt::scaffold);
	assert(bubble.size() > 2);

	V u = bubble.front(), w = bubble.back();
	if (edge(u, w, g).second) {
		// Already scaffolded.
		return;
	}
	assert(isBubble(g, bubble.begin(), bubble.end()));

	assert(bubble.size() > 2);
	size_t n = bubble.size() - 2;
	g_popped.reserve(g_popped.size() + n);
	for (Bubble::const_iterator it = bubble.begin() + 1;
			it != bubble.end() - 1; ++it)
		g_popped.push_back(it->contigIndex());

	add_edge(u, w, max(longestPath(g, bubble), 1), g);
}

/** Pop the specified bubble if it is simple, otherwise scaffold. */
static void popOrScaffoldBubble(Graph& g, const Bubble& bubble)
{
#pragma omp atomic
	g_count.bubbles++;
	if (!popSimpleBubble(&g, bubble.front()) && opt::scaffold) {
#pragma omp atomic
		g_count.scaffold++;
		scaffoldBubble(g, bubble);
	}
}

/** Return the length of the specified vertex in k-mer. */
static unsigned getKmerLength(const ContigProperties& vp)
{
	assert(vp.length >= opt::k);
	return vp.length - opt::k + 1;
}

/** Return the mean k-mer coverage of the specified vertex. */
static float getMeanCoverage(const ContigProperties& vp)
{
	return (float)vp.coverage / getKmerLength(vp);
}

/** Remove contigs with insufficient coverage. */
static void filterGraph(Graph& g)
{
	typedef graph_traits<Graph> GTraits;
	typedef GTraits::vertex_descriptor V;
	typedef GTraits::vertex_iterator Vit;

	unsigned removedContigs = 0, removedKmer = 0;
	std::pair<Vit, Vit> urange = vertices(g);
	for (Vit uit = urange.first; uit != urange.second; ++uit) {
		V u = *uit;
		if (get(vertex_removed, g, u))
			continue;
		const ContigProperties& vp = g[u];
		if (getMeanCoverage(vp) < opt::minCoverage) {
			removedContigs++;
			removedKmer += getKmerLength(vp);
			clear_vertex(u, g);
			remove_vertex(u, g);
			g_popped.push_back(get(vertex_contig_index, g, u));
		}
	}
	if (opt::verbose > 0) {
		cerr << "Removed " << removedKmer << " k-mer in "
			<< removedContigs << " contigs with mean k-mer coverage "
			"less than " << opt::minCoverage << ".\n";
		printGraphStats(cerr, g);
	}
}

/** Remove the specified contig from the adjacency graph. */
static void removeContig(Graph* g, ContigID id)
{
	ContigNode v(id, false);
	g->clear_vertex(v);
	g->remove_vertex(v);
}

int main(int argc, char** argv)
{
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
			case 'a': arg >> opt::maxBranches; break;
			case 'b': arg >> opt::maxLength; break;
			case 'c': arg >> opt::minCoverage; break;
			case 'g': arg >> opt::graphPath; break;
			case 'j': arg >> opt::threads; break;
			case 'k': arg >> opt::k; break;
			case 'D': arg >> opt::dialign_debug; break;
			case 'M': arg >> opt::dialign_score; break;
			case 'P': arg >> opt::dialign_prob; break;
			case 'p': arg >> opt::identity; break;
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

	if (opt::k <= 0) {
		cerr << PROGRAM ": " << "missing -k,--kmer option\n";
		die = true;
	}

	if (argc - optind < 2) {
		cerr << PROGRAM ": missing arguments\n";
		die = true;
	}

	if (argc - optind > 2) {
		cerr << PROGRAM ": too many arguments\n";
		die = true;
	}

	if (die) {
		cerr << "Try `" << PROGRAM
			<< " --help' for more information.\n";
		exit(EXIT_FAILURE);
	}

	init_parameters();
	set_parameters_dna();
	para->DEBUG = opt::dialign_debug;
	para->SCR_MATRIX_FILE_NAME = (char*)opt::dialign_score.c_str();
	para->DIAG_PROB_FILE_NAME = (char*)opt::dialign_prob.c_str();
	initDialign();

	const char* contigsPath(argv[optind++]);
	string adjPath(argv[optind++]);

	// Read the contig adjacency graph.
	if (opt::verbose > 0)
		cerr << "Reading `" << adjPath << "'...\n";
	ifstream fin(adjPath.c_str());
	assert_good(fin, adjPath);
	Graph g;
	fin >> g;
	assert(fin.eof());
	g_contigNames.lock();
	if (opt::verbose > 0)
		printGraphStats(cerr, g);

	// Read the contigs.
	Contigs& contigs = g_contigs;
	if (opt::identity > 0) {
		if (opt::verbose > 0)
			cerr << "Reading `" << contigsPath << "'...\n";
		FastaReader in(contigsPath, FastaReader::NO_FOLD_CASE);
		for (FastaRecord rec; in >> rec;) {
			if (g_contigNames.count(rec.id) == 0)
				continue;
			assert(contigs.size() == get(g_contigNames, rec.id));
			contigs.push_back(rec.seq);
		}
		assert(in.eof());
		assert(!contigs.empty());
		opt::colourSpace = isdigit(contigs.front()[0]);
	}

	// Remove contigs with insufficient coverage.
	if (opt::minCoverage > 0)
		filterGraph(g);

	if (opt::dot)
		cout << "digraph bubbles {\n";

	Bubbles bubbles = discoverBubbles(g);
	for (Bubbles::const_iterator it = bubbles.begin();
			it != bubbles.end(); ++it)
		popOrScaffoldBubble(g, *it);

	// Each bubble should be identified twice. Remove the duplicate.
	sort(g_popped.begin(), g_popped.end());
	g_popped.erase(unique(g_popped.begin(), g_popped.end()),
			g_popped.end());

	if (opt::dot) {
		cout << "}\n";
	} else {
		for (vector<ContigID>::const_iterator it = g_popped.begin();
				it != g_popped.end(); ++it)
			cout << get(g_contigNames, *it) << '\n';
	}
	free_prob_dist(pdist);
	free(para);

	if (opt::verbose > 0)
		cerr << "Bubbles: " << (g_count.bubbles + 1) / 2
			<< " Popped: " << (g_count.popped + 1) / 2
			<< " Scaffolds: " << (g_count.scaffold + 1) / 2
			<< " Complex: " << (g_count.notSimple + 1) / 2
			<< " Too long: " << (g_count.tooLong + 1) / 2
			<< " Too many: " << (g_count.tooMany + 1) / 2
			<< " Dissimilar: " << (g_count.dissimilar + 1) / 2
			<< '\n';

	if (!opt::graphPath.empty()) {
		// Remove the popped contigs from the adjacency graph.
		for_each(g_popped.begin(), g_popped.end(),
				bind1st(ptr_fun(removeContig), &g));

		// Assemble unambiguous paths.
		g_contigNames.unlock();
		typedef vector<ContigPath> ContigPaths;
		ContigPaths paths;
		size_t numContigs = num_vertices(g) / 2;
		if (opt::scaffold) {
			Graph gorig = g;
			assemble(g, back_inserter(paths));
			for (ContigPaths::const_iterator it = paths.begin();
					it != paths.end(); ++it) {
				ContigNode u(numContigs + it - paths.begin(), false);
				string name = createContigName();
				put(vertex_name, g, u, name);
				cout << name << '\t'
					<< addDistance(gorig, *it) << '\n';
			}
		} else {
			assemble(g, back_inserter(paths));
			for (ContigPaths::const_iterator it = paths.begin();
					it != paths.end(); ++it) {
				ContigNode u(numContigs + it - paths.begin(), false);
				string name = createContigName();
				put(vertex_name, g, u, name);
				cout << name << '\t' << *it << '\n';
			}
		}
		g_contigNames.lock();
		paths.clear();

		// Output the updated adjacency graph.
		ofstream fout(opt::graphPath.c_str());
		assert_good(fout, opt::graphPath);
		write_graph(fout, g, PROGRAM, commandLine);
		assert_good(fout, opt::graphPath);
		if (opt::verbose > 0)
			printGraphStats(cerr, g);
	}

	return 0;
}
