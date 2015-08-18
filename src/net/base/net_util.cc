// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net_util.hh"

#include <errno.h>
#include <string.h>

#include <algorithm>
#include <iterator>
#include <set>

#if defined(_WIN32)
#include <windows.h>
#include <iphlpapi.h>
#include <winsock2.h>
#include <ws2bth.h>
#pragma comment(lib, "iphlpapi.lib")
#elif defined(OS_POSIX)
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#if !defined(OS_NACL)
#include <net/if.h>
#if !defined(OS_ANDROID)
#include <ifaddrs.h>
#endif  // !defined(OS_NACL)
#endif  // !defined(OS_ANDROID)
#endif  // defined(OS_POSIX)

#include "../../chromium/basictypes.hh"
#include "../../chromium/logging.hh"
#include "../../chromium/stringprintf.hh"
#include "../../url/url_canon.hh"
#include "../../url/url_canon_stdstring.hh"
#include "../../url/url_canon_ip.hh"

namespace net {

int SetNonBlocking(int fd) {
#if defined(_WIN32)
  unsigned long no_block = 1;
  return ioctlsocket(fd, FIONBIO, &no_block);
#elif defined(OS_POSIX)
  int flags = fcntl(fd, F_GETFL, 0);
  if (-1 == flags)
    return flags;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

SockaddrStorage::SockaddrStorage(const SockaddrStorage& other)
    : addr_len(other.addr_len),
      addr(reinterpret_cast<struct sockaddr*>(&addr_storage)) {
  memcpy(addr, other.addr, addr_len);
}

void SockaddrStorage::operator=(const SockaddrStorage& other) {
  addr_len = other.addr_len;
  // addr is already set to &this->addr_storage by default ctor.
  memcpy(addr, other.addr, addr_len);
}

// Extracts the address and port portions of a sockaddr.
bool GetIPAddressFromSockAddr(const struct sockaddr* sock_addr,
                              socklen_t sock_addr_len,
                              const uint8_t** address,
                              size_t* address_len,
                              uint16_t* port) {
  if (sock_addr->sa_family == AF_INET) {
    if (sock_addr_len < static_cast<socklen_t>(sizeof(struct sockaddr_in)))
      return false;
    const struct sockaddr_in* addr =
        reinterpret_cast<const struct sockaddr_in*>(sock_addr);
    *address = reinterpret_cast<const uint8_t*>(&addr->sin_addr);
    *address_len = kIPv4AddressSize;
    if (port)
      *port = ntohs(addr->sin_port);
    return true;
  }

  if (sock_addr->sa_family == AF_INET6) {
    if (sock_addr_len < static_cast<socklen_t>(sizeof(struct sockaddr_in6)))
      return false;
    const struct sockaddr_in6* addr =
        reinterpret_cast<const struct sockaddr_in6*>(sock_addr);
    *address = reinterpret_cast<const uint8_t*>(&addr->sin6_addr);
    *address_len = kIPv6AddressSize;
    if (port)
      *port = ntohs(addr->sin6_port);
    return true;
  }

  return false;  // Unrecognized |sa_family|.
}

std::string IPAddressToString(const uint8_t* address,
                              size_t address_len) {
  std::string str;
  url::StdStringCanonOutput output(&str);

  if (address_len == kIPv4AddressSize) {
    url::AppendIPv4Address(address, &output);
  } else if (address_len == kIPv6AddressSize) {
    url::AppendIPv6Address(address, &output);
  } else {
    CHECK(false) << "Invalid IP address with length: " << address_len;
  }

  output.Complete();
  return str;
}

std::string IPAddressToStringWithPort(const uint8_t* address,
                                      size_t address_len,
                                      uint16_t port) {
  std::string address_str = IPAddressToString(address, address_len);

  if (address_len == kIPv6AddressSize) {
    // Need to bracket IPv6 addresses since they contain colons.
    return chromium::StringPrintf("[%s]:%d", address_str.c_str(), port);
  }
  return chromium::StringPrintf("%s:%d", address_str.c_str(), port);
}

std::string NetAddressToString(const struct sockaddr* sa,
                               socklen_t sock_addr_len) {
  const uint8_t* address;
  size_t address_len;
  if (!GetIPAddressFromSockAddr(sa, sock_addr_len, &address,
                                &address_len, NULL)) {
    NOTREACHED();
    return std::string();
  }
  return IPAddressToString(address, address_len);
}

std::string NetAddressToStringWithPort(const struct sockaddr* sa,
                                       socklen_t sock_addr_len) {
  const uint8_t* address;
  size_t address_len;
  uint16_t port;
  if (!GetIPAddressFromSockAddr(sa, sock_addr_len, &address,
                                &address_len, &port)) {
    NOTREACHED();
    return std::string();
  }
  return IPAddressToStringWithPort(address, address_len, port);
}

std::string IPAddressToString(const IPAddressNumber& addr) {
  return IPAddressToString(&addr.front(), addr.size());
}

std::string IPAddressToStringWithPort(const IPAddressNumber& addr,
                                      uint16_t port) {
  return IPAddressToStringWithPort(&addr.front(), addr.size(), port);
}

std::string IPAddressToPackedString(const IPAddressNumber& addr) {
  return std::string(reinterpret_cast<const char *>(&addr.front()),
                     addr.size());
}

AddressFamily GetAddressFamily(const IPAddressNumber& address) {
  switch (address.size()) {
    case kIPv4AddressSize:
      return ADDRESS_FAMILY_IPV4;
    case kIPv6AddressSize:
      return ADDRESS_FAMILY_IPV6;
    default:
      return ADDRESS_FAMILY_UNSPECIFIED;
  }
}

int ConvertAddressFamily(AddressFamily address_family) {
  switch (address_family) {
    case ADDRESS_FAMILY_UNSPECIFIED:
      return AF_UNSPEC;
    case ADDRESS_FAMILY_IPV4:
      return AF_INET;
    case ADDRESS_FAMILY_IPV6:
      return AF_INET6;
  }
  NOTREACHED();
  return AF_UNSPEC;
}

}  // namespace net
