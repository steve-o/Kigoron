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

class FilePath;

#if defined(_WIN32)
typedef HANDLE PlatformFile;
#elif defined(OS_POSIX)
typedef int PlatformFile;

#if defined(OS_POSIX)
typedef struct stat64 stat_wrapper_t;
#endif
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
  // FLAG_(OPEN|CREATE).* are mutually exclusive. You should specify exactly one
  // of the five (possibly combining with other flags) when opening or creating
  // a file.
  // FLAG_(WRITE|APPEND) are mutually exclusive. This is so that APPEND behavior
  // will be consistent with O_APPEND on POSIX.
  // FLAG_EXCLUSIVE_(READ|WRITE) only grant exclusive access to the file on
  // creation on POSIX; for existing files, consider using Lock().
  enum Flags {
    FLAG_OPEN = 1 << 0,             // Opens a file, only if it exists.
    FLAG_CREATE = 1 << 1,           // Creates a new file, only if it does not
                                    // already exist.
    FLAG_OPEN_ALWAYS = 1 << 2,      // May create a new file.
    FLAG_CREATE_ALWAYS = 1 << 3,    // May overwrite an old file.
    FLAG_OPEN_TRUNCATED = 1 << 4,   // Opens a file and truncates it, only if it
                                    // exists.
    FLAG_READ = 1 << 5,
    FLAG_WRITE = 1 << 6,
    FLAG_APPEND = 1 << 7,
    FLAG_EXCLUSIVE_READ = 1 << 8,   // EXCLUSIVE is opposite of Windows SHARE.
    FLAG_EXCLUSIVE_WRITE = 1 << 9,
    FLAG_ASYNC = 1 << 10,
    FLAG_TEMPORARY = 1 << 11,       // Used on Windows only.
    FLAG_HIDDEN = 1 << 12,          // Used on Windows only.
    FLAG_DELETE_ON_CLOSE = 1 << 13,
    FLAG_WRITE_ATTRIBUTES = 1 << 14,  // Used on Windows only.
    FLAG_SHARE_DELETE = 1 << 15,      // Used on Windows only.
    FLAG_TERMINAL_DEVICE = 1 << 16,   // Serial port flags.
    FLAG_BACKUP_SEMANTICS = 1 << 17,  // Used on Windows only.
    FLAG_EXECUTE = 1 << 18,           // Used on Windows only.
  };

  // This enum has been recorded in multiple histograms. If the order of the
  // fields needs to change, please ensure that those histograms are obsolete or
  // have been moved to a different enum.
  //
  // FILE_ERROR_ACCESS_DENIED is returned when a call fails because of a
  // filesystem restriction. FILE_ERROR_SECURITY is returned when a browser
  // policy doesn't allow the operation to be executed.
  enum Error {
    FILE_OK = 0,
    FILE_ERROR_FAILED = -1,
    FILE_ERROR_IN_USE = -2,
    FILE_ERROR_EXISTS = -3,
    FILE_ERROR_NOT_FOUND = -4,
    FILE_ERROR_ACCESS_DENIED = -5,
    FILE_ERROR_TOO_MANY_OPENED = -6,
    FILE_ERROR_NO_MEMORY = -7,
    FILE_ERROR_NO_SPACE = -8,
    FILE_ERROR_NOT_A_DIRECTORY = -9,
    FILE_ERROR_INVALID_OPERATION = -10,
    FILE_ERROR_SECURITY = -11,
    FILE_ERROR_ABORT = -12,
    FILE_ERROR_NOT_A_FILE = -13,
    FILE_ERROR_NOT_EMPTY = -14,
    FILE_ERROR_INVALID_URL = -15,
    FILE_ERROR_IO = -16,
    // Put new entries here and increment FILE_ERROR_MAX.
    FILE_ERROR_MAX = -17
  };

  // This explicit mapping matches both FILE_ on Windows and SEEK_ on Linux.
  enum Whence {
    FROM_BEGIN   = 0,
    FROM_CURRENT = 1,
    FROM_END     = 2
  };

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
