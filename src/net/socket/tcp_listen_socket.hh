// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_TCP_LISTEN_SOCKET_HH_
#define NET_SOCKET_TCP_LISTEN_SOCKET_HH_

#include <string>

#include "chromium/basictypes.hh"
#include "net/socket/socket_descriptor.hh"
#include "net/socket/stream_listen_socket.hh"

namespace net {

// Implements a TCP socket.
class TCPListenSocket : public StreamListenSocket {
 public:
  virtual ~TCPListenSocket();
  // Listen on port for the specified IP address.  Use 127.0.0.1 to only
  // accept local connections.
  static std::shared_ptr<TCPListenSocket> CreateAndListen(
      const std::string& ip, int port, StreamListenSocket::Delegate* del);

  // Get raw TCP socket descriptor bound to ip:port.
  static SocketDescriptor CreateAndBind(const std::string& ip, int port);

  // Get raw TCP socket descriptor bound to ip and return port it is bound to.
  static SocketDescriptor CreateAndBindAnyPort(const std::string& ip,
                                               int* port);

 protected:
  explicit TCPListenSocket(SocketDescriptor s, StreamListenSocket::Delegate* del);

  // Implements StreamListenSocket::Accept.
  virtual void Accept() override;
};

// Factory that can be used to instantiate TCPListenSocket.
class TCPListenSocketFactory : public StreamListenSocketFactory {
 public:
  explicit TCPListenSocketFactory(const std::string& ip, int port);
  virtual ~TCPListenSocketFactory();

  // StreamListenSocketFactory overrides.
  virtual std::shared_ptr<StreamListenSocket> CreateAndListen(
      StreamListenSocket::Delegate* delegate) const override;

 private:
  const std::string ip_;
  const int port_;
};

}  // namespace net

#endif  // NET_SOCKET_TCP_LISTEN_SOCKET_HH_
