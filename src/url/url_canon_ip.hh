// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef URL_URL_CANON_IP_HH_
#define URL_URL_CANON_IP_HH_

#include "url/url_canon.hh"

namespace url {

// Writes the given IPv4 address to |output|.
void AppendIPv4Address(const unsigned char address[4],
                                  CanonOutput* output);

// Writes the given IPv6 address to |output|.
void AppendIPv6Address(const unsigned char address[16],
                                  CanonOutput* output);

}  // namespace url

#endif  // URL_URL_CANON_IP_H_
