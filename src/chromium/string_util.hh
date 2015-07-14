// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file defines utility functions for working with strings.

#ifndef CHROMIUM_STRING_UTIL_HH__
#define CHROMIUM_STRING_UTIL_HH__

#pragma once

#include <string>

#include "string_piece.hh"  // For implicit conversions.

// Safe standard library wrappers for all platforms.

extern const char kWhitespaceASCII[];

// Removes characters in |trim_chars| from the beginning and end of |input|.
// |trim_chars| must be null-terminated.
// NOTE: Safe to use the same variable for both |input| and |output|.
bool TrimString(const std::string& input,
                            const char trim_chars[],
                            std::string* output);

// Trims any whitespace from either end of the input string.  Returns where
// whitespace was found.
// The non-wide version has two functions:
// * TrimWhitespaceASCII()
//   This function is for ASCII strings and only looks for ASCII whitespace;
// Please choose the best one according to your usage.
// NOTE: Safe to use the same variable for both input and output.
enum TrimPositions {
  TRIM_NONE     = 0,
  TRIM_LEADING  = 1 << 0,
  TRIM_TRAILING = 1 << 1,
  TRIM_ALL      = TRIM_LEADING | TRIM_TRAILING,
};
TrimPositions TrimWhitespaceASCII(const std::string& input,
                                              TrimPositions positions,
                                              std::string* output);

// Deprecated. This function is only for backward compatibility and calls
// TrimWhitespaceASCII().
TrimPositions TrimWhitespace(const std::string& input,
                                         TrimPositions positions,
                                         std::string* output);

#endif  // CHROMIUM_STRING_UTIL_HH__
