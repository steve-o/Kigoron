// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SERVER_HTTP_CONNECTION_HH_
#define NET_SERVER_HTTP_CONNECTION_HH_

#include <memory>
#include <string>

#include "chromium/basictypes.hh"
#include "net/http/http_status_code.hh"

namespace kigoron {

class KigoronHttpServer;
class http_connection_t;

}

namespace net {

class HttpServerResponseInfo;
class WebSocket;

class HttpConnection {
 public:
  explicit HttpConnection();
  ~HttpConnection();

  void Shift(int num_bytes);

  const std::string& recv_data() const { return recv_data_; }
  int id() const { return id_; }

 private:
  friend class kigoron::KigoronHttpServer;
  friend class kigoron::http_connection_t;
  static int last_id_;

  std::shared_ptr<WebSocket> web_socket_;
  std::string recv_data_;
  int id_;
};

}  // namespace net

#endif  // NET_SERVER_HTTP_CONNECTION_HH_
