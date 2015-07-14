// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUM_STRING_SPLIT_HH__
#define CHROMIUM_STRING_SPLIT_HH__

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace chromium {

// Splits |str| into a vector of strings delimited by |s|. Append the results
// into |r| as they appear. If several instances of |s| are contiguous, or if
// |str| begins with or ends with |s|, then an empty string is inserted.
//
// |str| should not be in a multi-byte encoding like Shift-JIS or GBK in which
// the trailing byte of a multi-byte character can be in the ASCII range.
// UTF-8, and other single/multi-byte ASCII-compatible encodings are OK.
// Note: |c| must be in the ASCII range.
void SplitString(const std::string& str,
                             char c,
                             std::vector<std::string>* r);

bool SplitStringIntoKeyValues(
    const std::string& line,
    char key_value_delimiter,
    std::string* key, std::vector<std::string>* values);

bool SplitStringIntoKeyValuePairs(
    const std::string& line,
    char key_value_delimiter,
    char key_value_pair_delimiter,
    std::vector<std::pair<std::string, std::string> >* kv_pairs);

// The same as SplitString, but use a substring delimiter instead of a char.
void SplitStringUsingSubstr(const std::string& str,
                                        const std::string& s,
                                        std::vector<std::string>* r);

// The same as SplitString, but don't trim white space.
// |str| should not be in a multi-byte encoding like Shift-JIS or GBK in which
// the trailing byte of a multi-byte character can be in the ASCII range.
// UTF-8, and other single/multi-byte ASCII-compatible encodings are OK.
// Note: |c| must be in the ASCII range.
void SplitStringDontTrim(const std::string& str,
                                     char c,
                                     std::vector<std::string>* r);

// WARNING: this uses whitespace as defined by the HTML5 spec. If you need
// a function similar to this but want to trim all types of whitespace, then
// factor this out into a function that takes a string containing the characters
// that are treated as whitespace.
//
// Splits the string along whitespace (where whitespace is the five space
// characters defined by HTML 5). Each contiguous block of non-whitespace
// characters is added to result.
void SplitStringAlongWhitespace(const std::string& str,
                                            std::vector<std::string>* result);

}  // namespace base

#endif  // CHROMIUM_STRING_SPLIT_HH__
