#include "config.h"
#include "Common/Options.h"
#include "ContigID.h"
#include "ContigPath.h"
#include "Functional.h" // for mem_var
#include "IOUtil.h"
#include "Uncompress.h"
#include "Graph/Assemble.h"
#include "Graph/ContigGraph.h"
#include "Graph/DirectedGraph.h"
#include "Graph/DotIO.h"
#include "Graph/GraphAlgorithms.h"
#include "Graph/GraphUtil.h"
#include <boost/tuple/tuple.hpp>
#include <algorithm>
#include <cassert>
#include <climits> // for UINT_MAX
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <getopt.h>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#if _OPENMP
# include <omp.h>
#endif

using namespace std;
using boost::tie;

#define PROGRAM "MergePaths"

static const char VERSION_MESSAGE[] =
PROGRAM " (" PACKAGE_NAME ") " VERSION "\n"
"Written by Jared Simpson and Shaun Jackman.\n"
"\n"
"Copyright 2012 Canada's Michael Smith Genome Science Centre\n";

static const char USAGE_MESSAGE[] =
"Usage: " PROGRAM " [OPTION]... LEN PATH\n"
"Merge sequences of contigs IDs.\n"
"  LEN   lengths of the contigs\n"
"  PATH  sequences of contig IDs\n"
"\n"
"  -k, --kmer=KMER_SIZE  k-mer size\n"
"  -s, --seed-length=L   minimum length of a seed contig [0]\n"
"  -o, --out=FILE        write result to FILE\n"
"      --no-greedy       use the non-greedy algorithm [default]\n"
"      --greedy          use the greedy algorithm\n"
"  -g, --graph=FILE      write the path overlap graph to FILE\n"
"  -j, --threads=N       use N parallel threads [1]\n"
"  -v, --verbose         display verbose output\n"
"      --help            display this help and exit\n"
"      --version         output version information and exit\n"
"\n"
"Report bugs to <" PACKAGE_BUGREPORT ">.\n";

namespace opt {
	unsigned k; // used by GraphIO
	static string out;
	static int threads = 1;

	/** Minimum length of a seed contig. */
	static unsigned seedLen;

	/** Use a greedy algorithm. */
	static int greedy;

	/** Write the path overlap graph to this file. */
	static string graphPath;
}

static const char shortopts[] = "g:j:k:o:s:v";

enum { OPT_HELP = 1, OPT_VERSION };

static const struct option longopts[] = {
	{ "graph",       no_argument,       NULL, 'g' },
	{ "greedy",      no_argument,       &opt::greedy, true },
	{ "no-greedy",   no_argument,       &opt::greedy, false },
	{ "kmer",        required_argument, NULL, 'k' },
	{ "out",         required_argument, NULL, 'o' },
	{ "seed-length", required_argument, NULL, 's' },
	{ "threads",     required_argument,	NULL, 'j' },
	{ "verbose",     no_argument,       NULL, 'v' },
	{ "help",        no_argument,       NULL, OPT_HELP },
	{ "version",     no_argument,       NULL, OPT_VERSION },
	{ NULL, 0, NULL, 0 }
};

typedef map<ContigID, ContigPath> ContigPathMap;

/** Orientation of an edge. */
enum dir_type {
	DIR_X, // u--v none
	DIR_F, // u->v forward
	DIR_R, // u<-v reverse
	DIR_B, // u<>v both
};

/** Lengths of contigs measured in k-mer. */
typedef vector<unsigned> Lengths;

static ContigPath align(const Lengths& lengths,
		const ContigPath& p1, const ContigPath& p2,
		ContigNode pivot);
static ContigPath align(const Lengths& lengths,
		const ContigPath& p1, const ContigPath& p2,
		ContigNode pivot, dir_type& orientation);

static bool gDebugPrint;

/** Return all contigs that are tandem repeats, identified as those
 * contigs that appear more than once in a single path.
 */
static set<ContigID> findRepeats(const ContigPathMap& paths)
{
	set<ContigID> repeats;
	for (ContigPathMap::const_iterator pathIt = paths.begin();
			pathIt != paths.end(); ++pathIt) {
		const ContigPath& path = pathIt->second;
		map<ContigID, unsigned> count;
		for (ContigPath::const_iterator it = path.begin();
				it != path.end(); ++it)
			if (!it->ambiguous())
				count[it->contigIndex()]++;
		for (map<ContigID, unsigned>::const_iterator
				it = count.begin(); it != count.end(); ++it)
			if (it->second > 1)
				repeats.insert(it->first);
	}
	return repeats;
}

/** Remove tandem repeats from the set of paths.
 * @return the removed paths
 */
static set<ContigID> removeRepeats(ContigPathMap& paths)
{
	set<ContigID> repeats = findRepeats(paths);
	if (gDebugPrint) {
		cout << "Repeats:";
		if (!repeats.empty()) {
			for (set<ContigID>::const_iterator it = repeats.begin();
					it != repeats.end(); ++it)
				cout << ' ' << get(g_contigNames, *it);
		} else
			cout << " none";
		cout << '\n';
	}

	unsigned removed = 0;
	for (set<ContigID>::const_iterator it = repeats.begin();
			it != repeats.end(); ++it)
		if (paths.count(*it) > 0)
			removed++;
	if (removed == paths.size()) {
		// Every path was identified as a repeat. It's most likely a
		// cyclic sequence. Don't remove anything.
		repeats.clear();
		return repeats;
	}

	ostringstream ss;
	for (set<ContigID>::const_iterator it = repeats.begin();
			it != repeats.end(); ++it)
		if (paths.erase(*it) > 0)
			ss << ' ' << get(g_contigNames, *it);

	if (opt::verbose > 0 && removed > 0)
		cout << "Removing paths in repeats:" << ss.str() << '\n';
	return repeats;
}

static void appendToMergeQ(deque<ContigNode>& mergeQ,
	set<ContigNode>& seen, const ContigPath& path)
{
	for (ContigPath::const_iterator it = path.begin();
			it != path.end(); ++it)
		if (!it->ambiguous() && seen.insert(*it).second)
			mergeQ.push_back(*it);
}

/** A path overlap graph. */
typedef ContigGraph<DirectedGraph<> > PathGraph;

/** Add an edge if the two paths overlap.
 * @param pivot the pivot at which to seed the alignment
 * @return whether an overlap was found
 */
static bool addOverlapEdge(const Lengths& lengths,
		PathGraph& gout, ContigNode pivot,
		ContigNode seed1, const ContigPath& path1,
		ContigNode seed2, const ContigPath& path2)
{
	assert(seed1 != seed2);

	// Determine the orientation of the overlap edge.
	dir_type orientation = DIR_X;
	ContigPath consensus = align(lengths,
			path1, path2, pivot, orientation);
	if (consensus.empty())
		return false;
	assert(orientation != DIR_X);
	if (orientation == DIR_B) {
		// One of the paths subsumes the other. Use the order of the
		// seeds to determine the orientation of the edge.
		orientation = find(consensus.begin(), consensus.end(), seed1)
			< find(consensus.begin(), consensus.end(), seed2)
			? DIR_F : DIR_R;
	}
	assert(orientation == DIR_F || orientation == DIR_R);

	// Add the edge.
	ContigNode u = orientation == DIR_F ? seed1 : seed2;
	ContigNode v = orientation == DIR_F ? seed2 : seed1;
	bool added = false;
#pragma omp critical(gout)
	if (!edge(u, v, gout).second) {
		add_edge(u, v, gout);
		added = true;
	}
	return added;
}

/** Return the specified path. */
static ContigPath getPath(const ContigPathMap& paths, ContigNode u)
{
	ContigPathMap::const_iterator it = paths.find(u.contigIndex());
	assert(it != paths.end());
	ContigPath path = it->second;
	if (u.sense())
		reverseComplement(path.begin(), path.end());
	return path;
}

/** Find the overlaps between paths and add edges to the graph. */
static void findPathOverlaps(const Lengths& lengths,
		const ContigPathMap& paths,
		const ContigNode& seed1, const ContigPath& path1,
		PathGraph& gout)
{
	for (ContigPath::const_iterator it = path1.begin();
			it != path1.end(); ++it) {
		ContigNode seed2 = *it;
		if (seed1 == seed2)
			continue;
		if (seed2.ambiguous())
			continue;
		ContigPathMap::const_iterator path2It
			= paths.find(seed2.contigIndex());
		if (path2It == paths.end())
			continue;

		ContigPath path2 = path2It->second;
		if (seed2.sense())
			reverseComplement(path2.begin(), path2.end());
		addOverlapEdge(lengths,
				gout, seed2, seed1, path1, seed2, path2);
	}
}

/** Attempt to merge the paths specified in mergeQ with path.
 * @return the number of paths merged
 */
static unsigned mergePaths(const Lengths& lengths,
		ContigPath& path,
		deque<ContigNode>& mergeQ, set<ContigNode>& seen,
		const ContigPathMap& paths)
{
	unsigned merged = 0;
	deque<ContigNode> invalid;
	for (ContigNode pivot; !mergeQ.empty(); mergeQ.pop_front()) {
		pivot = mergeQ.front();
		ContigPathMap::const_iterator path2It
			= paths.find(pivot.contigIndex());
		if (path2It == paths.end())
			continue;

		ContigPath path2 = path2It->second;
		if (pivot.sense())
			reverseComplement(path2.begin(), path2.end());
		ContigPath consensus = align(lengths, path, path2, pivot);
		if (consensus.empty()) {
			invalid.push_back(pivot);
			continue;
		}

		appendToMergeQ(mergeQ, seen, path2);
		path.swap(consensus);
		if (gDebugPrint)
#pragma omp critical(cout)
			cout << get(g_contigNames, pivot)
				<< '\t' << path2 << '\n'
				<< '\t' << path << '\n';
		merged++;
	}
	mergeQ.swap(invalid);
	return merged;
}

/** Merge the paths of the specified seed path.
 * @return the merged contig path
 */
static ContigPath mergePath(const Lengths& lengths,
		const ContigPathMap& paths, const ContigPath& seedPath)
{
	assert(!seedPath.empty());
	ContigNode seed1 = seedPath.front();
	ContigPathMap::const_iterator path1It
		= paths.find(seed1.contigIndex());
	assert(path1It != paths.end());
	ContigPath path(path1It->second);
	if (seedPath.front().sense())
		reverseComplement(path.begin(), path.end());
	if (opt::verbose > 1)
#pragma omp critical(cout)
		cout << "\n* " << seedPath << '\n'
			<< get(g_contigNames, seedPath.front())
			<< '\t' << path << '\n';
	for (ContigPath::const_iterator it = seedPath.begin() + 1;
			it != seedPath.end(); ++it) {
		ContigNode seed2 = *it;
		ContigPathMap::const_iterator path2It
			= paths.find(seed2.contigIndex());
		assert(path2It != paths.end());
		ContigPath path2 = path2It->second;
		if (seed2.sense())
			reverseComplement(path2.begin(), path2.end());

		ContigNode pivot
			= find(path.begin(), path.end(), seed2) != path.end()
			? seed2 : seed1;
		ContigPath consensus = align(lengths, path, path2, pivot);
		if (consensus.empty()) {
			// This seed could be removed from the seed path.
			if (opt::verbose > 1)
#pragma omp critical(cout)
				cout << get(g_contigNames, seed2)
					<< '\t' << path2 << '\n'
					<< "\tinvalid\n";
		} else {
			path.swap(consensus);
			if (opt::verbose > 1)
#pragma omp critical(cout)
				cout << get(g_contigNames, seed2)
					<< '\t' << path2 << '\n'
					<< '\t' << path << '\n';
		}
		seed1 = seed2;
	}
	return path;
}

/** A collection of contig paths. */
typedef vector<ContigPath> ContigPaths;

/** Merge the specified seed paths.
 * @return the merged contig paths
 */
static ContigPaths mergeSeedPaths(const Lengths& lengths,
		const ContigPathMap& paths, const ContigPaths& seedPaths)
{
	if (opt::verbose > 0)
		cout << "\nMerging paths\n";

	ContigPaths out;
	out.reserve(seedPaths.size());
	for (ContigPaths::const_iterator it = seedPaths.begin();
			it != seedPaths.end(); ++it)
		out.push_back(mergePath(lengths, paths, *it));
	return out;
}

/** Extend the specified path as long as is unambiguously possible and
 * add the result to the specified container.
 */
static void extendPaths(const Lengths& lengths,
		ContigID id, const ContigPathMap& paths,
		ContigPathMap& out)
{
	ContigPathMap::const_iterator pathIt = paths.find(id);
	assert(pathIt != paths.end());

	pair<ContigPathMap::iterator, bool> inserted;
	#pragma omp critical(out)
	inserted = out.insert(*pathIt);
	assert(inserted.second);
	ContigPath& path = inserted.first->second;

	if (gDebugPrint)
		#pragma omp critical(cout)
		cout << "\n* " << get(g_contigNames, id) << "+\n"
			<< '\t' << path << '\n';

	set<ContigNode> seen;
	seen.insert(ContigNode(id, false));
	deque<ContigNode> mergeQ;
	appendToMergeQ(mergeQ, seen, path);
	while (mergePaths(lengths, path, mergeQ, seen, paths) > 0)
		;

	if (!mergeQ.empty() && gDebugPrint) {
		#pragma omp critical(cout)
		{
			cout << "invalid\n";
			for (deque<ContigNode>::const_iterator it
					= mergeQ.begin(); it != mergeQ.end(); ++it)
				cout << get(g_contigNames, *it) << '\t'
					<< paths.find(it->contigIndex())->second << '\n';
		}
	}
}

/** Return true if the contigs are equal or both are ambiguous. */
static bool equalOrBothAmbiguos(const ContigNode& a,
		const ContigNode& b)
{
	return a == b || (a.ambiguous() && b.ambiguous());
}

/** Return true if both paths are equal, ignoring ambiguous nodes. */
static bool equalIgnoreAmbiguos(const ContigPath& a,
		const ContigPath& b)
{
	return a.size() == b.size()
		&& equal(a.begin(), a.end(), b.begin(), equalOrBothAmbiguos);
}

/** Return whether this path is a cycle. */
static bool isCycle(const Lengths& lengths, const ContigPath& path)
{
	return !align(lengths, path, path, path.front()).empty();
}

/** Identify paths subsumed by the specified path.
 * @param overlaps [out] paths that are found to overlap
 * @return the ID of the subsuming path
 */
static ContigID identifySubsumedPaths(const Lengths& lengths,
		ContigPathMap::const_iterator path1It,
		ContigPathMap& paths,
		set<ContigID>& out,
		set<ContigID>& overlaps)
{
	ostringstream vout;
	out.clear();
	ContigID id(path1It->first);
	const ContigPath& path = path1It->second;
	if (gDebugPrint)
		vout << get(g_contigNames, ContigNode(id, false))
			<< '\t' << path << '\n';

	for (ContigPath::const_iterator it = path.begin();
			it != path.end(); ++it) {
		ContigNode pivot = *it;
		if (pivot.ambiguous() || pivot.id() == id)
			continue;
		ContigPathMap::iterator path2It
			= paths.find(pivot.contigIndex());
		if (path2It == paths.end())
			continue;
		ContigPath path2 = path2It->second;
		if (pivot.sense())
			reverseComplement(path2.begin(), path2.end());
		ContigPath consensus = align(lengths, path, path2, pivot);
		if (consensus.empty())
			continue;
		if (equalIgnoreAmbiguos(consensus, path)) {
			if (gDebugPrint)
				vout << get(g_contigNames, pivot)
					<< '\t' << path2 << '\n';
			out.insert(path2It->first);
		} else if (equalIgnoreAmbiguos(consensus, path2)) {
			// This path is larger. Use it as the seed.
			return identifySubsumedPaths(lengths, path2It, paths, out,
					overlaps);
		} else if (isCycle(lengths, consensus)) {
			// The consensus path is a cycle.
			bool isCyclePath1 = isCycle(lengths, path);
			bool isCyclePath2 = isCycle(lengths, path2);
			if (!isCyclePath1 && !isCyclePath2) {
				// Neither path is a cycle.
				if (gDebugPrint)
					vout << get(g_contigNames, pivot)
						<< '\t' << path2 << '\n'
						<< "ignored\t" << consensus << '\n';
				overlaps.insert(id);
				overlaps.insert(path2It->first);
			} else {
				// At least one path is a cycle.
				if (gDebugPrint)
					vout << get(g_contigNames, pivot)
						<< '\t' << path2 << '\n'
						<< "cycle\t" << consensus << '\n';
				if (isCyclePath1 && isCyclePath2)
					out.insert(path2It->first);
				else if (!isCyclePath1)
					overlaps.insert(id);
				else if (!isCyclePath2)
					overlaps.insert(path2It->first);
			}
		} else {
			if (gDebugPrint)
				vout << get(g_contigNames, pivot)
					<< '\t' << path2 << '\n'
					<< "ignored\t" << consensus << '\n';
			overlaps.insert(id);
			overlaps.insert(path2It->first);
		}
	}
	cout << vout.str();
	return id;
}

/** Remove paths subsumed by the specified path.
 * @param seed [out] the ID of the subsuming path
 * @param overlaps [out] paths that are found to overlap
 * @return the next iterator after path1it
 */
static ContigPathMap::const_iterator removeSubsumedPaths(
		const Lengths& lengths,
		ContigPathMap::const_iterator path1It, ContigPathMap& paths,
		ContigID& seed, set<ContigID>& overlaps)
{
	if (gDebugPrint)
		cout << '\n';
	set<ContigID> eq;
	seed = identifySubsumedPaths(lengths,
			path1It, paths, eq, overlaps);
	++path1It;
	for (set<ContigID>::const_iterator it = eq.begin();
			it != eq.end(); ++it) {
		if (*it == path1It->first)
			++path1It;
		paths.erase(*it);
	}
	return path1It;
}

/** Remove paths subsumed by another path.
 * @return paths that are found to overlap
 */
static set<ContigID> removeSubsumedPaths(const Lengths& lengths,
		ContigPathMap& paths)
{
	set<ContigID> overlaps, seen;
	for (ContigPathMap::const_iterator iter = paths.begin();
			iter != paths.end();) {
		if (seen.count(iter->first) == 0) {
			ContigID seed;
			iter = removeSubsumedPaths(lengths,
					iter, paths, seed, overlaps);
			seen.insert(seed);
		} else
			++iter;
	}
	return overlaps;
}

/** Add missing overlap edges. For each vertex u with at least two
 * outgoing edges, (u,v1) and (u,v2), add the edge (v1,v2) if v1 < v2,
 * and add the edge (v2,v1) if v2 < v1.
 */
static void addMissingEdges(const Lengths& lengths,
		PathGraph& g, const ContigPathMap& paths)
{
	typedef graph_traits<PathGraph>::adjacency_iterator Vit;
	typedef graph_traits<PathGraph>::vertex_iterator Uit;
	typedef graph_traits<PathGraph>::vertex_descriptor V;

	unsigned numAdded = 0;
	pair<Uit, Uit> urange = vertices(g);
	for (Uit uit = urange.first; uit != urange.second; ++uit) {
		V u = *uit;
		if (out_degree(u, g) < 2)
			continue;
		pair<Vit, Vit> vrange = adjacent_vertices(u, g);
		for (Vit vit1 = vrange.first; vit1 != vrange.second;) {
			V v1 = *vit1;
			++vit1;
			assert(v1 != u);
			ContigPath path1 = getPath(paths, v1);
			if (find(path1.begin(), path1.end(),
						ContigPath::value_type(u)) == path1.end())
				continue;
			for (Vit vit2 = vit1; vit2 != vrange.second; ++vit2) {
				V v2 = *vit2;
				assert(v2 != u);
				assert(v1 != v2);
				if (edge(v1, v2, g).second || edge(v2, v1, g).second)
					continue;
				ContigPath path2 = getPath(paths, v2);
				if (find(path2.begin(), path2.end(),
							ContigPath::value_type(u)) == path2.end())
					continue;
				numAdded += addOverlapEdge(lengths,
						g, u, v1, path1, v2, path2);
			}
		}
	}
	if (opt::verbose > 0)
		cout << "Added " << numAdded << " missing edges.\n";
}

/** Remove transitive edges. */
static void removeTransitiveEdges(PathGraph& pathGraph)
{
	unsigned nbefore = num_edges(pathGraph);
	unsigned nremoved = remove_transitive_edges(pathGraph);
	unsigned nafter = num_edges(pathGraph);
	if (opt::verbose > 0)
		cout << "Removed " << nremoved << " transitive edges of "
			<< nbefore << " edges leaving "
			<< nafter << " edges.\n";
	assert(nbefore - nremoved == nafter);
}

/** Remove ambiguous edges that overlap by only a small amount.
 * Remove the edge (u,v) if deg+(u) > 1 and deg-(v) > 1 and the
 * overlap of (u,v) is small.
 */
static void removeSmallOverlaps(PathGraph& g,
		const ContigPathMap& paths)
{
	typedef graph_traits<PathGraph>::edge_descriptor E;
	typedef graph_traits<PathGraph>::out_edge_iterator Eit;
	typedef graph_traits<PathGraph>::vertex_descriptor V;
	typedef graph_traits<PathGraph>::vertex_iterator Vit;

	vector<E> edges;
	pair<Vit, Vit> urange = vertices(g);
	for (Vit uit = urange.first; uit != urange.second; ++uit) {
		V u = *uit;
		if (out_degree(u, g) < 2)
			continue;
		ContigPath pathu = getPath(paths, u);
		pair<Eit, Eit> uvits = out_edges(u, g);
		for (Eit uvit = uvits.first; uvit != uvits.second; ++uvit) {
			E uv = *uvit;
			V v = target(uv, g);
			assert(v != u);
			if (in_degree(v, g) < 2)
				continue;
			ContigPath pathv = getPath(paths, v);
			if (pathu.back() == pathv.front()
					&& paths.count(pathu.back().contigIndex()) > 0)
				edges.push_back(uv);
		}
	}
	remove_edges(g, edges.begin(), edges.end());
	if (opt::verbose > 0)
		cout << "Removed " << edges.size()
			<< " small overlap edges.\n";
}

/** Output the path overlap graph. */
static void outputPathGraph(PathGraph& pathGraph)
{
	if (opt::graphPath.empty())
		return;
	ofstream out(opt::graphPath.c_str());
	assert_good(out, opt::graphPath);
	write_dot(out, pathGraph);
	assert_good(out, opt::graphPath);
}

/** Sort and output the specified paths. */
static void outputSortedPaths(const ContigPathMap& paths)
{
	// Sort the paths.
	vector<ContigPath> sortedPaths(paths.size());
	transform(paths.begin(), paths.end(), sortedPaths.begin(),
			mem_var(&ContigPathMap::value_type::second));
	sort(sortedPaths.begin(), sortedPaths.end());

	// Output the paths.
	ofstream fout(opt::out.c_str());
	ostream& out = opt::out.empty() ? cout : fout;
	assert_good(out, opt::out);
	for (vector<ContigPath>::const_iterator it = sortedPaths.begin();
			it != sortedPaths.end(); ++it)
		out << createContigName() << '\t' << *it << '\n';
	assert_good(out, opt::out);
}

/** Assemble the path overlap graph. */
static void assemblePathGraph(const Lengths& lengths,
		PathGraph& pathGraph, ContigPathMap& paths)
{
	ContigPaths seedPaths;
	assembleDFS(pathGraph, back_inserter(seedPaths));
	ContigPaths mergedPaths = mergeSeedPaths(lengths,
			paths, seedPaths);
	if (opt::verbose > 1)
		cout << '\n';

	// Replace each path with the merged path.
	for (ContigPaths::const_iterator it1 = seedPaths.begin();
			it1 != seedPaths.end(); ++it1) {
		const ContigPath& path(mergedPaths[it1 - seedPaths.begin()]);
		ContigPath pathrc(path);
		reverseComplement(pathrc.begin(), pathrc.end());
		for (ContigPath::const_iterator it2 = it1->begin();
				it2 != it1->end(); ++it2) {
			ContigNode seed(*it2);
			if (find(path.begin(), path.end(), seed) != path.end()) {
				paths[seed.contigIndex()]
					= seed.sense() ? pathrc : path;
			} else {
				// This seed was not included in the merged path.
			}
		}
	}

	removeRepeats(paths);

	// Remove the subsumed paths.
	if (opt::verbose > 0)
		cout << "Removing redundant contigs\n";
	removeSubsumedPaths(lengths, paths);

	outputSortedPaths(paths);
}

/** Read a set of paths from the specified file. */
static ContigPathMap readPaths(const Lengths& lengths,
		const string& filePath)
{
	if (opt::verbose > 0)
		cerr << "Reading `" << filePath << "'..." << endl;
	ifstream in(filePath.c_str());
	assert_good(in, filePath);

	unsigned tooSmall = 0;
	ContigPathMap paths;
	std::string name;
	ContigPath path;
	while (in >> name >> path) {
		// Ignore seed contigs shorter than the threshold length.
		ContigID id(get(g_contigNames, name));
		unsigned len = lengths[id] + opt::k - 1;
		if (len < opt::seedLen) {
			tooSmall++;
			continue;
		}

		bool inserted = paths.insert(
				make_pair(id, path)).second;
		assert(inserted);
		(void)inserted;
	}
	assert(in.eof());

	if (opt::seedLen > 0)
		cout << "Ignored " << tooSmall
			<< " paths whose seeds are shorter than "
			<< opt::seedLen << " bp.\n";
	return paths;
}

/** Store it in out and increment it.
 * @return true if out != last
 */
template<class T>
bool atomicInc(T& it, T last, T& out)
{
	#pragma omp critical(atomicInc)
	out = it == last ? it : it++;
	return out != last;
}

/** Build the path overlap graph. */
static void buildPathGraph(const Lengths& lengths,
		PathGraph& g, const ContigPathMap& paths)
{
	// Create the vertices of the path overlap graph.
	PathGraph(lengths.size()).swap(g);

	// Remove the non-seed contigs.
	typedef graph_traits<PathGraph>::vertex_iterator vertex_iterator;
	pair<vertex_iterator, vertex_iterator> vit = g.vertices();
	for (vertex_iterator u = vit.first; u != vit.second; ++u)
		if (paths.count(get(vertex_contig_index, g, *u)) == 0)
			remove_vertex(*u, g);

	// Find the overlapping paths.
	ContigPathMap::const_iterator sharedIt = paths.begin();
	#pragma omp parallel
	for (ContigPathMap::const_iterator it;
			atomicInc(sharedIt, paths.end(), it);)
		findPathOverlaps(lengths, paths,
				ContigNode(it->first, false), it->second, g);
	if (gDebugPrint)
		cout << '\n';

	addMissingEdges(lengths, g, paths);
	removeTransitiveEdges(g);
	removeSmallOverlaps(g, paths);
	if (opt::verbose > 0)
		printGraphStats(cout, g);
	outputPathGraph(g);
}

/** Read contig lengths. */
static Lengths readContigLengths(istream& in)
{
	assert(in);
	assert(g_contigNames.empty());
	Lengths lengths;
	string s;
	unsigned len;
	while (in >> s >> len) {
		in.ignore(numeric_limits<streamsize>::max(), '\n');
		put(g_contigNames, lengths.size(), s);
		assert(len >= opt::k);
		lengths.push_back(len - opt::k + 1);
	}
	assert(in.eof());
	assert(!lengths.empty());
	g_contigNames.lock();
	return lengths;
}

/** Read contig lengths. */
static Lengths readContigLengths(const string& path)
{
	ifstream in(path.c_str());
	assert_good(in, path);
	return readContigLengths(in);
}

int main(int argc, char** argv)
{
	bool die = false;
	for (int c; (c = getopt_long(argc, argv,
					shortopts, longopts, NULL)) != -1;) {
		istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
			case '?': die = true; break;
			case 'g': arg >> opt::graphPath; break;
			case 'j': arg >> opt::threads; break;
			case 'k': arg >> opt::k; break;
			case 'o': arg >> opt::out; break;
			case 's': arg >> opt::seedLen; break;
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
		cerr << PROGRAM ": missing -k,--kmer option\n";
		die = true;
	}

	if (argc - optind < 2) {
		cerr << PROGRAM ": missing arguments\n";
		die = true;
	} else if (argc - optind > 2) {
		cerr << PROGRAM ": too many arguments\n";
		die = true;
	}

	if (die) {
		cerr << "Try `" << PROGRAM
			<< " --help' for more information.\n";
		exit(EXIT_FAILURE);
	}

	if (!opt::graphPath.empty())
		opt::greedy = false;

	gDebugPrint = opt::verbose > 1;

#if _OPENMP
	if (opt::threads > 0)
		omp_set_num_threads(opt::threads);
#endif

	if (opt::verbose > 0)
		cerr << "Reading `" << argv[optind] << "'..." << endl;

	Lengths lengths = readContigLengths(argv[optind++]);
	ContigPathMap originalPathMap = readPaths(
			lengths, argv[optind++]);

	removeRepeats(originalPathMap);

	if (!opt::greedy) {
		// Assemble the path overlap graph.
		PathGraph pathGraph;
		buildPathGraph(lengths, pathGraph, originalPathMap);
		if (!opt::out.empty())
			assemblePathGraph(lengths, pathGraph, originalPathMap);
		exit(EXIT_SUCCESS);
	}

	ContigPathMap resultsPathMap;
#if _OPENMP
	ContigPathMap::iterator sharedIt = originalPathMap.begin();
	#pragma omp parallel
	for (ContigPathMap::iterator it;
			atomicInc(sharedIt, originalPathMap.end(), it);)
		extendPaths(lengths,
				it->first, originalPathMap, resultsPathMap);
#else
	for (ContigPathMap::const_iterator it = originalPathMap.begin();
			it != originalPathMap.end(); ++it)
		extendPaths(lengths,
				it->first, originalPathMap, resultsPathMap);
#endif
	if (gDebugPrint)
		cout << '\n';

	set<ContigID> repeats = removeRepeats(resultsPathMap);

	if (gDebugPrint)
		cout << "\nRemoving redundant contigs\n";
	set<ContigID> overlaps = removeSubsumedPaths(lengths,
			resultsPathMap);

	if (!overlaps.empty() && !repeats.empty()) {
		// Remove the newly-discovered repeat contigs from the
		// original paths.
		for (set<ContigID>::const_iterator it = repeats.begin();
				it != repeats.end(); ++it)
			originalPathMap.erase(*it);

		// Reassemble the paths that were found to overlap.
		if (gDebugPrint) {
			cout << "\nReassembling overlapping contigs:";
			for (set<ContigID>::const_iterator it = overlaps.begin();
					it != overlaps.end(); ++it)
				cout << ' ' << get(g_contigNames, *it);
			cout << '\n';
		}

		for (set<ContigID>::const_iterator it = overlaps.begin();
				it != overlaps.end(); ++it) {
			if (originalPathMap.count(*it) == 0)
				continue; // repeat
			ContigPathMap::iterator oldIt = resultsPathMap.find(*it);
			if (oldIt == resultsPathMap.end())
				continue; // subsumed
			ContigPath old = oldIt->second;
			resultsPathMap.erase(oldIt);
			extendPaths(lengths,
					*it, originalPathMap, resultsPathMap);
			if (gDebugPrint) {
				if (resultsPathMap[*it] == old)
					cout << "no change\n";
				else
					cout << "was\t" << old << '\n';
			}
		}
		if (gDebugPrint)
			cout << '\n';

		removeRepeats(resultsPathMap);
		overlaps = removeSubsumedPaths(lengths, resultsPathMap);
		if (!overlaps.empty() && gDebugPrint) {
			cout << "\nOverlapping contigs:";
			for (set<ContigID>::const_iterator it = overlaps.begin();
					it != overlaps.end(); ++it)
				cout << ' ' << get(g_contigNames, *it);
			cout << '\n';
		}
	}
	originalPathMap.clear();

	outputSortedPaths(resultsPathMap);
	return 0;
}

/** Return the length of the specified contig in k-mer. */
static unsigned getLength(const Lengths& lengths,
		const ContigNode& u)
{
	return u.ambiguous() ? u.length()
		: lengths.at(u.id());
}

/** Functor to add the number of k-mer in two contigs. */
struct AddLength
{
	AddLength(const Lengths& lengths) : m_lengths(lengths) { }
	unsigned operator()(unsigned addend, const ContigNode& u) const
	{
		return addend + getLength(m_lengths, u);
	}
  private:
	const Lengths& m_lengths;
};

/** Attempt to fill in gaps in one path with the sequence from the
 * other path and store the consensus at result if an alignment is
 * found.
 * @return true if an alignment is found
 */
template <class iterator, class oiterator>
static bool alignCoordinates(const Lengths& lengths,
		iterator& first1, iterator last1,
		iterator& first2, iterator last2, oiterator& result)
{
	oiterator out = result;

	int ambiguous1 = 0, ambiguous2 = 0;
	iterator it1 = first1, it2 = first2;
	while (it1 != last1 && it2 != last2) {
		if (it1->ambiguous()) {
			ambiguous1 += it1->length();
			++it1;
			assert(it1 != last1);
			assert(!it1->ambiguous());
		}
		if (it2->ambiguous()) {
			ambiguous2 += it2->length();
			++it2;
			assert(it2 != last2);
			assert(!it2->ambiguous());
		}

		if (ambiguous1 > 0 && ambiguous2 > 0) {
			if (ambiguous1 > ambiguous2) {
				*out++ = ContigNode(ambiguous2, 'N');
				ambiguous1 -= ambiguous2;
				ambiguous2 = 0;
			} else {
				*out++ = ContigNode(ambiguous1, 'N');
				ambiguous2 -= ambiguous1;
				ambiguous1 = 0;
			}
		} else if (ambiguous1 > 0) {
			ambiguous1 -= getLength(lengths, *it2);
			*out++ = *it2++;
		} else if (ambiguous2 > 0) {
			ambiguous2 -= getLength(lengths, *it1);
			*out++ = *it1++;
		} else
			assert(false);

		if (ambiguous1 == 0 && ambiguous2 == 0)
			break;
		if (ambiguous1 < 0 || ambiguous2 < 0)
			return false;
	}

	assert(ambiguous1 == 0 || ambiguous2 == 0);
	int ambiguous = ambiguous1 + ambiguous2;
	assert(out > result);
	if (out[-1].ambiguous())
		assert(ambiguous == 0);
	else
		*out++ = ContigNode(max(1, ambiguous), 'N');
	first1 = it1;
	first2 = it2;
	result = out;
	return true;
}

/** Align the ambiguous region [it1, it1e) to [it2, it2e) and store
 * the consensus at out if an alignment is found.
 * @return true if an alignment is found
 */
template <class iterator, class oiterator>
static bool buildConsensus(const Lengths& lengths,
		iterator it1, iterator it1e,
		iterator it2, iterator it2e, oiterator& out)
{
	iterator it1b = it1 + 1;
	assert(!it1b->ambiguous());

	if (it1b == it1e) {
		// path2 completely fills the gap in path1.
		out = copy(it2, it2e, out);
		return true;
	}

	// The gaps of path1 and path2 overlap.
	iterator it2a = it2e - 1;
	if (it2e == it2 || !it2a->ambiguous()) {
		// The two paths do not agree. No alignment.
		return false;
	}

	unsigned ambiguous1 = it1->length();
	unsigned ambiguous2 = it2a->length();
	unsigned unambiguous1 = accumulate(it1b, it1e,
			0, AddLength(lengths));
	unsigned unambiguous2 = accumulate(it2, it2a,
			0, AddLength(lengths));
	if (ambiguous1 < unambiguous2
			|| ambiguous2 < unambiguous1) {
		// Two gaps overlap and either of the gaps is smaller
		// than the unambiguous sequence that overlaps the
		// gap. No alignment.
		return false;
	}

	unsigned n = max(1U,
			max(ambiguous2 - unambiguous1,
				ambiguous1 - unambiguous2));
	out = copy(it2, it2a, out);
	*out++ = ContigNode(n, 'N');
	out = copy(it1b, it1e, out);
	return true;
}

/** Align the ambiguous region [it1, last1) to [it2, last2) using it1e
 * as the seed of the alignment. The end of the alignment is returned
 * in it1 and it2.
 * @return true if an alignment is found
 */
template <class iterator, class oiterator>
static bool alignAtSeed(const Lengths& lengths,
		iterator& it1, iterator it1e, iterator last1,
		iterator& it2, iterator last2, oiterator& out)
{
	assert(it1 != last1);
	assert(it1->ambiguous());
	assert(it1 + 1 != last1);
	assert(!it1e->ambiguous());
	assert(it2 != last2);

	// Find the best seeded alignment. The best alignment has the
	// fewest number of contigs in the consensus sequence.
	unsigned bestLen = UINT_MAX;
	iterator bestIt2e;
	for (iterator it2e = it2;
			(it2e = find(it2e, last2, *it1e)) != last2; ++it2e) {
		oiterator myOut = out;
		if (buildConsensus(lengths, it1, it1e, it2, it2e, myOut)
				&& align(lengths, it1e, last1, it2e, last2, myOut)) {
			unsigned len = myOut - out;
			if (len <= bestLen) {
				bestLen = len;
				bestIt2e = it2e;
			}
		}
	}
	if (bestLen != UINT_MAX) {
		bool good = buildConsensus(lengths,
				it1, it1e, it2, bestIt2e, out);
		assert(good);
		it1 = it1e;
		it2 = bestIt2e;
		return good;
	} else
		return false;
}

/** Align the ambiguous region [it1, last1) to [it2, last2).
 * The end of the alignment is returned in it1 and it2.
 * @return true if an alignment is found
 */
template <class iterator, class oiterator>
static bool alignAmbiguous(const Lengths& lengths,
		iterator& it1, iterator last1,
		iterator& it2, iterator last2, oiterator& out)
{
	assert(it1 != last1);
	assert(it1->ambiguous());
	assert(it1 + 1 != last1);
	assert(it2 != last2);

	// Find a seed for the alignment.
	for (iterator it1e = it1; it1e != last1; ++it1e) {
		if (it1e->ambiguous())
			continue;
		if (alignAtSeed(lengths, it1, it1e, last1, it2, last2, out))
			return true;
	}

	// No valid seeded alignment. Check whether path2 fits entirely
	// within the gap of path1.
	return alignCoordinates(lengths, it1, last1, it2, last2, out);
}

/** Align the next pair of contigs.
 * The end of the alignment is returned in it1 and it2.
 * @return true if an alignment is found
 */
template <class iterator, class oiterator>
static bool alignOne(const Lengths& lengths,
		iterator& it1, iterator last1,
		iterator& it2, iterator last2, oiterator& out)
{
	// Check for a trivial alignment.
	unsigned n1 = last1 - it1, n2 = last2 - it2;
	if (n1 <= n2 && equal(it1, last1, it2)) {
		// [it1,last1) is a prefix of [it2,last2).
		out = copy(it1, last1, out);
		it1 += n1;
		it2 += n1;
		assert(it1 == last1);
		return true;
	} else if (n2 < n1 && equal(it2, last2, it1)) {
		// [it2,last2) is a prefix of [it1,last1).
		out = copy(it2, last2, out);
		it1 += n2;
		it2 += n2;
		assert(it2 == last2);
		return true;
	}

	return
		it1->ambiguous() && it2->ambiguous()
			? (it1->length() > it2->length()
				? alignAmbiguous(lengths, it1, last1, it2, last2, out)
				: alignAmbiguous(lengths, it2, last2, it1, last1, out)
			)
		: it1->ambiguous()
			? alignAmbiguous(lengths, it1, last1, it2, last2, out)
		: it2->ambiguous()
			? alignAmbiguous(lengths, it2, last2, it1, last1, out)
			: (*out++ = *it1, *it1++ == *it2++);
}

/** Align the ambiguous region [it1, last1) to [it2, last2)
 * and store the consensus at out if an alignment is found.
 * @return the orientation of the alignment if an alignments is found
 * or zero otherwise
 */
template <class iterator, class oiterator>
static dir_type align(const Lengths& lengths,
		iterator it1, iterator last1,
		iterator it2, iterator last2, oiterator& out)
{
	assert(it1 != last1);
	assert(it2 != last2);
	while (it1 != last1 && it2 != last2)
		if (!alignOne(lengths, it1, last1, it2, last2, out))
			return DIR_X;
	assert(it1 == last1 || it2 == last2);
	out = copy(it1, last1, out);
	out = copy(it2, last2, out);
	return it1 == last1 && it2 == last2 ? DIR_B
		: it1 == last1 ? DIR_F
		: it2 == last2 ? DIR_R
		: DIR_X;
}

/** Find an equivalent region of the two specified paths, starting the
 * alignment at pivot1 of path1 and pivot2 of path2.
 * @param[out] orientation the orientation of the alignment
 * @return the consensus sequence
 */
static ContigPath align(const Lengths& lengths,
		const ContigPath& p1, const ContigPath& p2,
		ContigPath::const_iterator pivot1,
		ContigPath::const_iterator pivot2,
		dir_type& orientation)
{
	assert(*pivot1 == *pivot2);
	ContigPath::const_reverse_iterator
		rit1 = ContigPath::const_reverse_iterator(pivot1+1),
		rit2 = ContigPath::const_reverse_iterator(pivot2+1);
	ContigPath alignmentr(p1.rend() - rit1 + p2.rend() - rit2);
	ContigPath::iterator rout = alignmentr.begin();
	dir_type alignedr = align(lengths,
			rit1, p1.rend(), rit2, p2.rend(), rout);
	alignmentr.erase(rout, alignmentr.end());

	ContigPath::const_iterator it1 = pivot1, it2 = pivot2;
	ContigPath alignmentf(p1.end() - it1 + p2.end() - it2);
	ContigPath::iterator fout = alignmentf.begin();
	dir_type alignedf = align(lengths,
			it1, p1.end(), it2, p2.end(), fout);
	alignmentf.erase(fout, alignmentf.end());

	ContigPath consensus;
	if (alignedr != DIR_X && alignedf != DIR_X) {
		// Found an alignment.
		assert(!alignmentf.empty());
		assert(!alignmentr.empty());
		consensus.reserve(alignmentr.size()-1 + alignmentf.size());
		consensus.assign(alignmentr.rbegin(), alignmentr.rend()-1);
		consensus.insert(consensus.end(),
				alignmentf.begin(), alignmentf.end());

		// Determine the orientation of the alignment.
		unsigned dirs = alignedr << 2 | alignedf;
		static const dir_type DIRS[16] = {
			DIR_X, // 0000 XX impossible
			DIR_X, // 0001 XF impossible
			DIR_X, // 0010 XR impossible
			DIR_X, // 0011 XB impossible
			DIR_X, // 0100 FX impossible
			DIR_B, // 0101 FF u is subsumed in v
			DIR_R, // 0110 FR v->u
			DIR_R, // 0111 FB v->u
			DIR_X, // 1000 RX impossible
			DIR_F, // 1001 RF u->v
			DIR_B, // 1010 RR v is subsumed in u
			DIR_F, // 1011 RB u->v
			DIR_X, // 1100 BX impossible
			DIR_F, // 1101 BF u->v
			DIR_R, // 1110 BR v->u
			DIR_B, // 1111 BB u and v are equal
		};
		assert(dirs < 16);
		orientation = DIRS[dirs];
		assert(orientation != DIR_X);
	}
	return consensus;
}

/** Return a pivot suitable for aligning the two paths if one exists,
 * otherwise return false.
 */
static pair<ContigNode, bool> findPivot(
		const ContigPath& path1, const ContigPath& path2)
{
	for (ContigPath::const_iterator it = path2.begin();
			it != path2.end(); ++it) {
		if (it->ambiguous())
			continue;
		if (count(path2.begin(), path2.end(), *it) == 1
				&& count(path1.begin(), path1.end(), *it) == 1)
			return make_pair(*it, true);
	}
	return make_pair(ContigNode(0), false);
}

/** Find an equivalent region of the two specified paths.
 * @param[out] orientation the orientation of the alignment
 * @return the consensus sequence
 */
static ContigPath align(const Lengths& lengths,
		const ContigPath& path1, const ContigPath& path2,
		ContigNode pivot, dir_type& orientation)
{
	if (&path1 == &path2) {
		// Ignore the trivial alignment when aligning a path to
		// itself.
	} else if (path1 == path2) {
		// These two paths are identical.
		orientation = DIR_B;
		return path1;
	} else {
		ContigPath::const_iterator it
			= search(path1.begin(), path1.end(),
				path2.begin(), path2.end());
		if (it != path1.end()) {
			// path2 is subsumed in path1.
			// Determine the orientation of the edge.
			orientation
				= it == path1.begin() ? DIR_R
				: it + path2.size() == path1.end() ? DIR_F
				: DIR_B;
			return path1;
		}
	}

	// Find a suitable pivot.
	if (find(path1.begin(), path1.end(), pivot) == path1.end()
			|| find(path2.begin(), path2.end(), pivot)
				== path2.end()) {
		bool good;
		tie(pivot, good) = findPivot(path1, path2);
		if (!good)
			return ContigPath();
	}
	assert(find(path1.begin(), path1.end(), pivot) != path1.end());

	ContigPath::const_iterator it2 = find(path2.begin(), path2.end(),
			pivot);
	assert(it2 != path2.end());
	if (&path1 != &path2) {
		// The seed must be unique in path2, unless we're aligning a
		// path to itself.
		assert(count(it2+1, path2.end(), pivot) == 0);
	}

	ContigPath consensus;
	for (ContigPath::const_iterator it1 = find_if(
				path1.begin(), path1.end(),
				bind2nd(equal_to<ContigNode>(), pivot));
			it1 != path1.end();
			it1 = find_if(it1+1, path1.end(),
				bind2nd(equal_to<ContigNode>(), pivot))) {
		if (&*it1 == &*it2) {
			// We are aligning a path to itself, and this is the
			// trivial alignment, which we'll ignore.
			continue;
		}
		consensus = align(lengths,
				path1, path2, it1, it2, orientation);
		if (!consensus.empty())
			return consensus;
	}
	return consensus;
}

/** Find an equivalent region of the two specified paths.
 * @return the consensus sequence
 */
static ContigPath align(const Lengths& lengths,
		const ContigPath& path1, const ContigPath& path2,
		ContigNode pivot)
{
	dir_type orientation;
	return align(lengths, path1, path2, pivot, orientation);
}
