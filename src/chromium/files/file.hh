// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUM_FILES_FILE_HH_
#define CHROMIUM_FILES_FILE_HH_

#if defined(_WIN32)
#include <windows.h>
#endif

#if defined(OS_POSIX)
#include <sys/stat.h>
#endif

#include <cstdint>
#include <ctime>
#include <string>

namespace chromium {

#if defined(OS_POSIX)
typedef struct stat64 stat_wrapper_t;
#endif  // defined(OS_POSIX)

// Thin wrapper around an OS-level file.
// Note that this class does not provide any support for asynchronous IO, other
// than the ability to create asynchronous handles on Windows.
//
// Note about const: this class does not attempt to determine if the underlying
// file system object is affected by a particular method in order to consider
// that method const or not. Only methods that deal with member variables in an
// obvious non-modifying way are marked as const. Any method that forward calls
// to the OS is not considered const, even if there is no apparent change to
// member variables.
class File {
 public:
  // Used to hold information about a given file.
  // If you add more fields to this structure (platform-specific fields are OK),
  // make sure to update all functions that use it in file_util_{win|posix}.cc
  // too, and the ParamTraits<base::PlatformFileInfo> implementation in
  // chrome/common/common_param_traits.cc.
  struct Info {
    Info();
    ~Info();
#if defined(OS_POSIX)
    // Fills this struct with values from |stat_info|.
    void FromStat(const stat_wrapper_t& stat_info);
#endif

    // The size of the file in bytes.  Undefined when is_directory is true.
    int64_t size;

    // True if the file corresponds to a directory.
    bool is_directory;

    // True if the file corresponds to a symbolic link.
    bool is_symbolic_link;

    // The last modified time of a file.
    std::time_t last_modified;

    // The last accessed time of a file.
    std::time_t last_accessed;

    // The creation time of a file.
    std::time_t creation_time;
  };
};

}  // namespace chromium

#endif  // CHROMIUM_FILES_FILE_HH_
