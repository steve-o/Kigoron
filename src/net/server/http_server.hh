// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SERVER_HTTP_SERVER_HH_
#define NET_SERVER_HTTP_SERVER_HH_

#include <list>
#include <map>

#include "chromium/basictypes.hh"
#include "net/http/http_status_code.hh"
#include "net/socket/stream_listen_socket.hh"

namespace net {

class HttpConnection;
class HttpServerRequestInfo;
class HttpServerResponseInfo;
class IPEndPoint;
class WebSocket;

class HttpServer : public StreamListenSocket::Delegate {
 public:
  class Delegate {
   public:
    virtual void OnHttpRequest(int connection_id,
                               const HttpServerRequestInfo& info) = 0;

    virtual void OnWebSocketRequest(int connection_id,
                                    const HttpServerRequestInfo& info) = 0;

    virtual void OnWebSocketMessage(int connection_id,
                                    const std::string& data) = 0;

    virtual void OnClose(int connection_id) = 0;

   protected:
    virtual ~Delegate() {}
  };

  explicit HttpServer(const StreamListenSocketFactory& socket_factory,
             HttpServer::Delegate* delegate);

  void AcceptWebSocket(int connection_id,
                       const HttpServerRequestInfo& request);
  void SendOverWebSocket(int connection_id, const std::string& data);
  // Sends the provided data directly to the given connection. No validation is
  // performed that data constitutes a valid HTTP response. A valid HTTP
  // response may be split across multiple calls to SendRaw.
  void SendRaw(int connection_id, const std::string& data);
  void SendResponse(int connection_id, const HttpServerResponseInfo& response);
  void Send(int connection_id,
            HttpStatusCode status_code,
            const std::string& data,
            const std::string& mime_type);
  void Send200(int connection_id,
               const std::string& data,
               const std::string& mime_type);
  void Send404(int connection_id);
  void Send500(int connection_id, const std::string& message);

  void Close(int connection_id);

  // Copies the local address to |address|. Returns a network error code.
  int GetLocalAddress(IPEndPoint* address);

  // ListenSocketDelegate
  virtual void DidAccept(StreamListenSocket* server,
                         std::shared_ptr<StreamListenSocket> socket) override;
  virtual void DidRead(StreamListenSocket* socket,
                       const char* data,
                       int len) override;
  virtual void DidClose(StreamListenSocket* socket) override;

 public:
  virtual ~HttpServer();

 private:
  friend class HttpConnection;

  // Expects the raw data to be stored in recv_data_. If parsing is successful,
  // will remove the data parsed from recv_data_, leaving only the unused
  // recv data.
  bool ParseHeaders(HttpConnection* connection,
                    HttpServerRequestInfo* info,
                    size_t* pos);

  HttpConnection* FindConnection(int connection_id);
  HttpConnection* FindConnection(StreamListenSocket* socket);

  HttpServer::Delegate* delegate_;
  std::shared_ptr<StreamListenSocket> server_;
  typedef std::map<int, HttpConnection*> IdToConnectionMap;
  IdToConnectionMap id_to_connection_;
  typedef std::map<StreamListenSocket*, HttpConnection*> SocketToConnectionMap;
  SocketToConnectionMap socket_to_connection_;
};

}  // namespace net

#endif // NET_SERVER_HTTP_SERVER_HH_
