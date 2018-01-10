// Copyright 2004-present Facebook. All Rights Reserved.

#include <folly/ExceptionWrapper.h>
#include <folly/synchronization/Baton.h>
#include <gtest/gtest.h>
#include <atomic>

#include "yarpl/Single.h"
#include "yarpl/single/SingleTestObserver.h"
#include "yarpl/test_utils/Tuple.h"

// TODO can we eliminate need to import both of these?
using namespace yarpl;
using namespace yarpl::single;

TEST(Single, SingleOnNext) {
  auto a = Single<int>::create([](std::shared_ptr<SingleObserver<int>> obs) {
    obs->onSubscribe(SingleSubscriptions::empty());
    obs->onSuccess(1);
  });

  auto to = SingleTestObserver<int>::create();
  a->subscribe(to);
  to->awaitTerminalEvent();
  to->assertOnSuccessValue(1);
}

TEST(Single, OnError) {
  std::string errorMessage("DEFAULT->No Error Message");
  auto a = Single<int>::create([](std::shared_ptr<SingleObserver<int>> obs) {
    obs->onError(
        folly::exception_wrapper(std::runtime_error("something broke!")));
  });

  auto to = SingleTestObserver<int>::create();
  a->subscribe(to);
  to->awaitTerminalEvent();
  to->assertOnErrorMessage("something broke!");
}

TEST(Single, Just) {
  auto a = Singles::just<int>(1);

  auto to = SingleTestObserver<int>::create();
  a->subscribe(to);
  to->awaitTerminalEvent();
  to->assertOnSuccessValue(1);
}

TEST(Single, Error) {
  std::string errorMessage("DEFAULT->No Error Message");
  auto a = Singles::error<int>(std::runtime_error("something broke!"));

  auto to = SingleTestObserver<int>::create();
  a->subscribe(to);
  to->awaitTerminalEvent();
  to->assertOnErrorMessage("something broke!");
}

TEST(Single, SingleMap) {
  auto a = Single<int>::create([](std::shared_ptr<SingleObserver<int>> obs) {
    obs->onSubscribe(SingleSubscriptions::empty());
    obs->onSuccess(1);
  });

  auto to = SingleTestObserver<const char*>::create();
  a->map([](int) { return "hello"; })->subscribe(to);
  to->awaitTerminalEvent();
  to->assertOnSuccessValue("hello");
}

TEST(Single, MapWithException) {
  auto single = Singles::just<int>(3)->map([](int n) {
    if (n > 2) {
      throw std::runtime_error{"Too big!"};
    }
    return n;
  });

  auto observer = yarpl::make_ref<SingleTestObserver<int>>();
  single->subscribe(observer);

  observer->assertOnErrorMessage("Too big!");
}
