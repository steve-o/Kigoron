// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/tcp_listen_socket.hh"

#if defined(OS_WIN)
// winsock2.h must be included first in order to ensure it is included before
// windows.h.
#include <winsock2.h>
#elif defined(OS_POSIX)
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "net/base/net_errors.h"
#endif

#include "chromium/logging.hh"
#include "net/base/ip_endpoint.hh"
#include "net/base/net_util.hh"
#include "net/socket/socket_descriptor.hh"

using std::string;

namespace net {

// static
std::shared_ptr<TCPListenSocket> TCPListenSocket::CreateAndListen(
    chromium::MessageLoopForIO* message_loop_for_io,
    const string& ip, int port, StreamListenSocket::Delegate* del) {
  SocketDescriptor s = CreateAndBind(ip, port);
  if (s == kInvalidSocket)
    return std::shared_ptr<TCPListenSocket>();
  std::shared_ptr<TCPListenSocket> sock(new TCPListenSocket(message_loop_for_io, s, del));
  sock->Listen();
  return std::move (sock);
}

TCPListenSocket::TCPListenSocket(chromium::MessageLoopForIO* message_loop_for_io,
                                 SocketDescriptor s,
                                 StreamListenSocket::Delegate* del)
    : StreamListenSocket(message_loop_for_io, s, del) {
}

TCPListenSocket::~TCPListenSocket() {}

SocketDescriptor TCPListenSocket::CreateAndBind(const string& address_string, int port) {
  SocketDescriptor s = CreatePlatformSocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  if (s != kInvalidSocket) {
#if defined(OS_POSIX)
    // Allow rapid reuse.
    static const int kOn = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &kOn, sizeof(kOn));
#endif
    IPAddressNumber address_number;
    if (!ParseIPLiteralToNumber(address_string, &address_number)) {
      return kInvalidSocket;
    }
    SockaddrStorage storage;
    if (!IPEndPoint(address_number, port).ToSockAddr(storage.addr, &storage.addr_len)) {
      return kInvalidSocket;
    }
    if (bind(s, storage.addr, storage.addr_len)) {
#if defined(_WIN32)
      closesocket(s);
#elif defined(OS_POSIX)
      close(s);
#endif
      LOG(ERROR) << "Could not bind socket to " << address_string << ":" << port;
      s = kInvalidSocket;
    }
  }
  return s;
}

SocketDescriptor TCPListenSocket::CreateAndBindAnyPort(const string& ip,
                                                       int* port) {
  SocketDescriptor s = CreateAndBind(ip, 0);
  if (s == kInvalidSocket)
    return kInvalidSocket;
  SockaddrStorage storage;
  bool failed = getsockname(s, storage.addr, &storage.addr_len) != 0;
  if (failed) {
    LOG(ERROR) << "Could not determine bound port, getsockname() failed";
#if defined(_WIN32)
    closesocket(s);
#elif defined(OS_POSIX)
    close(s);
#endif
    return kInvalidSocket;
  }
  *port = GetPortFromSockaddr(storage.addr, storage.addr_len);
  return s;
}

void TCPListenSocket::Accept() {
  SocketDescriptor conn = AcceptSocket();
  if (conn == kInvalidSocket)
    return;
  std::shared_ptr<TCPListenSocket> sock(
      new TCPListenSocket(message_loop_for_io_, conn, socket_delegate_));
  // It's up to the delegate to AddRef if it wants to keep it around.
  sock->WatchSocket(WAITING_READ);
  socket_delegate_->DidAccept(this, std::static_pointer_cast<StreamListenSocket>(sock));
}

TCPListenSocketFactory::TCPListenSocketFactory(chromium::MessageLoopForIO* message_loop_for_io, const string& ip, int port)
    : message_loop_for_io_(message_loop_for_io),
      ip_(ip),
      port_(port) {
}

TCPListenSocketFactory::~TCPListenSocketFactory() {}

std::shared_ptr<StreamListenSocket> TCPListenSocketFactory::CreateAndListen(
    StreamListenSocket::Delegate* delegate) const {
  return std::static_pointer_cast<StreamListenSocket>(TCPListenSocket::CreateAndListen(message_loop_for_io_, ip_, port_, delegate));
}

}  // namespace net
