// Copyright 2004-present Facebook. All Rights Reserved.

#include <folly/Baton.h>
#include <gtest/gtest.h>
#include <atomic>

#include "yarpl/Observable.h"
#include "yarpl/flowable/Subscriber.h"
#include "yarpl/flowable/Subscribers.h"
#include "yarpl/schedulers/ThreadScheduler.h"

#include "Tuple.h"

// TODO can we eliminate need to import both of these?
using namespace yarpl;
using namespace yarpl::observable;

namespace {

void unreachable() {
  EXPECT_TRUE(false);
}

template <typename T>
class CollectingObserver : public Observer<T> {
 public:
  static_assert(
      std::is_copy_constructible<T>::value,
      "CollectingSubscriber needs to copy the value in order to collect it");

  void onNext(T next) override {
    values_.push_back(std::move(next));
  }

  void onComplete() override {
    Observer<T>::onComplete();
    complete_ = true;
  }

  void onError(std::exception_ptr ex) override {
    Observer<T>::onError(ex);
    error_ = true;

    try {
      std::rethrow_exception(ex);
    } catch (const std::exception& e) {
      errorMsg_ = e.what();
    }
  }

  const std::vector<T>& values() const {
    return values_;
  }

  bool complete() const {
    return complete_;
  }

  bool error() const {
    return error_;
  }

  const std::string& errorMsg() const {
    return errorMsg_;
  }

 private:
  std::vector<T> values_;
  std::string errorMsg_;
  bool complete_{false};
  bool error_{false};
};

/// Construct a pipeline with a collecting observer against the supplied
/// observable.  Return the items that were sent to the observer.  If some
/// exception was sent, the exception is thrown.
template <typename T>
std::vector<T> run(Reference<Observable<T>> observable) {
  auto collector = make_ref<CollectingObserver<T>>();
  observable->subscribe(collector);
  return collector->values();
}

} // namespace

TEST(Observable, SingleOnNext) {
  {
    ASSERT_EQ(std::size_t{0}, Refcounted::objects());
    auto a = Observable<int>::create([](Reference<Observer<int>> obs) {
      auto s = Subscriptions::empty();
      obs->onSubscribe(s);
      obs->onNext(1);
      obs->onComplete();
    });

    ASSERT_EQ(std::size_t{1}, Refcounted::objects());

    std::vector<int> v;
    a->subscribe(
        Observers::create<int>([&v](const int& value) { v.push_back(value); }));

    ASSERT_EQ(std::size_t{1}, Refcounted::objects());
    EXPECT_EQ(v.at(0), 1);
  }
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
}

TEST(Observable, MultiOnNext) {
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
  {
    auto a = Observable<int>::create([](Reference<Observer<int>> obs) {
      obs->onSubscribe(Subscriptions::empty());
      obs->onNext(1);
      obs->onNext(2);
      obs->onNext(3);
      obs->onComplete();
    });

    std::vector<int> v;
    a->subscribe(
        Observers::create<int>([&v](const int& value) { v.push_back(value); }));

    EXPECT_EQ(v.at(0), 1);
    EXPECT_EQ(v.at(1), 2);
    EXPECT_EQ(v.at(2), 3);
  }
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
}

TEST(Observable, OnError) {
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
  {
    std::string errorMessage("DEFAULT->No Error Message");
    auto a = Observable<int>::create([](Reference<Observer<int>> obs) {
      try {
        throw std::runtime_error("something broke!");
      } catch (const std::exception&) {
        obs->onError(std::current_exception());
      }
    });

    a->subscribe(Observers::create<int>(
        [](int value) { /* do nothing */ },
        [&errorMessage](const std::exception_ptr e) {
          try {
            std::rethrow_exception(e);
          } catch (const std::runtime_error& ex) {
            errorMessage = std::string(ex.what());
          }
        }));

    EXPECT_EQ("something broke!", errorMessage);
  }
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
}

static std::atomic<int> instanceCount;

/**
 * Assert that all items passed through the Observable get destroyed
 */
TEST(Observable, ItemsCollectedSynchronously) {
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
  {
    auto a = Observable<Tuple>::create([](Reference<Observer<Tuple>> obs) {
      obs->onSubscribe(Subscriptions::empty());
      obs->onNext(Tuple{1, 2});
      obs->onNext(Tuple{2, 3});
      obs->onNext(Tuple{3, 4});
      obs->onComplete();
    });

    a->subscribe(Observers::create<Tuple>([](const Tuple& value) {
      std::cout << "received value " << value.a << std::endl;
    }));

    std::cout << "Finished ... remaining instances == " << instanceCount
              << std::endl;

    EXPECT_EQ(0, Tuple::instanceCount);
  }
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
}

/*
 * Assert that all items passed through the Observable get
 * copied and destroyed correctly over async boundaries.
 *
 * This is simulating "async" by having an Observer store the items
 * in a Vector which could then be consumed on another thread.
 */
TEST(DISABLED_Observable, ItemsCollectedAsynchronously) {
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
  // scope this so we can check destruction of Vector after this block
  {
    auto a = Observable<Tuple>::create([](Reference<Observer<Tuple>> obs) {
      obs->onSubscribe(Subscriptions::empty());
      std::cout << "-----------------------------" << std::endl;
      obs->onNext(Tuple{1, 2});
      std::cout << "-----------------------------" << std::endl;
      obs->onNext(Tuple{2, 3});
      std::cout << "-----------------------------" << std::endl;
      obs->onNext(Tuple{3, 4});
      std::cout << "-----------------------------" << std::endl;
      obs->onComplete();
    });

    std::vector<Tuple> v;
    v.reserve(10); // otherwise it resizes and copies on each push_back
    a->subscribe(Observers::create<Tuple>([&v](const Tuple& value) {
      std::cout << "received value " << value.a << std::endl;
      // copy into vector
      v.push_back(value);
      std::cout << "done pushing into vector" << std::endl;
    }));

    // expect that 3 instances were originally created, then 3 more when copying
    EXPECT_EQ(6, Tuple::createdCount);
    // expect that 3 instances still exist in the vector, so only 3 destroyed so
    // far
    EXPECT_EQ(3, Tuple::destroyedCount);

    std::cout << "Leaving block now so Vector should release Tuples..."
              << std::endl;
  }
  EXPECT_EQ(0, Tuple::instanceCount);
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
}

class TakeObserver : public Observer<int> {
 private:
  const int limit;
  int count = 0;
  Reference<yarpl::observable::Subscription> subscription_;
  std::vector<int>& v;

 public:
  TakeObserver(int _limit, std::vector<int>& _v) : limit(_limit), v(_v) {
    v.reserve(5);
  }

  void onSubscribe(Reference<yarpl::observable::Subscription> s) override {
    subscription_ = std::move(s);
  }

  void onNext(int value) override {
    v.push_back(value);
    if (++count >= limit) {
      //      std::cout << "Cancelling subscription after receiving " << count
      //                << " items." << std::endl;
      subscription_->cancel();
    }
  }

  void onError(const std::exception_ptr e) override {}
  void onComplete() override {}
};

// assert behavior of onComplete after subscription.cancel
TEST(Observable, SubscriptionCancellation) {
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
  {
    static std::atomic_int emitted{0};
    auto a = Observable<int>::create([](Reference<Observer<int>> obs) {
      std::atomic_bool isUnsubscribed{false};
      auto s =
          Subscriptions::create([&isUnsubscribed] { isUnsubscribed = true; });
      obs->onSubscribe(std::move(s));
      int i = 0;
      while (!isUnsubscribed && i <= 10) {
        emitted++;
        obs->onNext(i++);
      }
      if (!isUnsubscribed) {
        obs->onComplete();
      }
    });

    std::vector<int> v;
    a->subscribe(Reference<Observer<int>>(new TakeObserver(2, v)));
    EXPECT_EQ((unsigned long)2, v.size());
    EXPECT_EQ(2, emitted);
  }
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
}

TEST(Observable, toFlowable) {
  {
    ASSERT_EQ(std::size_t{0}, Refcounted::objects());
    auto a = Observable<int>::create([](Reference<Observer<int>> obs) {
      auto s = Subscriptions::empty();
      obs->onSubscribe(s);
      obs->onNext(1);
      obs->onComplete();
    });

    auto f = a->toFlowable(BackpressureStrategy::DROP);

    std::vector<int> v;
    f->subscribe(yarpl::flowable::Subscribers::create<int>(
        [&v](const int& value) { v.push_back(value); }));

    EXPECT_EQ(v.at(0), 1);
  }
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
}

TEST(Observable, toFlowableWithCancel) {
  {
    ASSERT_EQ(std::size_t{0}, Refcounted::objects());
    auto a = Observable<int>::create([](Reference<Observer<int>> obs) {
      auto s = Subscriptions::atomicBoolSubscription();
      obs->onSubscribe(s);
      int i = 0;
      while (!s->isCancelled()) {
        obs->onNext(++i);
      }
      if (!s->isCancelled()) {
        obs->onComplete();
      }
    });

    auto f = a->toFlowable(BackpressureStrategy::DROP);

    std::vector<int> v;
    f->take(5)->subscribe(yarpl::flowable::Subscribers::create<int>(
        [&v](const int& value) { v.push_back(value); }));

    EXPECT_EQ(v, std::vector<int>({1, 2, 3, 4, 5}));
  }
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
}

TEST(Observable, Just) {
  ASSERT_EQ(0u, Refcounted::objects());

  EXPECT_EQ(run(Observables::just(22)), std::vector<int>{22});
  EXPECT_EQ(
      run(Observables::justN({12, 34, 56, 98})),
      std::vector<int>({12, 34, 56, 98}));
  EXPECT_EQ(
      run(Observables::justN({"ab", "pq", "yz"})),
      std::vector<const char*>({"ab", "pq", "yz"}));

  EXPECT_EQ(0u, Refcounted::objects());
}

TEST(Observable, Range) {
  ASSERT_EQ(0u, Refcounted::objects());

  auto observable = Observables::range(10, 14);
  EXPECT_EQ(run(std::move(observable)), std::vector<int64_t>({10, 11, 12, 13}));

  EXPECT_EQ(0u, Refcounted::objects());
}

TEST(Observable, RangeWithMap) {
  ASSERT_EQ(0u, Refcounted::objects());

  auto observable = Observables::range(1, 4)
                        ->map([](int64_t v) { return v * v; })
                        ->map([](int64_t v) { return v * v; })
                        ->map([](int64_t v) { return std::to_string(v); });
  EXPECT_EQ(
      run(std::move(observable)), std::vector<std::string>({"1", "16", "81"}));

  EXPECT_EQ(0u, Refcounted::objects());
}

TEST(Observable, RangeWithFilter) {
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
  auto observable =
      Observables::range(0, 10)->filter([](int64_t v) { return v % 2 != 0; });
  EXPECT_EQ(run(std::move(observable)), std::vector<int64_t>({1, 3, 5, 7, 9}));
  EXPECT_EQ(std::size_t{0}, Refcounted::objects());
}

// TODO: Hits ASAN errors.
TEST(Observable, DISABLED_SimpleTake) {
  ASSERT_EQ(0u, Refcounted::objects());

  EXPECT_EQ(
      run(Observables::range(0, 100)->take(3)),
      std::vector<int64_t>({0, 1, 2}));

  EXPECT_EQ(0u, Refcounted::objects());
}

TEST(Observable, Error) {
  auto observable =
      Observables::error<int>(std::runtime_error("something broke!"));
  auto collector = make_ref<CollectingObserver<int>>();
  observable->subscribe(collector);

  EXPECT_EQ(collector->complete(), false);
  EXPECT_EQ(collector->error(), true);
  EXPECT_EQ(collector->errorMsg(), "something broke!");
}

TEST(Observable, ErrorPtr) {
  auto observable = Observables::error<int>(
      std::make_exception_ptr(std::runtime_error("something broke!")));
  auto collector = make_ref<CollectingObserver<int>>();
  observable->subscribe(collector);

  EXPECT_EQ(collector->complete(), false);
  EXPECT_EQ(collector->error(), true);
  EXPECT_EQ(collector->errorMsg(), "something broke!");
}

TEST(Observable, Empty) {
  auto observable = Observables::empty<int>();
  auto collector = make_ref<CollectingObserver<int>>();
  observable->subscribe(collector);

  EXPECT_EQ(collector->complete(), true);
  EXPECT_EQ(collector->error(), false);
}

TEST(Observable, ObserversComplete) {
  EXPECT_EQ(0u, Refcounted::objects());

  auto observable = Observables::empty<int>();
  EXPECT_EQ(1u, Refcounted::objects());

  bool completed = false;

  auto observer = Observers::create<int>(
      [](int) { unreachable(); },
      [](std::exception_ptr) { unreachable(); },
      [&] { completed = true; });

  observable->subscribe(std::move(observer));
  observable.reset();

  EXPECT_EQ(0u, Refcounted::objects());

  EXPECT_TRUE(completed);
}

TEST(Observable, ObserversError) {
  EXPECT_EQ(0u, Refcounted::objects());

  auto observable = Observables::error<int>(std::runtime_error("Whoops"));
  EXPECT_EQ(1u, Refcounted::objects());

  bool errored = false;

  auto observer = Observers::create<int>(
      [](int) { unreachable(); },
      [&](std::exception_ptr) { errored = true; },
      [] { unreachable(); });

  observable->subscribe(std::move(observer));
  observable.reset();

  EXPECT_EQ(0u, Refcounted::objects());

  EXPECT_TRUE(errored);
}

TEST(Observable, CancelReleasesObjects) {
  EXPECT_EQ(0u, Refcounted::objects());

  auto lambda = [](Reference<Observer<int>> observer) {
    // we will send nothing
  };
  auto observable = Observable<int>::create(std::move(lambda));

  EXPECT_EQ(1u, Refcounted::objects());

  auto collector = make_ref<CollectingObserver<int>>();
  observable->subscribe(collector);

  observable.reset();
  collector.reset();
  EXPECT_EQ(0u, Refcounted::objects());
}
