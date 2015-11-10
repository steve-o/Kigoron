// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef URL_URL_CONSTANTS_HH_
#define URL_URL_CONSTANTS_HH_

namespace url {

extern const char kAboutBlankURL[];

extern const char kAboutScheme[];
extern const char kBlobScheme[];
extern const char kDataScheme[];
extern const char kFileScheme[];
extern const char kFileSystemScheme[];
extern const char kFtpScheme[];
extern const char kGopherScheme[];
extern const char kHttpScheme[];
extern const char kHttpsScheme[];
extern const char kJavaScriptScheme[];
extern const char kMailToScheme[];
extern const char kWsScheme[];
extern const char kWssScheme[];

// Used to separate a standard scheme and the hostname: "://".
extern const char kStandardSchemeSeparator[];

}  // namespace url

#endif  // URL_URL_CONSTANTS_HH_
