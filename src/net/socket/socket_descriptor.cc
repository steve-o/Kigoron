// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/socket_descriptor.hh"

#if defined(OS_POSIX)
#include <sys/types.h>
#include <sys/socket.h>
#endif

#include "chromium/basictypes.hh"

#if defined(_WIN32)
#include <ws2tcpip.h>
#endif

namespace net {

PlatformSocketFactory* g_socket_factory = NULL;

PlatformSocketFactory::PlatformSocketFactory() {
}

PlatformSocketFactory::~PlatformSocketFactory() {
}

void PlatformSocketFactory::SetInstance(PlatformSocketFactory* factory) {
  g_socket_factory = factory;
}

SocketDescriptor CreateSocketDefault(int family, int type, int protocol) {
  return ::socket(family, type, protocol);
}

SocketDescriptor CreatePlatformSocket(int family, int type, int protocol) {
  if (g_socket_factory)
    return g_socket_factory->CreateSocket(family, type, protocol);
  else
    return CreateSocketDefault(family, type, protocol);
}

}  // namespace net
