// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file defines utility functions for escaping strings.

#ifndef CHROMIUM_JSON_STRING_ESCAPE_HH_
#define CHROMIUM_JSON_STRING_ESCAPE_HH_

#include <string>

namespace chromium {

// Escape |str| appropriately for a JSON string literal, _appending_ the
// result to |dst|. This will create unicode escape sequences (\uXXXX).
// If |put_in_quotes| is true, the result will be surrounded in double quotes.
// The outputted literal, when interpreted by the browser, should result in a
// javascript string that is identical and the same length as the input |str|.
void JsonDoubleQuote(const std::string& str,
                                 bool put_in_quotes,
                                 std::string* dst);

// Same as above, but always returns the result double quoted.
std::string GetDoubleQuotedJson(const std::string& str);

}  // namespace chromium

#endif  // CHROMIUM_JSON_STRING_ESCAPE_HH_
