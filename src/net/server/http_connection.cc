// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/server/http_connection.hh"

#include "net/server/http_server_response_info.hh"
#include "net/server/web_socket.hh"
#include "net/socket/stream_listen_socket.hh"

#include "kigoron_http_server.hh"

namespace net {

int HttpConnection::last_id_ = 0;

void HttpConnection::Send(const std::string& data) {
  if (!socket_.get())
    return;
  socket_->Send(data);
}

void HttpConnection::Send(const char* bytes, int len) {
  if (!socket_.get())
    return;
  socket_->Send(bytes, len);
}

void HttpConnection::Send(const HttpServerResponseInfo& response) {
  Send(response.Serialize());
}

HttpConnection::HttpConnection (
	kigoron::KigoronHttpServer* server,
	std::shared_ptr<StreamListenSocket> sock
	)
	: server_ (server)
	, socket_ (std::move (sock))
{
  id_ = last_id_++;
}

HttpConnection::~HttpConnection() {
  server_->OnClose(id_);
}

void HttpConnection::Shift(int num_bytes) {
  recv_data_ = recv_data_.substr(num_bytes);
}

}  // namespace net