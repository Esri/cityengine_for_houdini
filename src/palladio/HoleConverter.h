/*
 * Copyright 2014-2020 Esri R&D Zurich and VRBN
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

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace HoleConverter {

using Edge = std::pair<int64_t, int64_t>;
using Edges = std::vector<Edge>;

struct EdgeSource {
	virtual Edges getEdges() const = 0;
	virtual int64_t getPointIndex(int64_t vertexIndex) const = 0;
	virtual bool isBridge(int64_t pointIndexA, int64_t pointIndexB) const = 0;
};

using FaceOrHoleIndices = std::vector<int64_t>;
using FaceWithHoles = std::vector<FaceOrHoleIndices>; // first item is outer ring/face

FaceWithHoles extractHoles(EdgeSource const& source);

} // namespace HoleConverter
