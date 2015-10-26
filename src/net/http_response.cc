// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http_response.hh"

#include <cinttypes>

#include "chromium/format_macros.hh"
#include "chromium/logging.hh"
#include "chromium/strings/stringprintf.hh"
#include "net/http/http_status_code.hh"

namespace net {

HttpResponse::~HttpResponse() {
}

BasicHttpResponse::BasicHttpResponse() : code_(HTTP_OK) {
}

BasicHttpResponse::~BasicHttpResponse() {
}

std::string BasicHttpResponse::ToResponseString() const {
  // Response line with headers.
  std::string response_builder;

  std::string http_reason_phrase(GetHttpReasonPhrase(code_));

  // TODO(mtomasz): For http/1.0 requests, send http/1.0.
  chromium::StringAppendF(&response_builder,
                      "HTTP/1.1 %d %s\r\n",
                      code_,
                      http_reason_phrase.c_str());
  chromium::StringAppendF(&response_builder, "Connection: close\r\n");
  chromium::StringAppendF(&response_builder,
                      "Content-Length: %" PRIuS "\r\n",
                      content_.size());
  chromium::StringAppendF(&response_builder,
                      "Content-Type: %s\r\n",
                      content_type_.c_str());
  for (size_t i = 0; i < custom_headers_.size(); ++i) {
    const std::string& header_name = custom_headers_[i].first;
    const std::string& header_value = custom_headers_[i].second;
    DCHECK(header_value.find_first_of("\n\r") == std::string::npos) <<
        "Malformed header value.";
    chromium::StringAppendF(&response_builder,
                        "%s: %s\r\n",
                        header_name.c_str(),
                        header_value.c_str());
  }
  chromium::StringAppendF(&response_builder, "\r\n");

  return response_builder + content_;
}

}  // namespace net
