// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stack_trace.hh"

#include <string.h>

#include <algorithm>
#include <sstream>

namespace chromium {
namespace debug {

StackTrace::StackTrace(const void* const* trace, size_t count)
{
  count = std::min(count, _countof (trace_));
  if (count)
    memcpy(trace_, trace, count * sizeof(trace_[0]));
  count_ = static_cast<int>(count);
}

StackTrace::~StackTrace() {
}

const void *const *
StackTrace::Addresses(size_t* count) const
{
  *count = count_;
  if (count_)
    return trace_;
  return NULL;
}

std::string
StackTrace::ToString() const
{
  std::stringstream stream;
  OutputToStream(&stream);
  return stream.str();
}

}  // namespace debug
}  // namespace chromium

/* eof */
