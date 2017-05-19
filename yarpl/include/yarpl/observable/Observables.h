#pragma once

#include <limits>

#include "Observable.h"

namespace yarpl {
namespace observable {

class Observables {
 public:
  static Reference<Observable<int64_t>> range(int64_t start, int64_t end) {
    auto lambda = [start, end](Reference<Observer<int64_t>> observer) {
      for (int64_t i = start; i < end; ++i) {
        observer->onNext(i);
      }
      observer->onComplete();
    };

    return Observable<int64_t>::create(std::move(lambda));
  }

  template <typename T>
  static Reference<Observable<T>> just(const T& value) {
    auto lambda = [value](Reference<Observer<T>> observer) {
      // # requested should be > 0.  Ignoring the actual parameter.
      observer->onNext(value);
      observer->onComplete();
    };

    return Observable<T>::create(std::move(lambda));
  }

  template <typename T>
  static Reference<Observable<T>> justN(std::initializer_list<T> list) {
    std::vector<T> vec(list);

    auto lambda = [v = std::move(vec)](Reference<Observer<T>> observer) {
      for (auto const& elem : v) {
        observer->onNext(elem);
      }
      observer->onComplete();
    };

    return Observable<T>::create(std::move(lambda));
  }

  template <
      typename T,
      typename OnSubscribe,
      typename = typename std::enable_if<
          std::is_callable<OnSubscribe(Reference<Observer<T>>), void>::value>::
          type>
  static Reference<Observable<T>> create(OnSubscribe&& function) {
    return Reference<Observable<T>>(new FromPublisherOperator<T, OnSubscribe>(
        std::forward<OnSubscribe>(function)));
  }

  template <typename T>
  static Reference<Observable<T>> empty() {
    auto lambda = [](Reference<Observer<T>> observer) {
      observer->onComplete();
    };
    return Observable<T>::create(std::move(lambda));
  }

  template <typename T>
  static Reference<Observable<T>> error(std::exception_ptr ex) {
    auto lambda = [ex](Reference<Observer<T>> observer) {
      observer->onError(ex);
    };
    return Observable<T>::create(std::move(lambda));
  }

  template <typename T, typename ExceptionType>
  static Reference<Observable<T>> error(const ExceptionType& ex) {
    auto lambda = [ex](Reference<Observer<T>> observer) {
      observer->onError(std::make_exception_ptr(ex));
    };
    return Observable<T>::create(std::move(lambda));
  }

 private:
  Observables() = delete;
};

} // observable
} // yarpl
