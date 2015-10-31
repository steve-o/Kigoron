// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NET_ERRORS_HH__
#define NET_BASE_NET_ERRORS_HH__

#include <string>
#include <vector>

#include "chromium/basictypes.hh"
#include "chromium/files/file.hh"

namespace net {

// Error domain of the net module's error codes.
extern const char kErrorDomain[];

// Error values are negative.
enum Error {
  // No error.
  OK = 0,

#define NET_ERROR(label, value) ERR_ ## label = value,
#include "net/base/net_error_list.hh"
#undef NET_ERROR

  // The value of the first certificate error code.
  ERR_CERT_BEGIN = ERR_CERT_COMMON_NAME_INVALID,
};

// Returns a textual representation of the error code for logging purposes.
std::string ErrorToString(int error);

// Same as above, but leaves off the leading "net::".
std::string ErrorToShortString(int error);

// Returns true if |error| is a certificate error code.
bool IsCertificateError(int error);

// Map system error code to Error.
Error MapSystemError(int os_error);

// A convenient function to translate file error to net error code.
Error FileErrorToNetError(chromium::File::Error file_error);

}  // namespace net

#endif  // NET_BASE_NET_ERRORS_HH__
