#pragma once

#include "../Refcounted.h"

namespace yarpl {
namespace observable {

class Subscription : public virtual Refcounted {
 public:
  virtual ~Subscription() = default;
  virtual void cancel() = 0;

 protected:
  Subscription() = default;
};

} // observable
} // yarpl
