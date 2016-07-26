// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <string>
#include "DuplexConnection.h"

namespace reactivesocket {
class Stats {
 public:
  static Stats& noop();

  virtual void socketCreated() = 0;
  virtual void socketClosed() = 0;
  virtual void connectionCreated(
      const char type[4],
      reactivesocket::DuplexConnection* connection) = 0;
  virtual void connectionClosed(
      const char type[4],
      reactivesocket::DuplexConnection* connection) = 0;
  virtual void bytesWritten(size_t bytes) = 0;
  virtual void bytesRead(size_t bytes) = 0;
  virtual void frameWritten() = 0;
  virtual void frameRead() = 0;

  virtual ~Stats() = default;
};
}
