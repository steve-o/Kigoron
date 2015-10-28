// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUM_FLOAT_UTIL_HH_
#define CHROMIUM_FLOAT_UTIL_HH_

#include <float.h>

#include <cmath>

namespace chromium {

template <typename Float>
inline bool IsFinite(const Float& number) {
#if defined(OS_POSIX)
  return std::isfinite(number) != 0;
#elif defined(_WIN32)
  return _finite(number) != 0;
#endif
}

template <typename Float>
inline bool IsNaN(const Float& number) {
#if defined(OS_POSIX)
  return std::isnan(number) != 0;
#elif defined(_WIN32)
  return _isnan(number) != 0;
#endif
}

}  // namespace chromium

#endif  // CHROMIUM_FLOAT_UTIL_HH_
