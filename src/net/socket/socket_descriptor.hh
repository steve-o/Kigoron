// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_SOCKET_DESCRIPTOR_HH_
#define NET_SOCKET_SOCKET_DESCRIPTOR_HH_

#if defined(_WIN32)
#include <winsock2.h>
#endif  // _WIN32

namespace net {

#if defined(OS_POSIX)
typedef int SocketDescriptor;
const SocketDescriptor kInvalidSocket = -1;
#elif defined(_WIN32)
typedef SOCKET SocketDescriptor;
const SocketDescriptor kInvalidSocket = INVALID_SOCKET;
#endif

// Interface to create native socket.
// Usually such factories are used for testing purposes, which is not true in
// this case. This interface is used to substitute WSASocket/socket to make
// possible execution of some network code in sandbox.
class PlatformSocketFactory {
 public:
  PlatformSocketFactory();
  virtual ~PlatformSocketFactory();

  // Replace WSASocket/socket with given factory. The factory will be used by
  // CreatePlatformSocket.
  static void SetInstance(PlatformSocketFactory* factory);

  // Creates  socket. See WSASocket/socket documentation of parameters.
  virtual SocketDescriptor CreateSocket(int family, int type, int protocol) = 0;
};

// Creates  socket. See WSASocket/socket documentation of parameters.
SocketDescriptor CreatePlatformSocket(int family,
                                      int type,
                                      int protocol);

}  // namespace net

#endif  // NET_SOCKET_SOCKET_DESCRIPTOR_HH_
