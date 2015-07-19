// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUM_STRINGPRINTF_H_
#define CHROMIUM_STRINGPRINTF_H_

#include <stdarg.h>   // va_list

#include <string>

namespace chromium {

// Return a C++ string given printf-like input.
std::string StringPrintf(const char* format, ...);

// Return a C++ string given vprintf-like input.
std::string StringPrintV(const char* format, va_list ap);

// Store result into a supplied string and return it.
const std::string& SStringPrintf(std::string* dst, const char* format, ...);

// Append result to a supplied string.
void StringAppendF(std::string* dst, const char* format, ...);

// Lower-level routine that takes a va_list and appends to a specified
// string.  All other routines are just convenience wrappers around it.
void StringAppendV(std::string* dst, const char* format, va_list ap);

}  // namespace base

#endif  // CHROMIUM_STRINGPRINTF_H_
