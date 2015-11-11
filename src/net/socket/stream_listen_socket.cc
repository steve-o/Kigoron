
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/stream_listen_socket.hh"

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
#endif

#include <boost/thread/thread.hpp>

#include "chromium/logging.hh"
#include "net/base/ip_endpoint.hh"
#include "net/base/net_errors.hh"
#include "net/base/net_util.hh"
#include "net/socket/socket_descriptor.hh"

using std::string;

#if defined(_WIN32)
typedef int socklen_t;
#endif  // defined(_WIN32)

namespace net {

namespace {

const int kReadBufSize = 4096;

}  // namespace

#if defined(_WIN32)
const int StreamListenSocket::kSocketError = SOCKET_ERROR;
#elif defined(OS_POSIX)
const int StreamListenSocket::kSocketError = -1;
#endif

StreamListenSocket::StreamListenSocket(chromium::MessageLoopForIO* message_loop_for_io,
                                       SocketDescriptor s,
                                       StreamListenSocket::Delegate* del)
    : message_loop_for_io_(message_loop_for_io),
      socket_delegate_(del),
      socket_(s) {
  wait_state_ = NOT_WAITING;
}

StreamListenSocket::~StreamListenSocket() {
  CloseSocket();
}

void StreamListenSocket::Send(const char* bytes, int len,
                              bool append_linefeed) {
  SendInternal(bytes, len);
  if (append_linefeed)
    SendInternal("\r\n", 2);
}

void StreamListenSocket::Send(const string& str, bool append_linefeed) {
  Send(str.data(), static_cast<int>(str.length()), append_linefeed);
}

int StreamListenSocket::GetLocalAddress(IPEndPoint* address) {
  SockaddrStorage storage;
  if (getsockname(socket_, storage.addr, &storage.addr_len)) {
#if defined(_WIN32)
    int err = WSAGetLastError();
#else
    int err = errno;
#endif
    return MapSystemError(err);
  }
  if (!address->FromSockAddr(storage.addr, storage.addr_len))
    return ERR_ADDRESS_INVALID;
  return OK;
}

int StreamListenSocket::GetPeerAddress(IPEndPoint* address) {
  SockaddrStorage storage;
  if (getpeername(socket_, storage.addr, &storage.addr_len)) {
#if defined(_WIN32)
    int err = WSAGetLastError();
#else
    int err = errno;
#endif
    return MapSystemError(err);
  }

  if (!address->FromSockAddr(storage.addr, storage.addr_len))
    return ERR_ADDRESS_INVALID;

  return OK;
}

SocketDescriptor StreamListenSocket::AcceptSocket() {
  SocketDescriptor conn = accept(socket_, NULL, NULL);
  if (conn == kInvalidSocket)
    LOG(ERROR) << "Error accepting connection.";
  else
    SetNonBlocking(conn);
  return conn;
}

void StreamListenSocket::SendInternal(const char* bytes, int len) {
  char* send_buf = const_cast<char *>(bytes);
  int len_left = len;
  while (true) {
    int sent = send(socket_, send_buf, len_left, 0);
    if (sent == len_left) {  // A shortcut to avoid extraneous checks.
      break;
    }
    if (sent == kSocketError) {
#if defined(_WIN32)
      if (WSAGetLastError() != WSAEWOULDBLOCK) {
        LOG(ERROR) << "send failed: WSAGetLastError()==" << WSAGetLastError();
#elif defined(OS_POSIX)
      if (errno != EWOULDBLOCK && errno != EAGAIN) {
        LOG(ERROR) << "send failed: errno==" << errno;
#endif
        break;
      }
      // Otherwise we would block, and now we have to wait for a retry.
      // Fall through to PlatformThread::YieldCurrentThread()
    } else {
      // sent != len_left according to the shortcut above.
      // Shift the buffer start and send the remainder after a short while.
      send_buf += sent;
      len_left -= sent;
    }
    boost::this_thread::yield();
  }
}

void StreamListenSocket::Listen() {
  int backlog = 10;  // TODO(erikkay): maybe don't allow any backlog?
  if (listen(socket_, backlog) == -1) {
    // TODO(erikkay): error handling.
    LOG(ERROR) << "Could not listen on socket.";
    return;
  }
  WatchSocket(WAITING_ACCEPT);
}

void StreamListenSocket::Read() {
  char buf[kReadBufSize + 1];  // +1 for null termination.
  int len;
  do {
    len = recv(socket_, buf, kReadBufSize, 0);
    if (len == kSocketError) {
#if defined(_WIN32)
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK) {
#elif defined(OS_POSIX)
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
#endif
        break;
      } else {
        // TODO(ibrar): some error handling required here.
        break;
      }
    } else if (len == 0) {
      Close();
    } else {
      // TODO(ibrar): maybe change DidRead to take a length instead.
      DCHECK_GT(len, 0);
      DCHECK_LE(len, kReadBufSize);
      buf[len] = 0;  // Already create a buffer with +1 length.
      socket_delegate_->DidRead(this, buf, len);
    }
  } while (len == kReadBufSize);
}

void StreamListenSocket::Close() {
  if (wait_state_ == NOT_WAITING)
    return;
  wait_state_ = NOT_WAITING;
  UnwatchSocket();
  socket_delegate_->DidClose(this);
}

void StreamListenSocket::CloseSocket() {
  if (socket_ != kInvalidSocket) {
    UnwatchSocket();
#if defined(_WIN32)
    closesocket(socket_);
#elif defined(OS_POSIX)
    close(socket_);
#endif
  }
}

void StreamListenSocket::WatchSocket(WaitState state) {
  message_loop_for_io_->WatchFileDescriptor(
      socket_, true, chromium::MessageLoopForIO::WATCH_READ, &watcher_, this);
  wait_state_ = state;
}

void StreamListenSocket::UnwatchSocket() {
  watcher_.StopWatchingFileDescriptor();
}

void StreamListenSocket::OnFileCanReadWithoutBlocking(SocketDescriptor fd) {
  switch (wait_state_) {
    case WAITING_ACCEPT:
      Accept();
      break;
    case WAITING_READ:
      Read();
      break;
    default:
      // Close() is called by Read() in the Linux case.
      NOTREACHED();
      break;
  }
}

void StreamListenSocket::OnFileCanWriteWithoutBlocking(SocketDescriptor fd) {
  // MessagePumpLibevent callback, we don't listen for write events
  // so we shouldn't ever reach here.
  NOTREACHED();
}

}  // namespace net
