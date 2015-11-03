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
#include "net/base/net_util.hh"
#include "net/socket/socket_descriptor.hh"

using std::string;

namespace net {

// static
std::shared_ptr<TCPListenSocket> TCPListenSocket::CreateAndListen(
    const string& ip, int port, StreamListenSocket::Delegate* del) {
  SocketDescriptor s = CreateAndBind(ip, port);
  if (s == kInvalidSocket)
    return std::shared_ptr<TCPListenSocket>();
  std::shared_ptr<TCPListenSocket> sock(new TCPListenSocket(s, del));
  sock->Listen();
  return std::move (sock);
}

TCPListenSocket::TCPListenSocket(SocketDescriptor s,
                                 StreamListenSocket::Delegate* del)
    : StreamListenSocket(s, del) {
}

TCPListenSocket::~TCPListenSocket() {}

SocketDescriptor TCPListenSocket::CreateAndBind(const string& ip, int port) {
  SocketDescriptor s = CreatePlatformSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s != kInvalidSocket) {
#if defined(OS_POSIX)
    // Allow rapid reuse.
    static const int kOn = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &kOn, sizeof(kOn));
#endif
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    addr.sin_port = htons (port);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))) {
#if defined(_WIN32)
      closesocket(s);
#elif defined(OS_POSIX)
      close(s);
#endif
      LOG(ERROR) << "Could not bind socket to " << ip << ":" << port;
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
  sockaddr_in addr;
  socklen_t addr_size = sizeof(addr);
  bool failed = getsockname(s, reinterpret_cast<struct sockaddr*>(&addr),
                            &addr_size) != 0;
  if (addr_size != sizeof(addr))
    failed = true;
  if (failed) {
    LOG(ERROR) << "Could not determine bound port, getsockname() failed";
#if defined(_WIN32)
    closesocket(s);
#elif defined(OS_POSIX)
    close(s);
#endif
    return kInvalidSocket;
  }
  *port = ntohs (addr.sin_port);
  return s;
}

void TCPListenSocket::Accept() {
  SocketDescriptor conn = AcceptSocket();
  if (conn == kInvalidSocket)
    return;
  std::shared_ptr<TCPListenSocket> sock(
      new TCPListenSocket(conn, socket_delegate_));
  // It's up to the delegate to AddRef if it wants to keep it around.
  sock->WatchSocket(WAITING_READ);
  socket_delegate_->DidAccept(this, std::static_pointer_cast<StreamListenSocket>(sock));
}

TCPListenSocketFactory::TCPListenSocketFactory(const string& ip, int port)
    : ip_(ip),
      port_(port) {
}

TCPListenSocketFactory::~TCPListenSocketFactory() {}

std::shared_ptr<StreamListenSocket> TCPListenSocketFactory::CreateAndListen(
    StreamListenSocket::Delegate* delegate) const {
  return std::static_pointer_cast<StreamListenSocket>(TCPListenSocket::CreateAndListen(ip_, port_, delegate));
}

}  // namespace net
