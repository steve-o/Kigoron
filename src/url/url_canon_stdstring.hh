// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef URL_URL_CANON_STDSTRING_HH_
#define URL_URL_CANON_STDSTRING_HH_

// This header file defines a canonicalizer output method class for STL
// strings. Because the canonicalizer tries not to be dependent on the STL,
// we have segregated it here.

#include <string>

#include "url/url_canon.hh"

namespace url {

// Write into a std::string given in the constructor. This object does not own
// the string itself, and the user must ensure that the string stays alive
// throughout the lifetime of this object.
//
// The given string will be appended to; any existing data in the string will
// be preserved. The caller should reserve() the amount of data in the string
// they expect to be written. We will resize if necessary, but that's slow.
//
// Note that when canonicalization is complete, the string will likely have
// unused space at the end because we make the string very big to start out
// with (by |initial_size|). This ends up being important because resize
// operations are slow, and because the base class needs to write directly
// into the buffer.
//
// Therefore, the user should call Complete() before using the string that
// this class wrote into.
class StdStringCanonOutput : public CanonOutput {
 public:
  StdStringCanonOutput(std::string* str);
  virtual ~StdStringCanonOutput();

  // Must be called after writing has completed but before the string is used.
  void Complete();

  virtual void Resize(int sz) override;

 protected:
  std::string* str_;
};

}  // namespace url

#endif  // URL_URL_CANON_STDSTRING_HH_
