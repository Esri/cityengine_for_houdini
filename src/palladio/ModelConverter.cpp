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

#include "ModelConverter.h"
#include "AttributeConversion.h"
#include "LogHandler.h"
#include "MultiWatch.h"
#include "ShapeConverter.h"

#include "GU/GU_HoleInfo.h"

#include <mutex>
#include <variant>

namespace {

constexpr bool DBG = false;

using UTVector3DVector = std::vector<UT_Vector3D>;

UTVector3DVector convertVertices(const double* vtx, size_t vtxSize) {
	UTVector3DVector utPoints;
	utPoints.reserve(vtxSize / 3);
	for (size_t pi = 0; pi < vtxSize; pi += 3) {
		utPoints.emplace_back(vtx[pi], vtx[pi + 1], vtx[pi + 2]);
	}
	return utPoints;
}

void setVertexNormals(GA_RWHandleV3& handle, const GA_Detail::OffsetMarker& marker, const double* nrm, size_t nrmSize,
                      const uint32_t* indices, size_t indicesSize) {
	uint32_t vi = 0;
	for (GA_Iterator it(marker.vertexRange()); !it.atEnd(); ++it, ++vi) {
		assert(vi < indicesSize);
		const auto nrmIdx = indices[vi];
		const auto nrmPos = nrmIdx * 3;
		assert(nrmPos + 2 < nrmSize);
		const auto nx = static_cast<fpreal32>(nrm[nrmPos + 0]);
		const auto ny = static_cast<fpreal32>(nrm[nrmPos + 1]);
		const auto nz = static_cast<fpreal32>(nrm[nrmPos + 2]);
		handle.set(it.getOffset(), UT_Vector3F(nx, ny, nz));
	}
}

std::mutex mDetailMutex; // guard the houdini detail object (and the hole groups)

GA_Offset createPrimitives(GU_Detail* mDetail, PrimitiveGroups& holeGroups, GroupCreation gc, const wchar_t* name,
                           const double* vtx, size_t vtxSize, const double* nrm, size_t nrmSize, const uint32_t* counts,
                           size_t countsSize, const uint32_t* holeCounts, size_t holeCountsSize,
                           const uint32_t* holeIndices, size_t holeIndicesSize, const uint32_t* vertexIndices,
                           size_t vertexIndicesSize, const uint32_t* normalIndices, size_t normalIndicesSize,
                           double const* const* uvs, size_t const* uvsSizes, uint32_t const* const* uvCounts,
                           size_t const* uvCountsSizes, uint32_t const* const* uvIndices, size_t const* uvIndicesSizes,
                           uint32_t uvSets) {
	WA("all");

	// -- create primitives
	const GA_Detail::OffsetMarker marker(*mDetail);
	const UTVector3DVector utPoints = convertVertices(vtx, vtxSize);
	const GEO_PolyCounts geoPolyCounts = [&counts, &countsSize]() {
		GEO_PolyCounts pc;
		for (size_t ci = 0; ci < countsSize; ci++)
			pc.append(counts[ci]);
		return pc;
	}();

	// allocate points with double precision
	GA_Offset pointStartOffset = mDetail->appendPointBlock(utPoints.size());
	for (size_t pi = 0; pi < utPoints.size(); pi++) {
		mDetail->setPos3(pointStartOffset + pi, utPoints[pi]);
	}

	// compute point index offsets for buildBlock
	std::vector<int> polyPointNumbers(vertexIndicesSize);
	for (size_t vi = 0; vi < vertexIndicesSize; vi++) {
		polyPointNumbers[vi] = mDetail->pointOffset(vertexIndices[vi]);
	}

	const GA_Offset primStartOffset =
	        GU_PrimPoly::buildBlock(mDetail, pointStartOffset, utPoints.size(), geoPolyCounts, polyPointNumbers.data());

	// -- add vertex normals
	if (nrmSize > 0) {
		GA_RWHandleV3 nrmh(mDetail->addNormalAttribute(GA_ATTRIB_VERTEX, GA_STORE_REAL32));
		setVertexNormals(nrmh, marker, nrm, vertexIndicesSize, normalIndices, normalIndicesSize);
	}

	// -- add texture coordinates
	for (size_t uvSet = 0; uvSet < uvSets; uvSet++) {
		size_t const psUVSSize = uvsSizes[uvSet];
		size_t const psUVCountsSize = uvCountsSizes[uvSet];
		size_t const psUVIndicesSize = uvIndicesSizes[uvSet];
		if constexpr (DBG)
			LOG_DBG << "-- uvset " << uvSet << ": psUVCountsSize = " << psUVCountsSize
			        << ", psUVIndicesSize = " << psUVIndicesSize;

		if (psUVSSize > 0 && psUVIndicesSize > 0 && psUVCountsSize > 0) {
			GA_RWHandleV3 uvh;
			if (uvSet == 0)
				uvh.bind(mDetail->addTextureAttribute(GA_ATTRIB_VERTEX, GA_STORE_REAL32)); // adds "uv" vertex attribute
			else {
				const std::string n = "uv" + std::to_string(uvSet);
				uvh.bind(mDetail->addTuple(GA_STORE_REAL32, GA_ATTRIB_VERTEX, GA_SCOPE_PUBLIC, n.c_str(), 3));
			}

			double const* const psUVS = uvs[uvSet];
			uint32_t const* const psUVCounts = uvCounts[uvSet];
			uint32_t const* const psUVIndices = uvIndices[uvSet];

			size_t fi = 0;
			size_t uvi = 0;
			for (GA_Iterator pit(marker.primitiveRange()); !pit.atEnd(); ++pit, ++fi) {
				GA_Primitive* prim = mDetail->getPrimitive(pit.getOffset());
				if constexpr (DBG)
					LOG_DBG << "   fi = " << fi << ": prim vtx cnt = " << prim->getVertexCount()
					        << ", vtx cnt = " << counts[fi] << ", uv cnt = " << psUVCounts[fi];

				if (psUVCounts[fi] > 0) {
					for (GA_Iterator vit(prim->getVertexRange()); !vit.atEnd(); ++vit, ++uvi) {
						if constexpr (DBG)
							LOG_DBG << "      vi = " << *vit << ": uvi = " << uvi;
						assert(uvi < psUVIndicesSize);
						const uint32_t uvIdx = psUVIndices[uvi];
						const auto du = psUVS[uvIdx * 2 + 0];
						const auto dv = psUVS[uvIdx * 2 + 1];
						const auto u = static_cast<fpreal32>(du);
						const auto v = static_cast<fpreal32>(dv);
						uvh.set(vit.getOffset(), UT_Vector3F(u, v, 0.0f));
					}
				}
			}
		}
	}

	// create temporary primitive groups of parent face and hole faces,
	// see ModelConverter::buildHoles for actual hole creation - we must not run buildHoles (which potentially deletes prims)
	// while we still might add more prims (more generated models)
	if (holeCountsSize > 0) {
		auto& elemGroupTable = mDetail->getElementGroupTable(GA_ATTRIB_PRIMITIVE);
		PrimitiveGroupDestroyer groupDestroyer(elemGroupTable);

		// collect the hole prims into groups
		size_t holeIndexPos = 0;
		for (size_t hi = 0; hi < holeCountsSize; hi++) {
			if (holeCounts[hi] > 0) {
				std::string groupName = "tempHoleGroup" + std::to_string(elemGroupTable.entries());
				PrimitiveGroupUPtr primGroup(static_cast<GA_PrimitiveGroup*>(elemGroupTable.newGroup(groupName, false)),
				                             groupDestroyer);
				primGroup->addIndex(primStartOffset + hi); // the parent face
				for (size_t hip = 0; hip < holeCounts[hi]; hip++, holeIndexPos++) {
					primGroup->addIndex(primStartOffset + holeIndices[holeIndexPos]);
				}
				holeGroups.push_back(std::move(primGroup));
			}
		}
	}

	// -- optionally create primitive groups
	if (gc == GroupCreation::PRIMCLS) {
		const std::string nName = toOSNarrowFromUTF16(name);
		auto& elemGroupTable = mDetail->getElementGroupTable(GA_ATTRIB_PRIMITIVE);
		GA_PrimitiveGroup* primGroup = static_cast<GA_PrimitiveGroup*>(elemGroupTable.newGroup(nName.c_str(), false));
		primGroup->addRange(marker.primitiveRange());
	}

	return primStartOffset;
}

} // namespace

ModelConverter::ModelConverter(GU_Detail* detail, GroupCreation gc, std::vector<prt::Status>& statuses,
                               UT_AutoInterrupt* autoInterrupt)
    : mDetail(detail), mGroupCreation(gc), mStatuses(statuses), mAutoInterrupt(autoInterrupt) {}

void ModelConverter::buildHoles() {
	// after all meshes have been added, we can run buildHoles (which might delete some prims)
	for (PrimitiveGroupUPtr& group : mHoleGroups) {
		mDetail->buildHoles(0.001f, 0.2f, 0, group.get());
	}
}

void ModelConverter::add(const wchar_t* name, const double* vtx, size_t vtxSize, const double* nrm, size_t nrmSize,
                         const uint32_t* counts, size_t countsSize, const uint32_t* holeCounts, size_t holeCountsSize,
                         const uint32_t* holeIndices, size_t holeIndicesSize, const uint32_t* vertexIndices,
                         size_t vertexIndicesSize, const uint32_t* normalIndices, size_t normalIndicesSize,
                         double const* const* uvs, size_t const* uvsSizes, uint32_t const* const* uvCounts,
                         size_t const* uvCountsSizes, uint32_t const* const* uvIndices, size_t const* uvIndicesSizes,
                         uint32_t uvSets, const uint32_t* faceRanges, size_t faceRangesSize,
                         const prt::AttributeMap** materials, const prt::AttributeMap** reports,
                         const int32_t* shapeIDs) {
	// we need to protect mDetail, it is accessed by multiple generate threads
	std::lock_guard<std::mutex> guard(mDetailMutex);

	const GA_Offset primStartOffset = createPrimitives(
	        mDetail, mHoleGroups, mGroupCreation, name, vtx, vtxSize, nrm, nrmSize, counts, countsSize, holeCounts,
	        holeCountsSize, holeIndices, holeIndicesSize, vertexIndices, vertexIndicesSize, normalIndices,
	        normalIndicesSize, uvs, uvsSizes, uvCounts, uvCountsSizes, uvIndices, uvIndicesSizes, uvSets);

	// -- convert materials/reports into primitive attributes based on face ranges
	if constexpr (DBG)
		LOG_DBG << "got " << faceRangesSize - 1 << " face ranges";
	if (faceRangesSize > 1) {
		WA("add materials/reports");

		AttributeConversion::ToHoudini toHoudini(mDetail);
		for (size_t fri = 0; fri < faceRangesSize - 1; fri++) {
			const GA_Offset rangeStart = primStartOffset + faceRanges[fri];
			const GA_Size rangeSize = faceRanges[fri + 1] - faceRanges[fri];

			if (materials != nullptr) {
				toHoudini.convert(materials[fri], rangeStart, rangeSize);
			}

			if (reports != nullptr) {
				toHoudini.convert(reports[fri], rangeStart, rangeSize);
			}

			if (!mShapeAttributeBuilders.empty()) {
				// implicit contract: the attr{Bool,Float,String} callbacks are called prior to ModelConverter::add
				const int32_t shapeID = shapeIDs[fri];
				auto it = mShapeAttributeBuilders.find(shapeID);
				if (it != mShapeAttributeBuilders.end()) {
					const AttributeMapUPtr attrMap(it->second->createAttributeMap());
					toHoudini.convert(attrMap.get(), rangeStart, rangeSize,
					                  AttributeConversion::ToHoudini::ArrayHandling::ARRAY);
				}
			}
		}
	}
}

prt::Status ModelConverter::generateError(size_t isIndex, prt::Status status, const wchar_t* message) {
	LOG_WRN << message; // generate error for one shape is not yet a reason to abort cooking
	mStatuses[isIndex] = status;
	return prt::STATUS_OK;
}

prt::Status ModelConverter::assetError(size_t isIndex, prt::CGAErrorLevel level, const wchar_t* key, const wchar_t* uri,
                                       const wchar_t* message) {
	LOG_WRN << key << L": " << message;
	return prt::STATUS_OK;
}

prt::Status ModelConverter::cgaError(size_t isIndex, int32_t shapeID, prt::CGAErrorLevel level, int32_t methodId,
                                     int32_t pc, const wchar_t* message) {
	LOG_WRN << message;
	return prt::STATUS_OK;
}

prt::Status ModelConverter::cgaPrint(size_t isIndex, int32_t shapeID, const wchar_t* txt) {
	LOG_INF << isIndex << ": " << shapeID << ": " << txt;
	return prt::STATUS_OK;
}

prt::Status ModelConverter::cgaReportBool(size_t isIndex, int32_t shapeID, const wchar_t* key, bool value) {
	return prt::STATUS_OK;
}

prt::Status ModelConverter::cgaReportFloat(size_t isIndex, int32_t shapeID, const wchar_t* key, double value) {
	return prt::STATUS_OK;
}

prt::Status ModelConverter::cgaReportString(size_t isIndex, int32_t shapeID, const wchar_t* key, const wchar_t* value) {
	return prt::STATUS_OK;
}

namespace {

AttributeMapBuilderUPtr& getBuilder(std::map<int32_t, AttributeMapBuilderUPtr>& amb, int32_t shapeID) {
	auto it = amb.find(shapeID);
	if (it == amb.end())
		it = amb.emplace(shapeID, std::move(AttributeMapBuilderUPtr(prt::AttributeMapBuilder::create()))).first;
	return it->second;
}

} // namespace

prt::Status ModelConverter::attrBool(size_t isIndex, int32_t shapeID, const wchar_t* key, bool value) {
	if constexpr (DBG)
		LOG_DBG << "attrBool: shapeID :" << shapeID << ", key: " << key << ", val: " << value;
	getBuilder(mShapeAttributeBuilders, shapeID)->setBool(key, value);
	return prt::STATUS_OK;
}

prt::Status ModelConverter::attrFloat(size_t isIndex, int32_t shapeID, const wchar_t* key, double value) {
	if constexpr (DBG)
		LOG_DBG << "attrFloat: shapeID :" << shapeID << ", key: " << key << ", val: " << value;
	getBuilder(mShapeAttributeBuilders, shapeID)->setFloat(key, value);
	return prt::STATUS_OK;
}

prt::Status ModelConverter::attrString(size_t isIndex, int32_t shapeID, const wchar_t* key, const wchar_t* value) {
	if constexpr (DBG)
		LOG_DBG << "attrString: shapeID :" << shapeID << ", key: " << key << ", val: " << value;
	getBuilder(mShapeAttributeBuilders, shapeID)->setString(key, value);
	return prt::STATUS_OK;
}

#if ((PRT_VERSION_MAJOR > 1 && PRT_VERSION_MINOR > 1) || PRT_VERSION_MAJOR > 2)

prt::Status ModelConverter::attrBoolArray(size_t isIndex, int32_t shapeID, const wchar_t* key, const bool* ptr,
                                          size_t size, size_t nRows) {
	if constexpr (DBG)
		LOG_DBG << "attrBoolArray: shapeID :" << shapeID << ", key: " << key << ", val: " << ptr << ", size: " << size;
	getBuilder(mShapeAttributeBuilders, shapeID)->setBoolArray(key, ptr, size);
	return prt::STATUS_OK;
}

prt::Status ModelConverter::attrFloatArray(size_t isIndex, int32_t shapeID, const wchar_t* key, const double* ptr,
                                           size_t size, size_t nRows) {
	if constexpr (DBG)
		LOG_DBG << "attrFloatArray: shapeID :" << shapeID << ", key: " << key << ", val: " << ptr << ", size: " << size;
	getBuilder(mShapeAttributeBuilders, shapeID)->setFloatArray(key, ptr, size);
	return prt::STATUS_OK;
}

prt::Status ModelConverter::attrStringArray(size_t isIndex, int32_t shapeID, const wchar_t* key,
                                            const wchar_t* const* ptr, size_t size, size_t nRows) {
	if constexpr (DBG)
		LOG_DBG << "attrStringArray: shapeID :" << shapeID << ", key: " << key << ", val: " << ptr
		        << ", size: " << size;
	getBuilder(mShapeAttributeBuilders, shapeID)->setStringArray(key, ptr, size);
	return prt::STATUS_OK;
}

#endif