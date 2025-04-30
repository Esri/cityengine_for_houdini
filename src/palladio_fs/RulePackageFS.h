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

#include "FS/FS_Info.h"
#include "FS/FS_Reader.h"

#include "prt/Cache.h"

#include <ctime>

/**
 * support for nested file RPK URIs:
 * rpk:file:/path/to/file.rpk!/nested/file.cgb
 */

class RulePackageReader : public FS_ReaderHelper {
public:
	RulePackageReader(prt::Cache* cache);
	~RulePackageReader() override = default;

	FS_ReaderStream* createStream(const char* source, const UT_Options* options) override;

private:
	prt::Cache* mCache;
};

#if (HOUDINI_VERSION_MAJOR >= 20 || (HOUDINI_VERSION_MAJOR == 19 && HOUDINI_VERSION_MINOR == 5))
using pld_time_t = std::time_t;
#else
using pld_time_t = int;
#endif

class RulePackageInfoHelper : public FS_InfoHelper {
public:
	RulePackageInfoHelper(prt::Cache* cache);
	~RulePackageInfoHelper() override = default;

	bool canHandle(const char* source) override;
	bool hasAccess(const char* source, int mode) override;
	bool getIsDirectory(const char* source) override;
	pld_time_t getModTime(const char* source) override;
	int64 getSize(const char* source) override;
	UT_String getExtension(const char* source) override;
	bool getContents(const char* source, UT_StringArray& contents, UT_StringArray* dirs) override;

private:
	prt::Cache* mCache;
};
