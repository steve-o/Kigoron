// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "url_canon_ip.hh"

#include <stdlib.h>

#include "../chromium/basictypes.hh"
#include "../chromium/logging.hh"
#include "../googleurl/url_parse.h"

namespace url {

namespace {

// Searches for the longest sequence of zeros in |address|, and writes the
// range into |contraction_range|. The run of zeros must be at least 16 bits,
// and if there is a tie the first is chosen.
void ChooseIPv6ContractionRange(const unsigned char address[16],
                                url_parse::Component* contraction_range) {
  // The longest run of zeros in |address| seen so far.
  url_parse::Component max_range;

  // The current run of zeros in |address| being iterated over.
  url_parse::Component cur_range;

  for (int i = 0; i < 16; i += 2) {
    // Test for 16 bits worth of zero.
    bool is_zero = (address[i] == 0 && address[i + 1] == 0);

    if (is_zero) {
      // Add the zero to the current range (or start a new one).
      if (!cur_range.is_valid())
        cur_range = url_parse::Component(i, 0);
      cur_range.len += 2;
    }

    if (!is_zero || i == 14) {
      // Just completed a run of zeros. If the run is greater than 16 bits,
      // it is a candidate for the contraction.
      if (cur_range.len > 2 && cur_range.len > max_range.len) {
        max_range = cur_range;
      }
      cur_range.reset();
    }
  }
  *contraction_range = max_range;
}

}  // namespace

void AppendIPv4Address(const unsigned char address[4], CanonOutput* output) {
  for (int i = 0; i < 4; i++) {
    char str[16];
    _itoa_s(address[i], str, 10);

    for (int ch = 0; str[ch] != 0; ch++)
      output->push_back(str[ch]);

    if (i != 3)
      output->push_back('.');
  }
}

void AppendIPv6Address(const unsigned char address[16], CanonOutput* output) {
  // We will output the address according to the rules in:
  // http://tools.ietf.org/html/draft-kawamura-ipv6-text-representation-01#section-4

  // Start by finding where to place the "::" contraction (if any).
  url_parse::Component contraction_range;
  ChooseIPv6ContractionRange(address, &contraction_range);

  for (int i = 0; i <= 14;) {
    // We check 2 bytes at a time, from bytes (0, 1) to (14, 15), inclusive.
    DCHECK(i % 2 == 0);
    if (i == contraction_range.begin && contraction_range.len > 0) {
      // Jump over the contraction.
      if (i == 0)
        output->push_back(':');
      output->push_back(':');
      i = contraction_range.end();
    } else {
      // Consume the next 16 bits from |address|.
      int x = address[i] << 8 | address[i + 1];

      i += 2;

      // Stringify the 16 bit number (at most requires 4 hex digits).
      char str[5];
      _itoa_s(x, str, 16);
      for (int ch = 0; str[ch] != 0; ++ch)
        output->push_back(str[ch]);

      // Put a colon after each number, except the last.
      if (i < 16)
        output->push_back(':');
    }
  }
}

}  // namespace url
