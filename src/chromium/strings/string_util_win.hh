// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUM_STRINGS_STRING_UTIL_WIN_HH__
#define CHROMIUM_STRINGS_STRING_UTIL_WIN_HH__

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace chromium {

inline int strcasecmp(const char* s1, const char* s2) {
  return _stricmp(s1, s2);
}

inline int strncasecmp(const char* s1, const char* s2, size_t count) {
  return _strnicmp(s1, s2, count);
}

inline int vsnprintf(char* buffer, size_t size,
                     const char* format, va_list arguments) {
  int length = vsnprintf_s(buffer, size, size - 1, format, arguments);
  if (length < 0)
    return _vscprintf(format, arguments);
  return length;
}

}  // namespace chromium

#endif  // CHROMIUM_STRINGS_STRING_UTIL_WIN_HH__
