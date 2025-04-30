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

#include "Utils.h"
#include "LogHandler.h"

#include "prt/API.h"
#include "prt/StringUtils.h"

#include "prtx/URI.h"

#ifndef _WIN32
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#endif

#ifndef _WIN32
#	pragma GCC diagnostic pop
#endif

#ifdef _WIN32
#	include <Windows.h>
#else
#	include <dlfcn.h>
#endif

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <string_view>

namespace {

constexpr std::string_view ILLEGAL_FS_CHARS = "\\/:*?\"<>|";

template <typename inC, typename outC, typename FUNC>
std::basic_string<outC> callAPI(FUNC f, const std::basic_string<inC>& s) {
	std::vector<outC> buffer(s.size());
	size_t size = buffer.size();
	f(s.c_str(), buffer.data(), &size, nullptr);
	if (size > buffer.size()) {
		buffer.resize(size);
		f(s.c_str(), buffer.data(), &size, nullptr);
	}
	return std::basic_string<outC>{buffer.data()};
}

template <typename C, typename FUNC>
std::basic_string<C> callAPI(FUNC f, size_t initialSize) {
	std::vector<C> buffer(initialSize, ' ');

	size_t actualSize = initialSize;
	f(buffer.data(), &actualSize, nullptr);
	buffer.resize(actualSize);

	if (initialSize < actualSize)
		f(buffer.data(), &actualSize, nullptr);

	return std::basic_string<C>{buffer.data()};
}

const std::wstring FILE_ANY = L"*";
const std::wstring FILE_DOT = L".";
const std::wstring FILE_EXTENSION_PREFIX = FILE_ANY + FILE_DOT;

std::wstring cleanFileExtension(const std::wstring& extension) {
	if (extension.empty())
		return {};
	if (extension.rfind(FILE_EXTENSION_PREFIX, 0) == 0)
		return extension;
	if (extension.rfind(FILE_DOT, 0) == 0)
		return FILE_ANY + extension;
	return FILE_EXTENSION_PREFIX + extension;
}
} // namespace

std::vector<const wchar_t*> toPtrVec(const std::vector<std::wstring>& wsv) {
	std::vector<const wchar_t*> pw(wsv.size());
	std::transform(wsv.begin(), wsv.end(), pw.begin(), [](const auto& s) { return s.c_str(); });
	return pw;
}

std::vector<std::wstring> tokenizeAll(const std::wstring& input, wchar_t token) {
	std::vector<std::wstring> out;

	std::wstring_view delimiterStringView(input);
	size_t startIdx = 0;

	while (startIdx != std::wstring::npos) {
		const size_t endIdx = delimiterStringView.find_first_of(token, startIdx);
		std::wstring_view tokenView;

		if (endIdx == std::wstring::npos) {
			tokenView = delimiterStringView.substr(startIdx);
			startIdx = std::wstring::npos;
		}
		else {
			tokenView = delimiterStringView.substr(startIdx, endIdx - startIdx);
			startIdx = endIdx + 1;
		}

		if (!tokenView.empty())
			out.emplace_back(tokenView);
	}
	return out;
}

std::pair<std::wstring, std::wstring> tokenizeFirst(const std::wstring& input, wchar_t token) {
	const auto p = input.find_first_of(token);
	if (p == std::wstring::npos)
		return {{}, input};
	else if (p > 0 && p < input.length() - 1) {
		return {input.substr(0, p), input.substr(p + 1)};
	}
	else if (p == 0) { // empty style
		return {{}, input.substr(1)};
	}
	else if (p == input.length() - 1) { // empty name
		return {input.substr(0, p), {}};
	}
	return {input, {}};
}

std::vector<std::pair<std::wstring, std::wstring>> getCGBs(const ResolveMapSPtr& rm) {
	constexpr const size_t START_SIZE = 16 * 1024;
	auto searchKeyFunc = [&rm](wchar_t* result, size_t* resultSize, prt::Status* status) {
		constexpr const wchar_t* PROJECT = L"";
		constexpr const wchar_t* PATTERN = L"*.cgb";
		rm->searchKey(PROJECT, PATTERN, result, resultSize, status);
	};
	std::wstring cgbList = callAPI<wchar_t>(searchKeyFunc, START_SIZE);
	LOG_DBG << "   cgbList = '" << cgbList << "'";

	const std::vector<std::wstring>& cgbVec = tokenizeAll(cgbList, L';');
	std::vector<std::pair<std::wstring, std::wstring>> cgbs;

	for (const std::wstring& token : cgbVec) {
		LOG_DBG << "token: '" << token << "'";

		const wchar_t* stringValue = rm->getString(token.c_str());
		if (stringValue != nullptr) {
			cgbs.emplace_back(token, stringValue);
			LOG_DBG << L"got cgb: " << cgbs.back().first << L" => " << cgbs.back().second;
		}
	}
	return cgbs;
}

std::optional<std::pair<std::wstring, std::wstring>> getCGB(const ResolveMapSPtr& rm) {
#if (PRT_VERSION_MAJOR > 2) // CE 2023 introduced multiple CGBs per RPK and PRT 3.0 has tools for this
	prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
	const wchar_t* cgbKey = rm->findCGBKey(&status);
	if (cgbKey == nullptr || (status != prt::STATUS_OK))
		return std::nullopt;

	const wchar_t* cgbUri = rm->getString(cgbKey, &status);
	if (cgbUri == nullptr || (status != prt::STATUS_OK))
		return std::nullopt;

	return std::make_pair(cgbKey, cgbUri);

#else
	std::vector<std::pair<std::wstring, std::wstring>> cgbs = getCGBs(rm); // key -> uri
	if (cgbs.size() != 1)
		return std::nullopt;
	return cgbs.front();

#endif
}

const prt::AttributeMap* createValidatedOptions(const wchar_t* encID, const prt::AttributeMap* unvalidatedOptions) {
	const EncoderInfoUPtr encInfo(prt::createEncoderInfo(encID));
	const prt::AttributeMap* validatedOptions = nullptr;
	const prt::AttributeMap* optionStates = nullptr;
	const prt::Status s =
	        encInfo->createValidatedOptionsAndStates(unvalidatedOptions, &validatedOptions, &optionStates);
	if (optionStates != nullptr)
		optionStates->destroy();
	return (s == prt::STATUS_OK) ? validatedOptions : nullptr;
}

std::string objectToXML(prt::Object const* obj) {
	if (obj == nullptr)
		throw std::invalid_argument("object pointer is not valid");
	auto toXMLFunc = [&obj](char* result, size_t* resultSize, prt::Status* status) {
		obj->toXML(result, resultSize, status);
	};
	return callAPI<char>(toXMLFunc, 4096);
}

void getLibraryPath(std::filesystem::path& path, const void* func) {
#ifdef _WIN32
	HMODULE dllHandle = nullptr;
	if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)func, &dllHandle)) {
		DWORD c = GetLastError();
		char msg[255];
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, c, 0, msg, 255, nullptr);
		throw std::runtime_error("error while trying to get current module handle': " + std::string(msg));
	}
	assert(sizeof(TCHAR) == 1);
	const size_t PATHMAXSIZE = 4096;
	TCHAR pathA[PATHMAXSIZE];
	DWORD pathSize = GetModuleFileName(dllHandle, pathA, PATHMAXSIZE);
	if (pathSize == 0 || pathSize == PATHMAXSIZE) {
		DWORD c = GetLastError();
		char msg[255];
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, c, 0, msg, 255, nullptr);
		throw std::runtime_error("error while trying to get current module path': " + std::string(msg));
	}
	path = pathA;
#else /* macosx or linux */
	Dl_info dl_info;
	if (dladdr(func, &dl_info) == 0) {
		char* error = dlerror();
		throw std::runtime_error("error while trying to get current module path': " + std::string(error ? error : ""));
	}
	path = dl_info.dli_fname;
#endif
}

std::string getSharedLibraryPrefix() {
#if defined(_WIN32)
	return "";
#elif defined(__APPLE__)
	return "lib";
#elif defined(__linux__)
	return "lib";
#else
#	error unsupported build platform
#endif
}

std::string getSharedLibrarySuffix() {
#if defined(_WIN32)
	return ".dll";
#elif defined(__APPLE__)
	return ".dylib";
#elif defined(__linux__)
	return ".so";
#else
#	error unsupported build platform
#endif
}

std::string toOSNarrowFromUTF16(const std::wstring& osWString) {
	return callAPI<wchar_t, char>(prt::StringUtils::toOSNarrowFromUTF16, osWString);
}

std::wstring toUTF16FromOSNarrow(const std::string& osString) {
	return callAPI<char, wchar_t>(prt::StringUtils::toUTF16FromOSNarrow, osString);
}

std::wstring toUTF16FromUTF8(const std::string& utf8String) {
	return callAPI<char, wchar_t>(prt::StringUtils::toUTF16FromUTF8, utf8String);
}

std::string toUTF8FromOSNarrow(const std::string& osString) {
	std::wstring utf16String = toUTF16FromOSNarrow(osString);
	return callAPI<wchar_t, char>(prt::StringUtils::toUTF8FromUTF16, utf16String);
}

std::string toUTF8FromUTF16(const std::wstring& utf16String) {
	return callAPI<wchar_t, char>(prt::StringUtils::toUTF8FromUTF16, utf16String);
}

std::wstring toFileURI(const std::string& p) {
#ifdef _WIN32
	static const std::wstring schema = L"file:/";
#else
	static const std::wstring schema = L"file:";
#endif
	std::string utf8Path = toUTF8FromOSNarrow(p);
	std::wstring pecString = percentEncode(utf8Path);
	return schema + pecString;
}

std::wstring toFileURI(const std::filesystem::path& p) {
	return toFileURI(p.generic_string());
}

std::wstring percentEncode(const std::string& utf8String) {
	return toUTF16FromUTF8(callAPI<char, char>(prt::StringUtils::percentEncode, utf8String));
}

// the general URL form is for example:
// usdz:rpk:file:/foo/bar.rpk!/my/asset.usdz!/some/texture.jpg
bool isRulePackageUri(const char* uri) {
	if (uri == nullptr)
		return false;

	// URL needs to contain the rpk: schema
	if (std::strstr(uri, SCHEMA_RPK) == nullptr)
		return false;

	// needs to contain at least one '!' separator
	if (std::strchr(uri, '!') == nullptr)
		return false;

	return true;
}

// The base URI is the "inner most" URI as defined by prtx::URI, i.e. the actual file
std::string getBaseUriPath(const char* uri) {
	if (uri == nullptr)
		return {};

	// we assume p to be a percent-encoded UTF-8 URI (it comes from a PRT resolve map)
	prtx::URIPtr prtxUri = prtx::URI::create(toUTF16FromUTF8(uri));
	if (!prtxUri)
		return {};

	// let's find the innermost URI (the URI could point to a texture inside USDZ inside RPK)
	while (prtxUri->getNestedURI())
		prtxUri = prtxUri->getNestedURI();

	if (!prtxUri->isFilePath())
		return {};

	return toUTF8FromUTF16(prtxUri->getPath());
}

std::wstring getFileExtensionString(const std::vector<std::wstring>& extensions) {
	std::wstring extensionString;
	for (const std::wstring& extension : extensions) {
		std::wstring cleanedFileExtension = cleanFileExtension(extension);
		if (cleanedFileExtension.empty())
			continue;
		if (extensionString.empty())
			extensionString += cleanedFileExtension;
		else
			extensionString += L" " + cleanedFileExtension;
	}
	if (extensionString.empty())
		return FILE_ANY;
	return extensionString;
}

void ensureNonExistingFile(std::filesystem::path& p) {
	std::string fileName = p.filename().generic_string();

	// filter out common illegal characters
	// note: this needs to be done before we use functions like p.stem()
	//       to correctly handle ':' in Windows paths
	auto filterChar = [](char c) { return (ILLEGAL_FS_CHARS.find(c) != std::string::npos); };
	std::replace_if(fileName.begin(), fileName.end(), filterChar, '_');

	auto cleanFileName =  std::filesystem::path(fileName);
	auto stem = cleanFileName.stem().generic_string();
	p = p.parent_path() / (stem + cleanFileName.extension().generic_string());

	// ensure we do not produce a collision with existing file
	for (size_t suf = 0; std::filesystem::exists(p); suf++) {
		p = p.parent_path() / (stem + '_' + std::to_string(suf) + p.extension().generic_string());
	}
}
