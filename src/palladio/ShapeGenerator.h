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

#pragma once

#include "PRTContext.h"
#include "ShapeConverter.h"
#include "Utils.h"

class GU_Detail;

struct ShapeGenerator final : ShapeConverter {
	void get(const GU_Detail* detail, const PrimitiveClassifier& primCls, ShapeData& shapeData,
	         const PRTContextUPtr& prtCtx) override;
};
