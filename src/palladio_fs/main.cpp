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

#include "RulePackageFS.h"

#include "palladio/Utils.h"

#include "FS/FS_Utils.h"      // required for installFSHelpers to work below
#include "UT/UT_DSOVersion.h" // required for valid Houdini DSO

#include <iostream>
#include <memory>

namespace {

CacheObjectUPtr prtCache; // TODO: prevent from growing too much

std::unique_ptr<RulePackageReader> rpkReader;
std::unique_ptr<RulePackageInfoHelper> rpkInfoHelper;

} // namespace

void installFSHelpers() {
	prtCache.reset(prt::CacheObject::create(prt::CacheObject::CACHE_TYPE_NONREDUNDANT));

	rpkReader = std::make_unique<RulePackageReader>(prtCache.get());
	rpkInfoHelper = std::make_unique<RulePackageInfoHelper>(prtCache.get());

	std::clog << "CityEngine for Houdini: Registered custom FS reader for Rule Packages.\n";
}
