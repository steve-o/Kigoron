// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_util.hh"

#include <stdio.h>

namespace file_util {

bool ReadFileToString(const std::string& path, std::string* contents) {
  FILE* file = OpenFile(path, "rb");
  if (!file) {
    return false;
  }

  char buf[1 << 16];
  size_t len;
  while ((len = fread(buf, 1, sizeof(buf), file)) > 0) {
    if (contents)
      contents->append(buf, len);
  }
  CloseFile(file);

  return true;
}

bool CloseFile(FILE* file) {
  if (file == NULL)
    return true;
  return fclose(file) == 0;
}

}  // namespace
