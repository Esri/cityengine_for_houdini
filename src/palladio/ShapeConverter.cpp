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

#include "ShapeConverter.h"
#include "AttributeConversion.h"
#include "LogHandler.h"
#include "MultiWatch.h"
#include "PrimitiveClassifier.h"
#include "ShapeData.h"
#include "Utils.h"

#include "GA/GA_PageHandle.h"
#include "GEO/GEO_PrimPolySoup.h"
#include "GQ/GQ_Detail.h"
#include "GQ/GQ_Face.h"
#include "GU/GU_Detail.h"
#include "UT/UT_String.h"

#include <numeric>
#include <variant>

namespace {

constexpr bool DBG = true;

struct UV {
	std::vector<double> uvs;
	std::vector<uint32_t> idx;
};

// transfer texture coordinates
// clang-format off
const std::vector<std::string> UV_ATTR_NAMES {
	"uv", "uv1", "uv2", "uv3", "uv4", "uv5"
#if PRT_VERSION_MAJOR > 1
	, "uv6", "uv7", "uv8", "uv9"
#endif
};
// clang-format on

struct ConversionHelper {
	std::vector<uint32_t> indices;
	std::vector<uint32_t> faceCounts;
	std::vector<uint32_t> holes;
	std::vector<UV> uvSets;

	const std::vector<double>& coords;

	explicit ConversionHelper(const std::vector<double>& c) : coords(c) {
		uvSets.resize(UV_ATTR_NAMES.size());
	}

	InitialShapeBuilderUPtr createInitialShape() const {
		InitialShapeBuilderUPtr isb(prt::InitialShapeBuilder::create());

		isb->setGeometry(coords.data(), coords.size(), indices.data(), indices.size(), faceCounts.data(),
		                 faceCounts.size(), holes.data(), holes.size());

		for (size_t u = 0; u < uvSets.size(); u++) {
			const auto& uvSet = uvSets[u];
			if (!uvSet.uvs.empty()) {
				isb->setUVs(uvSet.uvs.data(), uvSet.uvs.size(), uvSet.idx.data(), uvSet.idx.size(), faceCounts.data(),
				            faceCounts.size(), u);
			}
		}

		return isb;
	}
};

std::pair<std::unique_ptr<GU_Detail>, std::unique_ptr<GQ_Detail>> extractHoles(GU_Detail const* detail,
                                                                               const GA_Primitive& p) {
	GA_PrimitiveGroup holeGroup(*detail);
	holeGroup.add(p);

	auto holeDetail = std::make_unique<GU_Detail>(detail, &holeGroup);

	auto gqd = std::make_unique<GQ_Detail>(holeDetail.get(), nullptr, -1.0f);
	gqd->unHole(0);

	UT_Options defragOpts;
	defragOpts.setOptionB("removeholes", true);
	holeDetail->defragment(&defragOpts);
	//holeDetail->sortVertexMapByPrimitiveUse();

	return std::make_pair(std::move(holeDetail), std::move(gqd));
}

void convertPolygon(ConversionHelper& ch, GU_Detail const* detail, GEO_PrimPolySoup::PolygonIterator const& p) {
	std::vector<GA_ROHandleV2D> uvHandles(UV_ATTR_NAMES.size());
	for (uint32_t uvSet = 0; uvSet < UV_ATTR_NAMES.size(); uvSet++) {
		const std::string& attrName = UV_ATTR_NAMES[uvSet];
		const GA_Attribute* attrib = detail->findFloatTuple(GA_ATTRIB_VERTEX, attrName, 2);
		uvHandles[uvSet].bind(attrib);
	}

	const GA_Size vtxCnt = p.getVertexCount();
	ch.faceCounts.push_back(static_cast<uint32_t>(vtxCnt));

	for (GA_Size i = vtxCnt - 1; i >= 0; i--) {
		ch.indices.push_back(static_cast<uint32_t>(p.getPointIndex(i)));
		if constexpr (DBG)
			LOG_DBG << "      vtx " << i << ": point idx = " << p.getPointIndex(i);
	}

	for (size_t u = 0; u < uvHandles.size(); u++) {
		const auto& uvh = uvHandles[u];
		if (uvh.isInvalid())
			continue;

		auto& uvSet = ch.uvSets[u];

		for (GA_Size i = vtxCnt - 1; i >= 0; i--) {
			GA_Offset off = p.getVertexOffset(i);
			const auto v = uvh.get(off);
			uvSet.uvs.push_back(v.x());
			uvSet.uvs.push_back(v.y());
			uvSet.idx.push_back(uvSet.idx.size());
			if constexpr (DBG)
				LOG_DBG << "     uv " << i << ": " << v.x() << ", " << v.y();
		}
	}
}

void convertPolygon(ConversionHelper& ch, GU_Detail const* detail, GA_Primitive const& p) {
	const GA_Size vtxCnt = p.getVertexCount();

	std::pair<std::unique_ptr<GU_Detail>, std::unique_ptr<GQ_Detail>> holeDetail = extractHoles(detail, p);

	if (!holeDetail.first) {
		std::vector<GA_ROHandleV2D> uvHandles(UV_ATTR_NAMES.size());
		for (uint32_t uvSet = 0; uvSet < UV_ATTR_NAMES.size(); uvSet++) {
			const std::string& attrName = UV_ATTR_NAMES[uvSet];
			const GA_Attribute* attrib = detail->findFloatTuple(GA_ATTRIB_VERTEX, attrName, 2);
			uvHandles[uvSet].bind(attrib);
		}

		ch.faceCounts.push_back(static_cast<uint32_t>(vtxCnt));

		for (GA_Size i = vtxCnt - 1; i >= 0; i--) {
			ch.indices.push_back(static_cast<uint32_t>(p.getPointIndex(i)));
			if constexpr (DBG)
				LOG_DBG << "      vtx " << i << ": point idx = " << p.getPointIndex(i);
		}

		for (size_t u = 0; u < uvHandles.size(); u++) {
			const auto& uvh = uvHandles[u];
			if (uvh.isInvalid())
				continue;

			auto& uvSet = ch.uvSets[u];

			for (GA_Size i = vtxCnt - 1; i >= 0; i--) {
				GA_Offset off = p.getVertexOffset(i);
				const auto v = uvh.get(off);
				uvSet.uvs.push_back(v.x());
				uvSet.uvs.push_back(v.y());
				uvSet.idx.push_back(uvSet.idx.size());
				if constexpr (DBG)
					LOG_DBG << "     uv " << i << ": " << v.x() << ", " << v.y();
			}
		}
	}
	else {
		std::vector<GA_ROHandleV2D> uvHandles(UV_ATTR_NAMES.size());
		for (uint32_t uvSet = 0; uvSet < UV_ATTR_NAMES.size(); uvSet++) {
			const std::string& attrName = UV_ATTR_NAMES[uvSet];
			const GA_Attribute* attrib = holeDetail.first->findFloatTuple(GA_ATTRIB_VERTEX, attrName, 2);
			uvHandles[uvSet].bind(attrib);
		}

		GA_Size const primCount = holeDetail.first->getNumPrimitives();

		ch.faceCounts.reserve(ch.faceCounts.size() + primCount);
		ch.holes.reserve(ch.holes.size() + primCount + 1);
		ch.indices.reserve(ch.indices.size() + vtxCnt);

		for (GA_Offset i = 0; i < primCount; i++) {
			GA_Primitive const* prim = holeDetail.first->getPrimitive(i);
			ch.holes.push_back(ch.faceCounts.size());
			ch.faceCounts.push_back(static_cast<uint32_t>(prim->getVertexCount()));

			std::vector<int32_t> indices(prim->getVertexCount());
			for (GA_Offset j = 0; j < prim->getVertexCount(); j++) {
				indices[j] = static_cast<int32_t>(prim->getPointIndex(j));
			}

			//			if (i == 0)
			ch.indices.insert(ch.indices.end(), indices.rbegin(), indices.rend());
			//			else
			//				ch.indices.insert(ch.indices.end(), indices.begin(), indices.end());
		}

		// required by PRT to delimit the holes belonging to a face
		ch.holes.push_back(std::numeric_limits<uint32_t>::max());

		for (size_t u = 0; u < uvHandles.size(); u++) {
			const auto& uvh = uvHandles[u];
			if (uvh.isInvalid())
				continue;

			auto& uvSet = ch.uvSets[u];

			for (GA_Offset i = 0; i < primCount; i++) {
				GA_Primitive const* prim = holeDetail.first->getPrimitive(i);

				for (GA_Offset j = 0; j < prim->getVertexCount(); j++) {
					GA_Offset vtxOff = holeDetail.first->getPrimitiveVertexOffset(holeDetail.first->primitiveOffset(i), j);
					const auto v = uvh.get(vtxOff);
					uvSet.uvs.push_back(v.x());
					uvSet.uvs.push_back(v.y());
					LOG_DBG << "uv " << j << ", vtxOff " << vtxOff << " = " << v.x() << ", " << v.y();
				}

				std::vector<int32_t> indices(prim->getVertexCount());
				std::iota(indices.begin(), indices.end(), uvSet.idx.size());
				//				if (i == 0)
				uvSet.idx.insert(uvSet.idx.end(), indices.rbegin(), indices.rend());
				//				else
				//					uvSet.idx.insert(uvSet.idx.end(), indices.begin(), indices.end());
			}
		}
	}
}

std::array<double, 3> getCentroid(const std::vector<double>& coords, const ConversionHelper& ch) {
	std::array<double, 3> centroid = {0.0, 0.0, 0.0};
	for (size_t i = 0; i < ch.indices.size(); i++) {
		auto idx = ch.indices[i];
		centroid[0] += coords[3 * idx + 0];
		centroid[1] += coords[3 * idx + 1];
		centroid[2] += coords[3 * idx + 2];
	}
	centroid[0] /= (double)ch.indices.size();
	centroid[1] /= (double)ch.indices.size();
	centroid[2] /= (double)ch.indices.size();
	return centroid;
}

// try to get random seed from incoming primitive attributes (important for default rule attr eval)
// use centroid based hash as fallback
int32_t getRandomSeed(const GA_Detail* detail, const GA_Offset& primOffset, const std::vector<double>& coords,
                      const ConversionHelper& ch) {
	int32_t randomSeed = 0;

	GA_ROAttributeRef seedRef(detail->findPrimitiveAttribute(PLD_RANDOM_SEED));
	if (!seedRef.isInvalid() && (seedRef->getStorageClass() == GA_STORECLASS_INT)) { // TODO: check for 32bit
		GA_ROHandleI seedH(seedRef);
		randomSeed = seedH.get(primOffset);
	}
	else {
		const std::array<double, 3> centroid = getCentroid(coords, ch);
		size_t hash = 0;
		hash_combine(hash, std::hash<double>{}(centroid[0]));
		hash_combine(hash, std::hash<double>{}(centroid[1]));
		hash_combine(hash, std::hash<double>{}(centroid[2]));
		randomSeed = static_cast<int32_t>(hash); // TODO: do we still get a good hash with this truncation?
	}

	return randomSeed;
}

} // namespace

struct MainAttributeHandles {
	GA_RWHandleS rpk;
	GA_RWHandleS ruleFile;
	GA_RWHandleS startRule;
	GA_RWHandleS style;
	GA_RWHandleI seed;

	void setup(GU_Detail* detail) {
		GA_RWAttributeRef rpkRef(detail->addStringTuple(GA_ATTRIB_PRIMITIVE, PLD_RPK, 1));
		rpk.bind(rpkRef);

		GA_RWAttributeRef ruleFileRef(detail->addStringTuple(GA_ATTRIB_PRIMITIVE, PLD_RULE_FILE, 1));
		ruleFile.bind(ruleFileRef);

		GA_RWAttributeRef startRuleRef(detail->addStringTuple(GA_ATTRIB_PRIMITIVE, PLD_START_RULE, 1));
		startRule.bind(startRuleRef);

		GA_RWAttributeRef styleRef(detail->addStringTuple(GA_ATTRIB_PRIMITIVE, PLD_STYLE, 1));
		style.bind(styleRef);

		GA_RWAttributeRef seedRef(detail->addIntTuple(GA_ATTRIB_PRIMITIVE, PLD_RANDOM_SEED, 1, GA_Defaults(0), nullptr,
		                                              nullptr, GA_STORE_INT32));
		seed.bind(seedRef);
	}
};

void ShapeConverter::get(const GU_Detail* detail, const PrimitiveClassifier& primCls, ShapeData& shapeData,
                         const PRTContextUPtr& prtCtx) {
	WA("all");

	// -- partition primitives into initial shapes by primitive classifier values
	PrimitivePartition primPart(detail, primCls);
	const PrimitivePartition::PartitionMap& partitions = primPart.get();

	// -- copy all coordinates
	std::vector<double> coords;
	assert(detail->getPointRange().getEntries() == detail->getNumPoints());
	coords.reserve(detail->getNumPoints() * 3);
	GA_Offset ptoff;
	GA_FOR_ALL_PTOFF(detail, ptoff) {

#if (HOUDINI_VERSION_MAJOR < 19 || (HOUDINI_VERSION_MAJOR == 19 && HOUDINI_VERSION_MINOR == 0))
		const UT_Vector3D p = detail->getPos3(ptoff);
#else
		const UT_Vector3D p = detail->getPos3D(ptoff);
#endif

		if constexpr (DBG)
			LOG_DBG << "coords " << coords.size() / 3 << ": " << p.x() << ", " << p.y() << ", " << p.z();
		coords.push_back(p.x());
		coords.push_back(p.y());
		coords.push_back(p.z());
	}

	// -- loop over all primitive partitions and create shape builders
	uint32_t isIdx = 0;
	for (auto pIt = partitions.cbegin(); pIt != partitions.cend(); ++pIt, ++isIdx) {
		if constexpr (DBG)
			LOG_DBG << "   -- creating initial shape " << isIdx << ", prim count = " << pIt->second.size();

		ConversionHelper ch(coords);

		// merge primitive geometry inside partition (potential multi-polygon initial shape)
		for (const auto& prim : pIt->second) {
			if constexpr (DBG)
				LOG_DBG << "   -- prim index " << prim->getMapIndex() << ", type: " << prim->getTypeName()
				        << ", id = " << prim->getTypeId().get();
			const auto& primType = prim->getTypeId();
			switch (primType.get()) {
				case GA_PRIMPOLY:
					convertPolygon(ch, detail, *prim);
					break;
				case GA_PRIMPOLYSOUP:
					for (GEO_PrimPolySoup::PolygonIterator pit(static_cast<const GEO_PrimPolySoup&>(*prim));
					     !pit.atEnd(); ++pit) {
						convertPolygon(ch, detail, pit);
					}
					break;
				default:
					if constexpr (DBG)
						LOG_DBG << "      ignoring primitive of type " << prim->getTypeName();
					break;
			}
		} // for each primitive

		const int32_t randomSeed = getRandomSeed(detail, pIt->second.front()->getMapOffset(), coords, ch);
		InitialShapeBuilderUPtr isb = ch.createInitialShape();
		shapeData.addBuilder(std::move(isb), randomSeed, pIt->second, pIt->first);
	} // for each primitive partition

	assert(shapeData.isValid());
}

void ShapeConverter::put(GU_Detail* detail, PrimitiveClassifier& primCls, const ShapeData& shapeData) const {
	WA("all");

	primCls.setupAttributeHandles(detail);

	MainAttributeHandles mah;
	mah.setup(detail);

	for (size_t isIdx = 0; isIdx < shapeData.getRuleAttributeMapBuilders().size(); isIdx++) {
		const auto& pv = shapeData.getPrimitiveMapping(isIdx);
		const int32_t randomSeed = shapeData.getInitialShapeRandomSeed(isIdx);

		for (auto& prim : pv) {
			primCls.put(prim);
			putMainAttributes(detail, mah, prim);
			const GA_Offset& off = prim->getMapOffset();
			if (mDefaultMainAttributes.mOverrideSeed) {
				mah.seed.set(off, mDefaultMainAttributes.mSeed);
			}
			else {
				mah.seed.set(off, randomSeed);
			}
		} // for all primitives in initial shape
	} // for all initial shapes
}

void ShapeConverter::getMainAttributes(SOP_Node* node, const OP_Context& context) {
	const fpreal now = context.getTime();
	mDefaultMainAttributes.mRPK = AssignNodeParams::getRPK(node, now);
	mDefaultMainAttributes.mStyle = AssignNodeParams::getStyle(node, now);
	mDefaultMainAttributes.mStartRule = AssignNodeParams::getStartRule(node, now);
	mDefaultMainAttributes.mSeed = AssignNodeParams::getSeed(node, now);
	mDefaultMainAttributes.mOverrideSeed = AssignNodeParams::getOverrideSeed(node, now);
}

namespace {

template <typename T>
T convert(const UT_StringHolder& s) {
	return T{s.toStdString()};
}

template <>
std::wstring convert(const UT_StringHolder& s) {
	return toUTF16FromOSNarrow(s.toStdString());
}

template <typename T>
void tryAssign(T& v, const GA_ROAttributeRef& ref, const GA_Offset& off) {
	if (ref.isInvalid())
		return;

	GA_ROHandleS h(ref);
	const UT_StringHolder& s = h.get(off);

	T t = convert<T>(s);
	if (!t.empty())
		v = t;
}

} // namespace

MainAttributes ShapeConverter::getMainAttributesFromPrimitive(const GU_Detail* detail, const GA_Primitive* prim) const {
	MainAttributes ma = mDefaultMainAttributes;
	const GA_Offset firstOffset = prim->getMapOffset();

	GA_ROAttributeRef rpkRef(detail->findPrimitiveAttribute(PLD_RPK));
	tryAssign(ma.mRPK, rpkRef, firstOffset);

	GA_ROAttributeRef startRuleRef(detail->findPrimitiveAttribute(PLD_START_RULE));
	tryAssign(ma.mStartRule, startRuleRef, firstOffset);

	GA_ROAttributeRef styleRef(detail->findPrimitiveAttribute(PLD_STYLE));
	tryAssign(ma.mStyle, styleRef, firstOffset);

	return ma;
}

void ShapeConverter::putMainAttributes(const GU_Detail* detail, MainAttributeHandles& mah,
                                       const GA_Primitive* primitive) const {
	MainAttributes ma = getMainAttributesFromPrimitive(detail, primitive);

	const GA_Offset& off = primitive->getMapOffset();
	mah.rpk.set(off, ma.mRPK.string().c_str());
	mah.startRule.set(off, toOSNarrowFromUTF16(ma.mStartRule).c_str());
	mah.style.set(off, toOSNarrowFromUTF16(ma.mStyle).c_str());
	if (ma.mOverrideSeed)
		mah.seed.set(off, ma.mSeed);
}
