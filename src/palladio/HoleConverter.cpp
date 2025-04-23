/*
 * Copyright 2014-2025 Esri R&D Zurich and VRBN
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "HoleConverter.h"

#include <set>

namespace HoleConverter {

struct undirectedLessThan {
	bool operator()(Edge a, Edge b) const {
		// treat (x,y) as (y,x)
		if (a.first > a.second)
			std::swap(a.first, a.second);
		if (b.first > b.second)
			std::swap(b.first, b.second);
		return (a < b);
	}
};

using UndirectedEdgeSet = std::set<Edge, undirectedLessThan>;

enum class BridgeEndCheck { EDGE_START, EDGE_END };

bool checkBridgeEnding(BridgeEndCheck endCheck, UndirectedEdgeSet const& bridges,
                       Edge const& startVertexToEndPointEdge) {
	auto checkStartPoint = [&startVertexToEndPointEdge](Edge const& bridge) {
		return startVertexToEndPointEdge.second == bridge.first;
	};
	auto checkEndPoint = [&startVertexToEndPointEdge](Edge const& bridge) {
		return startVertexToEndPointEdge.second == bridge.second;
	};
	auto it = [&endCheck, &bridges, &startVertexToEndPointEdge, &checkStartPoint, &checkEndPoint]() {
		if (endCheck == BridgeEndCheck::EDGE_START)
			return std::find_if(bridges.begin(), bridges.end(), checkStartPoint);
		else
			return std::find_if(bridges.begin(), bridges.end(), checkEndPoint);
	}();
	return (it != bridges.end());
}

FaceWithHoles detectFaceAndHoles(UndirectedEdgeSet const& bridges,
                                                Edges const& startVertexToEndPointEdges) {
	FaceWithHoles info(1); // insert the (yet empty) actual face

	size_t ringIdx = 0; // stack-like indexing to jump into the holes and back out again
	for (auto const& startVertexToEndPointEdge : startVertexToEndPointEdges) {

		// case 1: edge ends at the start of a bridge -> follow it inwards to the first/next hole
		if (checkBridgeEnding(BridgeEndCheck::EDGE_START, bridges, startVertexToEndPointEdge)) {
			info[ringIdx].push_back(startVertexToEndPointEdge.first);
			info.emplace_back();
			ringIdx++; // push the stack
		}

		// case 2: hole edge ends at the end of a bridge -> follow it "outward" to the previous hole or parent face
		else if (checkBridgeEnding(BridgeEndCheck::EDGE_END, bridges, startVertexToEndPointEdge)) {
			info[ringIdx].push_back(startVertexToEndPointEdge.first);
			ringIdx--; // pop the stack
		}

		// case 3: no bridge involved - add a vertex to the outer face or a hole
		else {
			info[ringIdx].push_back(startVertexToEndPointEdge.first);
		}

	} // for each edge

	return info;
}

// Note: there would be GQ_Detail::unhole which does the same as extractHoles, but it looses the UV coordinates on the
// hole primitives - until this is resolved, we need to roll our own hole extraction.
FaceWithHoles extractHoles(EdgeSource const& source) {
	Edges startVertexToEndPointEdges;
	UndirectedEdgeSet bridges;

	// we store unique bridges (i.e. only one direction) pointing from the outer face "inward" to the holes.
	// assumption: we start on the outer ring/face
	Edges const& allEdges = source.getEdges();
	for (auto const& [vertexIndexA, vertexIndexB] : allEdges) {
		int64_t pointIndexA = source.getPointIndex(vertexIndexA);
		int64_t pointIndexB = source.getPointIndex(vertexIndexB);
		if (source.isBridge(pointIndexA, pointIndexB)) {
			bridges.emplace(pointIndexA, pointIndexB);
		}
		else {
			// Store the VERTEX index for the edge start point, but the POINT index for the end point.
			// Only the end point index is needed to detect bridges when traversing the holes.
			// We need the vertex index later on to retrieve the correct per-vertex UV coordinates
			startVertexToEndPointEdges.emplace_back(vertexIndexA, pointIndexB);
		}
	}

	if (!bridges.empty()) {
		return detectFaceAndHoles(bridges, startVertexToEndPointEdges);
	}
	else {
		FaceWithHoles info(1);
		info.back().reserve(startVertexToEndPointEdges.size());
		for (auto const& startVertexToEndPointEdge : startVertexToEndPointEdges) {
			info.back().push_back(startVertexToEndPointEdge.first);
		}
		return info;
	}
}

} // namespace HoleConverter