// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/server/http_connection.hh"

#include "net/server/http_server_response_info.hh"
#include "net/server/web_socket.hh"

namespace net {

int HttpConnection::last_id_ = 0;

HttpConnection::HttpConnection() {
  id_ = last_id_++;
}

HttpConnection::~HttpConnection() {
}

void HttpConnection::Shift(int num_bytes) {
  recv_data_ = recv_data_.substr(num_bytes);
}

}  // namespace net
