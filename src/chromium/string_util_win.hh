// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUM_STRING_UTIL_WIN_HH__
#define CHROMIUM_STRING_UTIL_WIN_HH__
#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace chromium {

inline int vsnprintf(char* buffer, size_t size,
                     const char* format, va_list arguments) {
  int length = vsnprintf_s(buffer, size, size - 1, format, arguments);
  if (length < 0)
    return _vscprintf(format, arguments);
  return length;
}

}  // namespace chromium

#endif  // CHROMIUM_STRING_UTIL_WIN_HH__
