#pragma once

#include <cassert>
#include <utility>
#include "../Flowable.h"
#include "Subscriber.h"
#include "Subscription.h"

namespace yarpl {
namespace flowable {
/**
 * Base (helper) class for operators.  Operators are templated on two types:
 * D (downstream) and U (upstream).  Operators are created by method calls on
 * an upstream Flowable, and are Flowables themselves.  Multi-stage pipelines
 * can be built: a Flowable heading a sequence of Operators.
 */
template <typename U, typename D>
class FlowableOperator : public Flowable<D> {
 public:
  explicit FlowableOperator(Reference<Flowable<U>> upstream)
      : upstream_(std::move(upstream)) {}

 protected:
  ///
  /// \brief An Operator's subscription.
  ///
  /// When a pipeline chain is active, each Flowable has a corresponding
  /// subscription.  Except for the first one, the subscriptions are created
  /// against Operators.  Each operator subscription has two functions: as a
  /// subscriber for the previous stage; as a subscription for the next one,
  /// the user-supplied subscriber being the last of the pipeline stages.
  class Subscription : public ::yarpl::flowable::Subscription,
                       public Subscriber<U> {
   protected:
    Subscription(
        Reference<Flowable<D>> flowable,
        Reference<Subscriber<D>> subscriber)
        : flowable_(std::move(flowable)), subscriber_(std::move(subscriber)) {
      assert(flowable_);
      assert(subscriber_);

      // We expect to be heap-allocated; until this subscription finishes
      // (is canceled; completes; error's out), hold a reference so we are
      // not deallocated (by the subscriber).
      Refcounted::incRef(*this);
    }

    template<typename TOperator>
    TOperator* getFlowableAs() {
      return static_cast<TOperator*>(flowable_.get());
    }

    void subscriberOnNext(D value) {
      subscriber_->onNext(std::move(value));
    }

    void request(int64_t delta) override {
      upstream_->request(delta);
    }

    // this method should be used to terminate the operators
    void terminate() {
      subscriber_->onComplete(); // should break the cycle to this
      upstream_->cancel(); // should break the cycle to this
      Refcounted::decRef(*this);
    }

    void cancel() override {
      upstream_->cancel();
      subscriber_.reset(); // breaking the cycle
      Refcounted::decRef(*this);
    }

   private:
    void onSubscribe(
        Reference<::yarpl::flowable::Subscription> subscription) override {
      upstream_ = std::move(subscription);
      subscriber_->onSubscribe(
          Reference<::yarpl::flowable::Subscription>(this));
    }

    void onComplete() override {
      subscriber_->onComplete();
      upstream_.reset(); // breaking the cycle
      Refcounted::decRef(*this);
    }

    void onError(const std::exception_ptr error) override {
      subscriber_->onError(error);
      upstream_.reset(); // breaking the cycle
      Refcounted::decRef(*this);
    }

    /// The Flowable has the lambda, and other creation parameters.
    Reference<Flowable<D>> flowable_;

    /// This subscription controls the life-cycle of the subscriber.  The
    /// subscriber is retained as long as calls on it can be made.  (Note:
    /// the subscriber in turn maintains a reference on this subscription
    /// object until cancellation and/or completion.)
    Reference<Subscriber<D>> subscriber_;

    /// In an active pipeline, cancel and (possibly modified) request(n)
    /// calls should be forwarded upstream.  Note that `this` is also a
    /// subscriber for the upstream stage: thus, there are cycles; all of
    /// the objects drop their references at cancel/complete.
    Reference<::yarpl::flowable::Subscription> upstream_;
  };

  Reference<Flowable<U>> upstream_;
};

template <
    typename U,
    typename D,
    typename F,
    typename = typename std::enable_if<std::is_callable<F(U), D>::value>::type>
class MapOperator : public FlowableOperator<U, D> {
 public:
  MapOperator(Reference<Flowable<U>> upstream, F&& function)
      : FlowableOperator<U, D>(std::move(upstream)),
        function_(std::forward<F>(function)) {}

  void subscribe(Reference<Subscriber<D>> subscriber) override {
    FlowableOperator<U, D>::upstream_->subscribe(
        // Note: implicit cast to a reference to a subscriber.
        Reference<Subscription>(new Subscription(
            Reference<Flowable<D>>(this), std::move(subscriber))));
  }

 private:
  class Subscription : public FlowableOperator<U, D>::Subscription {
    using Super = typename FlowableOperator<U,D>::Subscription;
   public:
    Subscription(
        Reference<Flowable<D>> flowable,
        Reference<Subscriber<D>> subscriber)
        : FlowableOperator<U, D>::Subscription(
              std::move(flowable),
              std::move(subscriber)) {}

    void onNext(U value) override {
      auto* map = Super::template getFlowableAs<MapOperator>();
      Super::subscriberOnNext(map->function_(std::move(value)));
    }
  };

  F function_;
};

template <
    typename U,
    typename F,
    typename =
        typename std::enable_if<std::is_callable<F(U), bool>::value>::type>
class FilterOperator : public FlowableOperator<U, U> {
 public:
  FilterOperator(Reference<Flowable<U>> upstream, F&& function)
      : FlowableOperator<U, U>(std::move(upstream)),
        function_(std::forward<F>(function)) {}

  void subscribe(Reference<Subscriber<U>> subscriber) override {
    FlowableOperator<U, U>::upstream_->subscribe(
        // Note: implicit cast to a reference to a subscriber.
        Reference<Subscription>(new Subscription(
            Reference<Flowable<U>>(this), std::move(subscriber))));
  }

 private:
  class Subscription : public FlowableOperator<U, U>::Subscription {
    using Super = typename FlowableOperator<U,U>::Subscription;
   public:
    Subscription(
        Reference<Flowable<U>> flowable,
        Reference<Subscriber<U>> subscriber)
        : FlowableOperator<U, U>::Subscription(
              std::move(flowable),
              std::move(subscriber)) {}

    void onNext(U value) override {
      auto* filter = Super::template getFlowableAs<FilterOperator>();
      if (filter->function_(value)) {
        Super::subscriberOnNext(std::move(value));
      } else {
        Super::request(1l);
      }
    }

   private:
  };

  F function_;
};

template <typename T>
class TakeOperator : public FlowableOperator<T, T> {
 public:
  TakeOperator(Reference<Flowable<T>> upstream, int64_t limit)
      : FlowableOperator<T, T>(std::move(upstream)), limit_(limit) {}

  void subscribe(Reference<Subscriber<T>> subscriber) override {
    FlowableOperator<T, T>::upstream_->subscribe(
        Reference<Subscription>(new Subscription(
            Reference<Flowable<T>>(this), limit_, std::move(subscriber))));
  }

 private:
  class Subscription : public FlowableOperator<T, T>::Subscription {
    using Super = typename FlowableOperator<T,T>::Subscription;
   public:
    Subscription(
        Reference<Flowable<T>> flowable,
        int64_t limit,
        Reference<Subscriber<T>> subscriber)
        : FlowableOperator<T, T>::Subscription(
              std::move(flowable),
              std::move(subscriber)),
          limit_(limit) {}

    void onNext(T value) override {
      if (limit_-- > 0) {
        if (pending_ > 0)
          --pending_;
        Super::subscriberOnNext(
            std::move(value));
        if (limit_ == 0) {
          Super::terminate();
        }
      }
    }

    void request(int64_t delta) override {
      delta = std::min(delta, limit_ - pending_);
      if (delta > 0) {
        pending_ += delta;
        Super::request(delta);
      }
    }

   private:
    int64_t pending_{0};
    int64_t limit_;
  };

  const int64_t limit_;
};

template <typename T>
class SubscribeOnOperator : public FlowableOperator<T, T> {
 public:
  SubscribeOnOperator(Reference<Flowable<T>> upstream, Scheduler& scheduler)
      : FlowableOperator<T, T>(std::move(upstream)),
        worker_(scheduler.createWorker()) {}

  void subscribe(Reference<Subscriber<T>> subscriber) override {
    FlowableOperator<T, T>::upstream_->subscribe(
        Reference<Subscription>(new Subscription(
            Reference<Flowable<T>>(this),
            std::move(worker_),
            std::move(subscriber))));
  }

 private:
  class Subscription : public FlowableOperator<T, T>::Subscription {
    using Super = typename FlowableOperator<T,T>::Subscription;
   public:
    Subscription(
        Reference<Flowable<T>> flowable,
        std::unique_ptr<Worker> worker,
        Reference<Subscriber<T>> subscriber)
        : FlowableOperator<T, T>::Subscription(
              std::move(flowable),
              std::move(subscriber)),
          worker_(std::move(worker)) {}

    void request(int64_t delta) override {
      worker_->schedule([delta, this] { this->callSuperRequest(delta); });
    }

    void cancel() override {
      worker_->schedule([this] { this->callSuperCancel(); });
    }

    void onNext(T value) override {
      Super::subscriberOnNext(std::move(value));
    }

   private:
    // Trampoline to call superclass method; gcc bug 58972.
    void callSuperRequest(int64_t delta) {
      Super::request(delta);
    }

    // Trampoline to call superclass method; gcc bug 58972.
    void callSuperCancel() {
      Super::cancel();
    }

    std::unique_ptr<Worker> worker_;
  };

  std::unique_ptr<Worker> worker_;
};

template <typename T, typename OnSubscribe>
class FromPublisherOperator : public Flowable<T> {
 public:
  explicit FromPublisherOperator(OnSubscribe&& function)
      : function_(std::move(function)) {}

  void subscribe(Reference<Subscriber<T>> subscriber) override {
    function_(std::move(subscriber));
  }

 private:
  OnSubscribe function_;
};

} // flowable
} // yarpl
