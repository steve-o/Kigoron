// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SERVER_HTTP_CONNECTION_HH_
#define NET_SERVER_HTTP_CONNECTION_HH_

#include <memory>
#include <string>

#include "chromium/basictypes.hh"
#include "net/http/http_status_code.hh"

namespace net {

class HttpServer;
class HttpServerResponseInfo;
class StreamListenSocket;
class WebSocket;

class HttpConnection {
 public:
  ~HttpConnection();

  void Send(const std::string& data);
  void Send(const char* bytes, int len);
  void Send(const HttpServerResponseInfo& response);

  void Shift(int num_bytes);

  const std::string& recv_data() const { return recv_data_; }
  int id() const { return id_; }

 private:
  friend class HttpServer;
  static int last_id_;

  explicit HttpConnection (HttpServer* server, std::shared_ptr<StreamListenSocket> sock);

  HttpServer* server_;
  std::shared_ptr<StreamListenSocket> socket_;
  std::shared_ptr<WebSocket> web_socket_;
  std::string recv_data_;
  int id_;
};

}  // namespace net

#endif  // NET_SERVER_HTTP_CONNECTION_HH_
