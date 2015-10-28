// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUM_BASE64_HH__
#define CHROMIUM_BASE64_HH__

#include <string>

#include "chromium/strings/string_piece.hh"

namespace chromium {

// Encodes the input string in base64.
void Base64Encode(const StringPiece& input, std::string* output);

// Decodes the base64 input string.  Returns true if successful and false
// otherwise.  The output string is only modified if successful.
bool Base64Decode(const StringPiece& input, std::string* output);

}  // namespace chromium

#endif  // CHROMIUM_BASE64_HH__
