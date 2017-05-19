// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <folly/io/async/AsyncServerSocket.h>
#include "src/ConnectionAcceptor.h"

namespace folly {
class ScopedEventBaseThread;
}

namespace rsocket {

/**
 * TCP implementation of ConnectionAcceptor for use with RSocket::createServer
 *
 * Construction of this does nothing.  The `start` method kicks off work.
 */
class TcpConnectionAcceptor : public ConnectionAcceptor {
 public:
  struct Options {
    explicit Options(uint16_t port_ = 8080, size_t threads_ = 1,
                     int backlog_ = 10) : port(port_), threads(threads_),
                                          backlog(backlog_) {}

    /// Port to listen on for TCP requests.
    uint16_t port;

    /// Number of worker threads processing requests.
    size_t threads;

    /// Number of connections to buffer before accept handlers process them.
    int backlog;
  };

  //////////////////////////////////////////////////////////////////////////////

  explicit TcpConnectionAcceptor(Options);
  ~TcpConnectionAcceptor();

  //////////////////////////////////////////////////////////////////////////////

  // ConnectionAcceptor overrides.

  /**
   * Bind an AsyncServerSocket and start accepting TCP connections.
   */
  folly::Future<folly::Unit> start(
      std::function<
          void(std::unique_ptr<rsocket::DuplexConnection>, folly::EventBase&)>)
      override;

  /**
   * Shutdown the AsyncServerSocket and associated listener thread.
   */
  void stop() override;

 private:
  class SocketCallback;

  /// The thread driving the AsyncServerSocket.
  std::unique_ptr<folly::ScopedEventBaseThread> serverThread_;

  /// The callbacks handling accepted connections.  Each has its own worker
  /// thread.
  std::vector<std::unique_ptr<SocketCallback>> callbacks_;

  std::function<
      void(std::unique_ptr<rsocket::DuplexConnection>, folly::EventBase&)>
      onAccept_;

  /// The socket listening for new connections.
  folly::AsyncServerSocket::UniquePtr serverSocket_;

  /// Options this acceptor has been configured with.
  Options options_;
};
}
