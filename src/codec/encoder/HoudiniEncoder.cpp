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

#include "HoudiniEncoder.h"
#include "HoudiniCallbacks.h"

#include "prtx/Attributable.h"
#include "prtx/Exception.h"
#include "prtx/ExtensionManager.h"
#include "prtx/GenerateContext.h"
#include "prtx/Log.h"
#include "prtx/Material.h"
#include "prtx/ReportsCollector.h"
#include "prtx/ShapeIterator.h"
#include "prtx/URI.h"

#include <algorithm>
#include <cassert>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>
#include <vector>

namespace {

constexpr bool DBG = false;

constexpr const wchar_t* ENC_NAME = L"SideFX(tm) Houdini(tm) Encoder";
constexpr const wchar_t* ENC_DESCRIPTION = L"Encodes geometry into the Houdini format.";

std::vector<const wchar_t*> toPtrVec(const prtx::WStringVector& wsv) {
	std::vector<const wchar_t*> pw(wsv.size());
	for (size_t i = 0; i < wsv.size(); i++)
		pw[i] = wsv[i].c_str();
	return pw;
}

template <typename T>
std::pair<std::vector<const T*>, std::vector<size_t>> toPtrVec(const std::vector<std::vector<T>>& v) {
	std::vector<const T*> pv(v.size());
	std::vector<size_t> ps(v.size());
	for (size_t i = 0; i < v.size(); i++) {
		pv[i] = v[i].data();
		ps[i] = v[i].size();
	}
	return std::make_pair(pv, ps);
}

std::wstring uriToPath(const prtx::TexturePtr& t) {
	return t->getURI()->getPath();
}

// we blacklist all CGA-style material attribute keys, see prtx/Material.h
const std::set<std::wstring> MATERIAL_ATTRIBUTE_BLACKLIST = {
        L"ambient.b",
        L"ambient.g",
        L"ambient.r",
        L"bumpmap.rw",
        L"bumpmap.su",
        L"bumpmap.sv",
        L"bumpmap.tu",
        L"bumpmap.tv",
        L"color.a",
        L"color.b",
        L"color.g",
        L"color.r",
        L"color.rgb",
        L"colormap.rw",
        L"colormap.su",
        L"colormap.sv",
        L"colormap.tu",
        L"colormap.tv",
        L"dirtmap.rw",
        L"dirtmap.su",
        L"dirtmap.sv",
        L"dirtmap.tu",
        L"dirtmap.tv",
        L"normalmap.rw",
        L"normalmap.su",
        L"normalmap.sv",
        L"normalmap.tu",
        L"normalmap.tv",
        L"opacitymap.rw",
        L"opacitymap.su",
        L"opacitymap.sv",
        L"opacitymap.tu",
        L"opacitymap.tv",
        L"specular.b",
        L"specular.g",
        L"specular.r",
        L"specularmap.rw",
        L"specularmap.su",
        L"specularmap.sv",
        L"specularmap.tu",
        L"specularmap.tv",
        L"bumpmap",
        L"colormap",
        L"dirtmap",
        L"normalmap",
        L"opacitymap",
        L"specularmap"

#if PRT_VERSION_MAJOR > 1
        // also blacklist CGA-style PBR attrs from CE 2019.0, PRT 2.x
        ,
        L"opacitymap.mode",
        L"emissive.b",
        L"emissive.g",
        L"emissive.r",
        L"emissivemap.rw",
        L"emissivemap.su",
        L"emissivemap.sv",
        L"emissivemap.tu",
        L"emissivemap.tv",
        L"metallicmap.rw",
        L"metallicmap.su",
        L"metallicmap.sv",
        L"metallicmap.tu",
        L"metallicmap.tv",
        L"occlusionmap.rw",
        L"occlusionmap.su",
        L"occlusionmap.sv",
        L"occlusionmap.tu",
        L"occlusionmap.tv",
        L"roughnessmap.rw",
        L"roughnessmap.su",
        L"roughnessmap.sv",
        L"roughnessmap.tu",
        L"roughnessmap.tv",
        L"emissivemap",
        L"metallicmap",
        L"occlusionmap",
        L"roughnessmap"
#endif
};

void convertMaterialToAttributeMap(prtx::PRTUtils::AttributeMapBuilderPtr& aBuilder, const prtx::Material& prtxAttr,
                                   const prtx::WStringVector& keys) {
	if constexpr (DBG)
		log_debug(L"-- converting material: %1%") % prtxAttr.name();
	for (const auto& key : keys) {
		if (MATERIAL_ATTRIBUTE_BLACKLIST.count(key) > 0)
			continue;

		if constexpr (DBG)
			log_debug(L"   key: %1%") % key;

		switch (prtxAttr.getType(key)) {
			case prt::Attributable::PT_BOOL:
				aBuilder->setBool(key.c_str(), prtxAttr.getBool(key) == prtx::PRTX_TRUE);
				break;

			case prt::Attributable::PT_FLOAT:
				aBuilder->setFloat(key.c_str(), prtxAttr.getFloat(key));
				break;

			case prt::Attributable::PT_INT:
				aBuilder->setInt(key.c_str(), prtxAttr.getInt(key));
				break;

			case prt::Attributable::PT_STRING: {
				const std::wstring& v = prtxAttr.getString(key); // explicit copy
				aBuilder->setString(key.c_str(), v.c_str());     // also passing on empty strings
				break;
			}

			case prt::Attributable::PT_BOOL_ARRAY: {
				const std::vector<uint8_t>& ba = prtxAttr.getBoolArray(key);
				auto boo = std::unique_ptr<bool[]>(new bool[ba.size()]);
				for (size_t i = 0; i < ba.size(); i++)
					boo[i] = (ba[i] == prtx::PRTX_TRUE);
				aBuilder->setBoolArray(key.c_str(), boo.get(), ba.size());
				break;
			}

			case prt::Attributable::PT_INT_ARRAY: {
				const std::vector<int32_t>& array = prtxAttr.getIntArray(key);
				aBuilder->setIntArray(key.c_str(), &array[0], array.size());
				break;
			}

			case prt::Attributable::PT_FLOAT_ARRAY: {
				const std::vector<double>& array = prtxAttr.getFloatArray(key);
				aBuilder->setFloatArray(key.c_str(), array.data(), array.size());
				break;
			}

			case prt::Attributable::PT_STRING_ARRAY: {
				const prtx::WStringVector& a = prtxAttr.getStringArray(key);
				std::vector<const wchar_t*> pw = toPtrVec(a);
				aBuilder->setStringArray(key.c_str(), pw.data(), pw.size());
				break;
			}

			case prtx::Material::PT_TEXTURE: {
				const auto& t = prtxAttr.getTexture(key);
				const std::wstring p = t->getURI()->wstring();
				aBuilder->setString(key.c_str(), p.c_str());
				break;
			}

			case prtx::Material::PT_TEXTURE_ARRAY: {
				const auto& ta = prtxAttr.getTextureArray(key);

				prtx::WStringVector pa(ta.size());
				std::transform(ta.begin(), ta.end(), pa.begin(),
				               [](const prtx::TexturePtr& t) { return t->getURI()->wstring(); });

				std::vector<const wchar_t*> ppa = toPtrVec(pa);
				aBuilder->setStringArray(key.c_str(), ppa.data(), ppa.size());
				break;
			}

			default:
				if constexpr (DBG)
					log_debug(L"ignored atttribute '%s' with type %d") % key % prtxAttr.getType(key);
				break;
		}
	}
}

void convertReportsToAttributeMap(prtx::PRTUtils::AttributeMapBuilderPtr& amb, const prtx::ReportsPtr& r) {
	if (!r)
		return;

	for (const auto& b : r->mBools)
		amb->setBool(b.first->c_str(), b.second);
	for (const auto& f : r->mFloats)
		amb->setFloat(f.first->c_str(), f.second);
	for (const auto& s : r->mStrings)
		amb->setString(s.first->c_str(), s.second->c_str());
}

template <typename F>
void forEachKey(prt::Attributable const* a, F f) {
	if (a == nullptr)
		return;

	size_t keyCount = 0;
	wchar_t const* const* keys = a->getKeys(&keyCount);

	for (size_t k = 0; k < keyCount; k++) {
		wchar_t const* const key = keys[k];
		f(a, key);
	}
}

void forwardGenericAttributes(HoudiniCallbacks* hc, size_t initialShapeIndex, const prtx::InitialShape& initialShape,
                              const prtx::ShapePtr& shape) {
	forEachKey(initialShape.getAttributeMap(),
	           [&hc, &shape, &initialShapeIndex, &initialShape](prt::Attributable const* a, wchar_t const* key) {
		           assert(key != nullptr);
		           const std::wstring keyStr(key);

		           if (!shape->hasKey(keyStr))
			           return;

		           switch (shape->getType(keyStr)) {
			           case prtx::Attributable::PT_STRING: {
				           const auto v = shape->getString(keyStr);
				           hc->attrString(initialShapeIndex, shape->getID(), key, v.c_str());
				           break;
			           }
			           case prtx::Attributable::PT_FLOAT: {
				           const auto v = shape->getFloat(keyStr);
				           hc->attrFloat(initialShapeIndex, shape->getID(), key, v);
				           break;
			           }
			           case prtx::Attributable::PT_BOOL: {
				           const auto v = shape->getBool(keyStr);
				           hc->attrBool(initialShapeIndex, shape->getID(), key, (v == prtx::PRTX_TRUE));
				           break;
			           }
			           case prtx::Attributable::PT_STRING_ARRAY: {
				           const prtx::WStringVector& v = shape->getStringArray(keyStr);
				           const std::vector<const wchar_t*> vPtrs = toPtrVec(v);
				           hc->attrStringArray(initialShapeIndex, shape->getID(), key, vPtrs.data(), vPtrs.size(), 1);
				           break;
			           }
			           case prtx::Attributable::PT_FLOAT_ARRAY: {
				           const prtx::DoubleVector& v = shape->getFloatArray(keyStr);
				           hc->attrFloatArray(initialShapeIndex, shape->getID(), key, v.data(), v.size(), 1);
				           break;
			           }
			           case prtx::Attributable::PT_BOOL_ARRAY: {
				           const prtx::BoolVector& v = shape->getBoolArray(keyStr);
				           const std::unique_ptr<bool[]> vPtrs(new bool[v.size()]);
				           for (size_t i = 0; i < v.size(); i++)
					           vPtrs[i] = prtx::toPrimitive(v[i]);
				           hc->attrBoolArray(initialShapeIndex, shape->getID(), key, vPtrs.get(), v.size(), 1);
				           break;
			           }
			           default:
				           break;
		           }
	           });
}

using AttributeMapNOPtrVector = std::vector<const prt::AttributeMap*>;

struct AttributeMapNOPtrVectorOwner {
	AttributeMapNOPtrVector v;
	~AttributeMapNOPtrVectorOwner() {
		for (const auto& m : v) {
			if (m)
				m->destroy();
		}
	}
};

struct TextureUVMapping {
	std::wstring key;
	uint8_t index;
	int8_t uvSet;
};

const std::vector<TextureUVMapping> TEXTURE_UV_MAPPINGS = []() -> std::vector<TextureUVMapping> {
	return {
	        // shader key   | idx | uv set  | CGA key
	        {L"diffuseMap", 0, 0},  // colormap
	        {L"bumpMap", 0, 1},     // bumpmap
	        {L"diffuseMap", 1, 2},  // dirtmap
	        {L"specularMap", 0, 3}, // specularmap
	        {L"opacityMap", 0, 4},  // opacitymap
	        {L"normalMap", 0, 5}    // normalmap

#if PRT_VERSION_MAJOR > 1
	        ,
	        {L"emissiveMap", 0, 6},  // emissivemap
	        {L"occlusionMap", 0, 7}, // occlusionmap
	        {L"roughnessMap", 0, 8}, // roughnessmap
	        {L"metallicMap", 0, 9}   // metallicmap
#endif
	};
}();

// return the highest required uv set (where a valid texture is present)
uint32_t scanValidTextures(const prtx::MaterialPtr& mat) {
	int8_t highestUVSet = -1;
	for (const auto& t : TEXTURE_UV_MAPPINGS) {
		const auto& ta = mat->getTextureArray(t.key);
		if (ta.size() > t.index && ta[t.index]->isValid())
			highestUVSet = std::max(highestUVSet, t.uvSet);
	}
	if (highestUVSet < 0)
		return 0;
	else
		return highestUVSet + 1;
}

const prtx::DoubleVector EMPTY_UVS;
const prtx::IndexVector EMPTY_IDX;

} // namespace

namespace detail {

SerializedGeometry serializeGeometry(const prtx::GeometryPtrVector& geometries,
                                     const std::vector<prtx::MaterialPtrVector>& materials) {
	// PASS 1: scan
	uint32_t numCoords = 0;
	uint32_t numNormalCoords = 0;
	uint32_t numCounts = 0;
	uint32_t numHoles = 0;
	uint32_t numIndices = 0;
	uint32_t maxNumUVSets = 0;
	auto matsIt = materials.cbegin();
	for (const auto& geo : geometries) {
		const prtx::MeshPtrVector& meshes = geo->getMeshes();
		const prtx::MaterialPtrVector& mats = *matsIt;
		auto matIt = mats.cbegin();
		for (const auto& mesh : meshes) {
			numCoords += static_cast<uint32_t>(mesh->getVertexCoords().size());
			numNormalCoords += static_cast<uint32_t>(mesh->getVertexNormalsCoords().size());

			numCounts += mesh->getFaceCount();
			numHoles += mesh->getHolesCount();
			const auto& vtxCnts = mesh->getFaceVertexCounts();
			numIndices = std::accumulate(vtxCnts.begin(), vtxCnts.end(), numIndices);

			const prtx::MaterialPtr& mat = *matIt;
			const uint32_t requiredUVSetsByMaterial = scanValidTextures(mat);
			maxNumUVSets = std::max(maxNumUVSets, std::max(mesh->getUVSetsCount(), requiredUVSetsByMaterial));
			++matIt;
		}
		++matsIt;
	}
	detail::SerializedGeometry sg(numCoords, numNormalCoords, numCounts, numHoles, numIndices, maxNumUVSets);

	// PASS 2: copy
	uint32_t vertexIndexBase = 0;
	uint32_t normalIndexBase = 0;
	uint32_t faceIndexBase = 0;
	std::vector<uint32_t> uvIndexBases(maxNumUVSets, 0u);
	for (const auto& geo : geometries) {
		const prtx::MeshPtrVector& meshes = geo->getMeshes();
		for (const auto& mesh : meshes) {
			// append points
			const prtx::DoubleVector& verts = mesh->getVertexCoords();
			sg.coords.insert(sg.coords.end(), verts.begin(), verts.end());

			// append normals
			const prtx::DoubleVector& norms = mesh->getVertexNormalsCoords();
			sg.normals.insert(sg.normals.end(), norms.begin(), norms.end());

			// append uv sets (uv coords, counts, indices) with special cases:
			// - if mesh has no uv sets but maxNumUVSets is > 0, insert "0" uv face counts to keep in sync
			// - if mesh has less uv sets than maxNumUVSets, copy uv set 0 to the missing higher sets
			const uint32_t numUVSets = mesh->getUVSetsCount();
			const prtx::DoubleVector& uvs0 = (numUVSets > 0) ? mesh->getUVCoords(0) : EMPTY_UVS;
			const prtx::IndexVector faceUVCounts0 =
			        (numUVSets > 0) ? mesh->getFaceUVCounts(0) : prtx::IndexVector(mesh->getFaceCount(), 0);
			if constexpr (DBG)
				log_debug("-- mesh: numUVSets = %1%") % numUVSets;

			for (uint32_t uvSet = 0; uvSet < sg.uvs.size(); uvSet++) {
				// append texture coordinates
				const prtx::DoubleVector& uvs = (uvSet < numUVSets) ? mesh->getUVCoords(uvSet) : EMPTY_UVS;
				const auto& src = uvs.empty() ? uvs0 : uvs;
				auto& tgt = sg.uvs[uvSet];
				tgt.insert(tgt.end(), src.begin(), src.end());

				// append uv face counts
				const prtx::IndexVector& faceUVCounts =
				        (uvSet < numUVSets && !uvs.empty()) ? mesh->getFaceUVCounts(uvSet) : faceUVCounts0;
				assert(faceUVCounts.size() == mesh->getFaceCount());
				auto& tgtCnts = sg.uvCounts[uvSet];
				tgtCnts.insert(tgtCnts.end(), faceUVCounts.begin(), faceUVCounts.end());
				if constexpr (DBG)
					log_debug("   -- uvset %1%: face counts size = %2%") % uvSet % faceUVCounts.size();

				// append uv vertex indices
				for (uint32_t fi = 0, faceCount = static_cast<uint32_t>(faceUVCounts.size()); fi < faceCount; ++fi) {
					const uint32_t* faceUVIdx0 = (numUVSets > 0) ? mesh->getFaceUVIndices(fi, 0) : EMPTY_IDX.data();
					const uint32_t* faceUVIdx =
					        (uvSet < numUVSets && !uvs.empty()) ? mesh->getFaceUVIndices(fi, uvSet) : faceUVIdx0;
					const uint32_t faceUVCnt = faceUVCounts[fi];
					if constexpr (DBG)
						log_debug("      fi %1%: faceUVCnt = %2%, faceVtxCnt = %3%") % fi % faceUVCnt %
						        mesh->getFaceVertexCount(fi);
					for (uint32_t vi = 0; vi < faceUVCnt; vi++)
						sg.uvIndices[uvSet].push_back(uvIndexBases[uvSet] +
						                              faceUVIdx[faceUVCnt - vi - 1]); // reverse winding
				}

				uvIndexBases[uvSet] += static_cast<uint32_t>(src.size()) / 2u;
			} // for all uv sets

			// append counts and indices for vertices and vertex normals
			for (uint32_t fi = 0, faceCount = mesh->getFaceCount(); fi < faceCount; ++fi) {
				const uint32_t vtxCnt = mesh->getFaceVertexCount(fi);

				const uint32_t* vtxIdx = mesh->getFaceVertexIndices(fi);
				const uint32_t* nrmIdx = mesh->getFaceVertexNormalIndices(fi);
				const size_t nrmCnt = mesh->getFaceVertexNormalCount(fi);
				sg.counts.push_back(vtxCnt);
				for (uint32_t vi = 0; vi < vtxCnt; vi++) {
					uint32_t viReversed = vtxCnt - vi - 1; // reverse winding
					sg.vertexIndices.push_back(vertexIndexBase + vtxIdx[viReversed]);
					if (nrmCnt > viReversed && nrmIdx != nullptr)
						sg.normalIndices.push_back(normalIndexBase + nrmIdx[viReversed]);
				}

				const uint32_t holeCount = mesh->getFaceHolesCount(fi);
				sg.holeCounts.push_back(holeCount);

				const uint32_t* holesIndices = mesh->getFaceHolesIndices(fi);
				if (holeCount > 0 && holesIndices != nullptr) {
					for (uint32_t hi = 0; hi < holeCount; hi++)
						sg.holeIndices.push_back(holesIndices[hi] + faceIndexBase);
				}
			}

			vertexIndexBase += (uint32_t)verts.size() / 3u;
			normalIndexBase += (uint32_t)norms.size() / 3u;
			faceIndexBase += mesh->getFaceCount();
		} // for all meshes
	}     // for all geometries

	return sg;
}

} // namespace detail

HoudiniEncoder::HoudiniEncoder(const std::wstring& id, const prt::AttributeMap* options, prt::Callbacks* callbacks)
    : prtx::GeometryEncoder(id, options, callbacks) {}

void HoudiniEncoder::init(prtx::GenerateContext&) {
	prt::Callbacks* cb = getCallbacks();
	if constexpr (DBG)
		log_debug("HoudiniEncoder::init: cb = %x") % (size_t)cb;
	auto* oh = dynamic_cast<HoudiniCallbacks*>(cb);
	if constexpr (DBG)
		log_debug("                   oh = %x") % (size_t)oh;
	if (oh == nullptr)
		throw prtx::StatusException(prt::STATUS_ILLEGAL_CALLBACK_OBJECT);
}

void HoudiniEncoder::encode(prtx::GenerateContext& context, size_t initialShapeIndex) {
	const prtx::InitialShape& initialShape = *context.getInitialShape(initialShapeIndex);
	auto* cb = dynamic_cast<HoudiniCallbacks*>(getCallbacks());

	const bool emitAttrs = getOptions()->getBool(EO_EMIT_ATTRIBUTES);

	prtx::DefaultNamePreparator namePrep;
	prtx::NamePreparator::NamespacePtr nsMesh = namePrep.newNamespace();
	prtx::NamePreparator::NamespacePtr nsMaterial = namePrep.newNamespace();
	prtx::EncodePreparatorPtr encPrep = prtx::EncodePreparator::create(true, namePrep, nsMesh, nsMaterial);

	// generate geometry
	prtx::ReportsAccumulatorPtr reportsAccumulator{prtx::WriteFirstReportsAccumulator::create()};
	prtx::ReportingStrategyPtr reportsCollector{
	        prtx::LeafShapeReportingStrategy::create(context, initialShapeIndex, reportsAccumulator)};
	prtx::LeafIteratorPtr li = prtx::LeafIterator::create(context, initialShapeIndex);
	for (prtx::ShapePtr shape = li->getNext(); shape; shape = li->getNext()) {
		prtx::ReportsPtr r = reportsCollector->getReports(shape->getID());
		encPrep->add(context.getCache(), shape, initialShape.getAttributeMap(), r);

		// get final values of generic attributes
		if (emitAttrs)
			forwardGenericAttributes(cb, initialShapeIndex, initialShape, shape);
	}

	const bool triangulateFacesWithHoles = getOptions()->getBool(EO_TRIANGULATE_FACES_WITH_HOLES);

	const prtx::EncodePreparator::PreparationFlags encodePreparatorFlags =
	        prtx::EncodePreparator::PreparationFlags()
	                .instancing(false)
	                .meshMerging(prtx::MeshMerging::NONE)
	                .triangulate(false)
	                .processHoles(triangulateFacesWithHoles ? prtx::HoleProcessor::TRIANGULATE_FACES_WITH_HOLES
	                                                        : prtx::HoleProcessor::PASS)
	                .mergeVertices(true)
	                .cleanupVertexNormals(true)
	                .cleanupUVs(true)
	                .processVertexNormals(prtx::VertexNormalProcessor::SET_MISSING_TO_FACE_NORMALS)
	                .indexSharing(prtx::EncodePreparator::PreparationFlags::INDICES_SEPARATE_FOR_ALL_VERTEX_ATTRIBUTES);

	prtx::EncodePreparator::InstanceVector instances;
	encPrep->fetchFinalizedInstances(instances, encodePreparatorFlags);
	convertGeometry(initialShape, instances, cb);
}

void HoudiniEncoder::convertGeometry(const prtx::InitialShape& initialShape,
                                     const prtx::EncodePreparator::InstanceVector& instances, HoudiniCallbacks* cb) {
	const bool emitMaterials = getOptions()->getBool(EO_EMIT_MATERIALS);
	const bool emitReports = getOptions()->getBool(EO_EMIT_REPORTS);

	prtx::GeometryPtrVector geometries;
	std::vector<prtx::MaterialPtrVector> materials;
	std::vector<prtx::ReportsPtr> reports;
	std::vector<int32_t> shapeIDs;

	geometries.reserve(instances.size());
	materials.reserve(instances.size());
	reports.reserve(instances.size());
	shapeIDs.reserve(instances.size());

	for (const auto& inst : instances) {
		geometries.push_back(inst.getGeometry());
		materials.push_back(inst.getMaterials());
		reports.push_back(inst.getReports());
		shapeIDs.push_back(inst.getShapeId());
	}

	const detail::SerializedGeometry sg = detail::serializeGeometry(geometries, materials);

	if constexpr (DBG) {
		log_debug("resolvemap: %s") % prtx::PRTUtils::objectToXML(initialShape.getResolveMap());
		log_debug("encoder #materials = %s") % materials.size();
	}

	uint32_t faceCount = 0;
	std::vector<uint32_t> faceRanges;
	AttributeMapNOPtrVectorOwner matAttrMaps;
	AttributeMapNOPtrVectorOwner reportAttrMaps;

	assert(geometries.size() == reports.size());
	assert(materials.size() == reports.size());
	auto matIt = materials.cbegin();
	auto repIt = reports.cbegin();
	prtx::PRTUtils::AttributeMapBuilderPtr amb(prt::AttributeMapBuilder::create());
	for (const auto& geo : geometries) {
		const prtx::MeshPtrVector& meshes = geo->getMeshes();

		for (size_t mi = 0; mi < meshes.size(); mi++) {
			const prtx::MeshPtr& m = meshes.at(mi);
			const prtx::MaterialPtr& mat = matIt->at(mi);

			faceRanges.push_back(faceCount);

			if (emitMaterials) {
				convertMaterialToAttributeMap(amb, *(mat.get()), mat->getKeys());
				matAttrMaps.v.push_back(amb->createAttributeMapAndReset());
			}

			if (emitReports) {
				convertReportsToAttributeMap(amb, *repIt);
				reportAttrMaps.v.push_back(amb->createAttributeMapAndReset());
				if constexpr (DBG)
					log_debug("report attr map: %1%") % prtx::PRTUtils::objectToXML(reportAttrMaps.v.back());
			}

			faceCount += m->getFaceCount();
		}

		++matIt;
		++repIt;
	}
	faceRanges.push_back(faceCount); // close last range

	assert(matAttrMaps.v.empty() || matAttrMaps.v.size() == faceRanges.size() - 1);
	assert(reportAttrMaps.v.empty() || reportAttrMaps.v.size() == faceRanges.size() - 1);
	assert(shapeIDs.size() == faceRanges.size() - 1);

	assert(sg.uvs.size() == sg.uvCounts.size());
	assert(sg.uvs.size() == sg.uvIndices.size());

	auto puvs = toPtrVec(sg.uvs);
	auto puvCounts = toPtrVec(sg.uvCounts);
	auto puvIndices = toPtrVec(sg.uvIndices);

	assert(sg.uvs.size() == puvCounts.first.size());
	assert(sg.uvs.size() == puvCounts.second.size());

	cb->add(initialShape.getName(), sg.coords.data(), sg.coords.size(), sg.normals.data(), sg.normals.size(),
	        sg.counts.data(), sg.counts.size(), sg.holeCounts.data(), sg.holeCounts.size(), sg.holeIndices.data(),
	        sg.holeIndices.size(), sg.vertexIndices.data(), sg.vertexIndices.size(), sg.normalIndices.data(),
	        sg.normalIndices.size(),

	        puvs.first.data(), puvs.second.data(), puvCounts.first.data(), puvCounts.second.data(),
	        puvIndices.first.data(), puvIndices.second.data(), static_cast<uint32_t>(sg.uvs.size()),

	        faceRanges.data(), faceRanges.size(), matAttrMaps.v.empty() ? nullptr : matAttrMaps.v.data(),
	        reportAttrMaps.v.empty() ? nullptr : reportAttrMaps.v.data(), shapeIDs.data());

	if constexpr (DBG)
		log_debug("HoudiniEncoder::convertGeometry: end");
}

void HoudiniEncoder::finish(prtx::GenerateContext& /*context*/) {}

HoudiniEncoderFactory* HoudiniEncoderFactory::createInstance() {
	prtx::EncoderInfoBuilder encoderInfoBuilder;

	encoderInfoBuilder.setID(ENCODER_ID_HOUDINI);
	encoderInfoBuilder.setName(ENC_NAME);
	encoderInfoBuilder.setDescription(ENC_DESCRIPTION);
	encoderInfoBuilder.setType(prt::CT_GEOMETRY);

	prtx::PRTUtils::AttributeMapBuilderPtr amb(prt::AttributeMapBuilder::create());
	amb->setBool(EO_EMIT_ATTRIBUTES, prtx::PRTX_FALSE);
	amb->setBool(EO_EMIT_MATERIALS, prtx::PRTX_FALSE);
	amb->setBool(EO_EMIT_REPORTS, prtx::PRTX_FALSE);
	amb->setBool(EO_TRIANGULATE_FACES_WITH_HOLES, prtx::PRTX_TRUE);
	encoderInfoBuilder.setDefaultOptions(amb->createAttributeMap());

	return new HoudiniEncoderFactory(encoderInfoBuilder.create());
}
