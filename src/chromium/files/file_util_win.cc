// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromium/files/file_util.hh"

#include <share.h>
#include <windows.h>
#include <time.h>

namespace chromium {

bool PathExists(const std::string& path) {
  return (GetFileAttributes(path.c_str()) != INVALID_FILE_ATTRIBUTES);
}

namespace Time {

time_t FromFileTime(const FILETIME& ft) {
   ULARGE_INTEGER ull;
   ull.LowPart = ft.dwLowDateTime;
   ull.HighPart = ft.dwHighDateTime;

   return ull.QuadPart / 10000000ULL - 11644473600ULL;
}

}  // namespace Time

bool GetFileInfo(const std::string& file_path, File::Info* results) {
  WIN32_FILE_ATTRIBUTE_DATA attr;
  if (!GetFileAttributesEx(file_path.c_str(),
                           GetFileExInfoStandard, &attr)) {
    return false;
  }

  ULARGE_INTEGER size;
  size.HighPart = attr.nFileSizeHigh;
  size.LowPart = attr.nFileSizeLow;
  results->size = size.QuadPart;

  results->is_directory =
      (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  results->last_modified = Time::FromFileTime(attr.ftLastWriteTime);
  results->last_accessed = Time::FromFileTime(attr.ftLastAccessTime);
  results->creation_time = Time::FromFileTime(attr.ftCreationTime);

  return true;
}

}  // namespace chromium

// -----------------------------------------------------------------------------

namespace file_util {

FILE* OpenFile(const std::string& filename, const char* mode) {
  return _fsopen(filename.c_str(), mode, _SH_DENYNO);
}

}  // namespace file_util
