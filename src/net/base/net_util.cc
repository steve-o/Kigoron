// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/net_util.hh"

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

#include "chromium/basictypes.hh"
#include "chromium/logging.hh"
#include "chromium/strings/stringprintf.hh"
#include "url/url_canon.hh"
#include "url/url_canon_stdstring.hh"
#include "url/url_canon_ip.hh"

namespace net {

int SetNonBlocking(SocketDescriptor fd) {
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

bool ParseIPLiteralToNumber(const std::string& ip_literal,
                            IPAddressNumber* ip_number) {
  // |ip_literal| could be either a IPv4 or an IPv6 literal. If it contains
  // a colon however, it must be an IPv6 address.
  if (ip_literal.find(':') != std::string::npos) {
    // GURL expects IPv6 hostnames to be surrounded with brackets.
    std::string host_brackets = "[" + ip_literal + "]";
    url::Component host_comp(0, host_brackets.size());

    // Try parsing the hostname as an IPv6 literal.
    ip_number->resize(16);  // 128 bits.
    return url::IPv6AddressToNumber(host_brackets.data(), host_comp,
                                    &(*ip_number)[0]);
  }

  // Otherwise the string is an IPv4 address.
  ip_number->resize(4);  // 32 bits.
  url::Component host_comp(0, ip_literal.size());
  int num_components;
  url::CanonHostInfo::Family family = url::IPv4AddressToNumber(
      ip_literal.data(), host_comp, &(*ip_number)[0], &num_components);
  return family == url::CanonHostInfo::IPV4;
}

namespace {

const unsigned char kIPv4MappedPrefix[] =
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF };
}

IPAddressNumber ConvertIPv4NumberToIPv6Number(
    const IPAddressNumber& ipv4_number) {
  DCHECK(ipv4_number.size() == 4);

  // IPv4-mapped addresses are formed by:
  // <80 bits of zeros>  + <16 bits of ones> + <32-bit IPv4 address>.
  IPAddressNumber ipv6_number;
  ipv6_number.reserve(16);
  ipv6_number.insert(ipv6_number.end(),
                     kIPv4MappedPrefix,
                     kIPv4MappedPrefix + arraysize(kIPv4MappedPrefix));
  ipv6_number.insert(ipv6_number.end(), ipv4_number.begin(), ipv4_number.end());
  return ipv6_number;
}

bool IsIPv4Mapped(const IPAddressNumber& address) {
  if (address.size() != kIPv6AddressSize)
    return false;
  return std::equal(address.begin(),
                    address.begin() + arraysize(kIPv4MappedPrefix),
                    kIPv4MappedPrefix);
}

IPAddressNumber ConvertIPv4MappedToIPv4(const IPAddressNumber& address) {
  DCHECK(IsIPv4Mapped(address));
  return IPAddressNumber(address.begin() + arraysize(kIPv4MappedPrefix),
                         address.end());
}

const uint16_t* GetPortFieldFromSockaddr(const struct sockaddr* address,
                                       socklen_t address_len) {
  if (address->sa_family == AF_INET) {
    DCHECK_LE(sizeof(sockaddr_in), static_cast<size_t>(address_len));
    const struct sockaddr_in* sockaddr =
        reinterpret_cast<const struct sockaddr_in*>(address);
    return &sockaddr->sin_port;
  } else if (address->sa_family == AF_INET6) {
    DCHECK_LE(sizeof(sockaddr_in6), static_cast<size_t>(address_len));
    const struct sockaddr_in6* sockaddr =
        reinterpret_cast<const struct sockaddr_in6*>(address);
    return &sockaddr->sin6_port;
  } else {
    NOTREACHED();
    return NULL;
  }
}

int GetPortFromSockaddr(const struct sockaddr* address, socklen_t address_len) {
  const uint16_t* port_field = GetPortFieldFromSockaddr(address, address_len);
  if (!port_field)
    return -1;
  return ntohs(*port_field);
}

}  // namespace net
