#ifndef GRAPHALGORITHMS_H
#define GRAPHALGORITHMS_H 1

#include <boost/graph/graph_traits.hpp>
#include <cassert>
#include <vector>

using boost::graph_traits;

/**
 * Return the transitive edges.
 * Find the subset of edges (u,w) in E for which there exists a vertex
 * v such that the edges (u,v) and (v,w) exist in E.
 * This algorithm is not a general-purpose transitive reduction
 * algorithm. It is able to find transitive edges with exactly one
 * intermediate vertex.
 */
template <typename Graph, typename OutIt>
void find_transitive_edges(const Graph& g, OutIt out)
{
	typedef graph_traits<Graph> GTraits;
	typedef typename GTraits::adjacency_iterator adjacency_iterator;
	typedef typename GTraits::edge_descriptor edge_descriptor;
	typedef typename GTraits::out_edge_iterator out_edge_iterator;
	typedef typename GTraits::vertex_descriptor vertex_descriptor;
	typedef typename GTraits::vertex_iterator vertex_iterator;

	std::vector<bool> seen(num_vertices(g));
	std::pair<vertex_iterator, vertex_iterator> urange = vertices(g);
	for (vertex_iterator uit = urange.first;
			uit != urange.second; ++uit) {
		vertex_descriptor u = *uit;
		if (get(vertex_removed, g, u))
			continue;

		// Clear the colour of the adjacent vertices.
		std::pair<adjacency_iterator, adjacency_iterator>
			vrange = adjacent_vertices(u, g);
		for (adjacency_iterator vit = vrange.first;
				vit != vrange.second; ++vit)
			seen[get(vertex_index, g, *vit)] = false;

		// Set the colour of all vertices reachable in two hops.
		for (adjacency_iterator vit = vrange.first;
				vit != vrange.second; ++vit) {
			vertex_descriptor v = *vit;
			assert(u != v); // no self loops
			std::pair<adjacency_iterator, adjacency_iterator>
				wrange = adjacent_vertices(v, g);
			for (adjacency_iterator wit = wrange.first;
					wit != wrange.second; ++wit) {
				vertex_descriptor w = *wit;
				assert(v != w); // no self loops
				seen[get(vertex_index, g, w)] = true;
			}
		}

		// Find the transitive edges.
		std::pair<out_edge_iterator, out_edge_iterator>
			uvrange = out_edges(u, g);
		for (out_edge_iterator uvit = uvrange.first;
				uvit != uvrange.second; ++uvit) {
			edge_descriptor uv = *uvit;
			vertex_descriptor v = target(uv, g);
			if (seen[get(vertex_index, g, v)]) {
				// The edge (u,v) is transitive. Mark it for removal.
				*out++ = uv;
			}
		}
	}
}

/** Remove the edges [first,last) from g.
 * @return the number of removed edges
 */
template <typename Graph, typename It>
void remove_edges(Graph& g, It first, It last)
{
	for (It it = first; it != last; ++it)
		remove_edge(*it, g);
}

/**
 * Remove transitive edges from the specified graph.
 * Find and remove the subset of edges (u,w) in E for which there
 * exists a vertex v such that the edges (u,v) and (v,w) exist in E.
 * This algorithm is not a general-purpose transitive reduction
 * algorithm. It is able to find transitive edges with exactly one
 * intermediate vertex.
 * @return the number of transitive edges removed from g
 */
template <typename Graph>
unsigned remove_transitive_edges(Graph& g)
{
	typedef typename graph_traits<Graph>::edge_descriptor
		edge_descriptor;
	std::vector<edge_descriptor> transitive;
	find_transitive_edges(g, back_inserter(transitive));
	remove_edges(g, transitive.begin(), transitive.end());
	return transitive.size();
}

#endif
