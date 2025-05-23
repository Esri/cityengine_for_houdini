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

#include "PalladioMain.h"

#include "prt/AttributeMap.h"
#include "prt/EncoderInfo.h"
#include "prt/InitialShape.h"
#include "prt/Object.h"
#include "prt/OcclusionSet.h"
#include "prt/ResolveMap.h"
#include "prt/RuleFileInfo.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

constexpr const char* SCHEMA_RPK = "rpk:";

struct PRTDestroyer {
	void operator()(prt::Object const* p) {
		if (p)
			p->destroy();
	}
};

using ObjectUPtr = std::unique_ptr<const prt::Object, PRTDestroyer>;
using InitialShapeNOPtrVector = std::vector<const prt::InitialShape*>;
using AttributeMapNOPtrVector = std::vector<const prt::AttributeMap*>;
using CacheObjectUPtr = std::unique_ptr<prt::CacheObject, PRTDestroyer>;
using AttributeMapUPtr = std::unique_ptr<const prt::AttributeMap, PRTDestroyer>;
using AttributeMapVector = std::vector<AttributeMapUPtr>;
using AttributeMapBuilderUPtr = std::unique_ptr<prt::AttributeMapBuilder, PRTDestroyer>;
using AttributeMapBuilderVector = std::vector<AttributeMapBuilderUPtr>;
using InitialShapeBuilderUPtr = std::unique_ptr<prt::InitialShapeBuilder, PRTDestroyer>;
using InitialShapeBuilderVector = std::vector<InitialShapeBuilderUPtr>;
using ResolveMapSPtr = std::shared_ptr<const prt::ResolveMap>;
using ResolveMapUPtr = std::unique_ptr<const prt::ResolveMap, PRTDestroyer>;
using ResolveMapBuilderUPtr = std::unique_ptr<prt::ResolveMapBuilder, PRTDestroyer>;
using RuleFileInfoUPtr = std::unique_ptr<const prt::RuleFileInfo, PRTDestroyer>;
using EncoderInfoUPtr = std::unique_ptr<const prt::EncoderInfo, PRTDestroyer>;
using OcclusionSetUPtr = std::unique_ptr<prt::OcclusionSet, PRTDestroyer>;

PLD_TEST_EXPORTS_API std::vector<std::wstring> tokenizeAll(const std::wstring& input, wchar_t token);
PLD_TEST_EXPORTS_API std::pair<std::wstring, std::wstring> tokenizeFirst(const std::wstring& input, wchar_t token);

PLD_TEST_EXPORTS_API std::optional<std::pair<std::wstring, std::wstring>> getCGB(const ResolveMapSPtr& rm);
PLD_TEST_EXPORTS_API [[nodiscard]] const prt::AttributeMap*
createValidatedOptions(const wchar_t* encID, const prt::AttributeMap* unvalidatedOptions);
PLD_TEST_EXPORTS_API std::string objectToXML(prt::Object const* obj);

void getLibraryPath(std::filesystem::path& path, const void* func);
std::string getSharedLibraryPrefix();
std::string getSharedLibrarySuffix();

PLD_TEST_EXPORTS_API std::string toOSNarrowFromUTF16(const std::wstring& osWString);
std::wstring toUTF16FromOSNarrow(const std::string& osString);
std::wstring toUTF16FromUTF8(const std::string& utf8String);
std::string toUTF8FromOSNarrow(const std::string& osString);
std::string toUTF8FromUTF16(const std::wstring& utf16String);

PLD_TEST_EXPORTS_API std::wstring toFileURI(const std::filesystem::path& p);
std::wstring toFileURI(const std::string& p);
PLD_TEST_EXPORTS_API std::wstring percentEncode(const std::string& utf8String);
PLD_TEST_EXPORTS_API bool isRulePackageUri(const char* uri);
PLD_TEST_EXPORTS_API std::string getBaseUriPath(const char* uri);

std::vector<const wchar_t*> toPtrVec(const std::vector<std::wstring>& wsv);

// hash_combine function from boost library: https://www.boost.org/doc/libs/1_73_0/boost/container_hash/hash.hpp
template <class SizeT>
inline void hash_combine(SizeT& seed, SizeT value) {
	seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

inline void replace_all_not_of(std::wstring& s, const std::wstring& allowedChars) {
	std::wstring::size_type pos = 0;
	while (pos < s.size()) {
		pos = s.find_first_not_of(allowedChars, pos);
		if (pos == std::wstring::npos)
			break;
		s[pos++] = L'_';
	}
}

inline bool startsWithAnyOf(const std::string& s, const std::vector<std::string>& sv) {
	return std::any_of(sv.begin(), sv.end(), [&s](std::string const& v) { return (s.find(v) == 0); });
}

PLD_TEST_EXPORTS_API std::wstring getFileExtensionString(const std::vector<std::wstring>& extensions);
PLD_TEST_EXPORTS_API void ensureNonExistingFile(std::filesystem::path& p);
