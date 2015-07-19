// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUM_PORT_HH_
#define CHROMIUM_PORT_HH_
#pragma once

#include <stdarg.h>

// It's possible for functions that use a va_list, such as StringPrintf, to
// invalidate the data in it upon use.  The fix is to make a copy of the
// structure before using it and use that copy instead.  va_copy is provided
// for this purpose.  MSVC does not provide va_copy, so define an
// implementation here.  It is not guaranteed that assignment is a copy, so the
// StringUtil.VariableArgsFunc unit test tests this capability.
#ifdef _MSC_VER
#define GG_VA_COPY(a, b) (a = b)
#else
#define GG_VA_COPY(a, b) (va_copy(a, b))
#endif

#endif  // CHROMIUM_PORT_HH_
