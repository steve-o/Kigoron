// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Stream-based listen socket implementation that handles reading and writing
// to the socket, but does not handle creating the socket nor connecting
// sockets, which are handled by subclasses on creation and in Accept,
// respectively.

// StreamListenSocket handles IO asynchronously in the specified MessageLoop.
// This class is NOT thread safe. It uses WSAEVENT handles to monitor activity
// in a given MessageLoop. This means that callbacks will happen in that loop's
// thread always and that all other methods (including constructor and
// destructor) should also be called from the same thread.

#ifndef NET_SOCKET_STREAM_LISTEN_SOCKET_HH_
#define NET_SOCKET_STREAM_LISTEN_SOCKET_HH_

#if defined(_WIN32)
#include <winsock2.h>
#endif
#include <memory>
#include <string>

#include "chromium/basictypes.hh"
#include "net/socket/socket_descriptor.hh"

namespace net {

class IPEndPoint;

class StreamListenSocket {

 public:
  virtual ~StreamListenSocket();

  // TODO(erikkay): this delegate should really be split into two parts
  // to split up the listener from the connected socket.  Perhaps this class
  // should be split up similarly.
  class Delegate {
   public:
    // |server| is the original listening Socket, connection is the new
    // Socket that was created.
    virtual void DidAccept(StreamListenSocket* server,
                           std::shared_ptr<StreamListenSocket> connection) = 0;
    virtual void DidRead(StreamListenSocket* connection,
                         const char* data,
                         int len) = 0;
    virtual void DidClose(StreamListenSocket* sock) = 0;

   protected:
    virtual ~Delegate() {}
  };

  // Send data to the socket.
  void Send(const char* bytes, int len, bool append_linefeed = false);
  void Send(const std::string& str, bool append_linefeed = false);

  // Copies the local address to |address|. Returns a network error code.
  // This method is virtual to support unit testing.
  virtual int GetLocalAddress(IPEndPoint* address);
  // Copies the peer address to |address|. Returns a network error code.
  // This method is virtual to support unit testing.
  virtual int GetPeerAddress(IPEndPoint* address);

  static const int kSocketError;

 protected:
  enum WaitState {
    NOT_WAITING      = 0,
    WAITING_ACCEPT   = 1,
    WAITING_READ     = 2
  };

  explicit StreamListenSocket(SocketDescriptor s, Delegate* del);

  SocketDescriptor AcceptSocket();
  virtual void Accept() = 0;

  void Listen();
  void Read();
  void Close();
  void CloseSocket();

  // Pass any value in case of Windows, because in Windows
  // we are not using state.
  void WatchSocket(WaitState state);
  void UnwatchSocket();

  Delegate* const socket_delegate_;

 private:
  void SendInternal(const char* bytes, int len);

  // Called by MessagePumpLibevent when the socket is ready to do I/O.
  void OnCanReadWithoutBlocking(int fd);
  void OnCanWriteWithoutBlocking(int fd);
  WaitState wait_state_;

  const SocketDescriptor socket_;
};

// Abstract factory that must be subclassed for each subclass of
// StreamListenSocket.
class StreamListenSocketFactory {
 public:
  virtual ~StreamListenSocketFactory() {}

  // Returns a new instance of StreamListenSocket or NULL if an error occurred.
  virtual std::shared_ptr<StreamListenSocket> CreateAndListen(
      StreamListenSocket::Delegate* delegate) const = 0;
};

}  // namespace net

#endif  // NET_SOCKET_STREAM_LISTEN_SOCKET_HH_
