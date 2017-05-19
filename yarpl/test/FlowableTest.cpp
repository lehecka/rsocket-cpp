#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "yarpl/Flowable.h"

namespace yarpl {
namespace flowable {
namespace {

void unreachable() {
  EXPECT_TRUE(false);
}

template <typename T>
class CollectingSubscriber : public Subscriber<T> {
 public:
  explicit CollectingSubscriber(int64_t requestCount = 100)
      : requestCount_(requestCount) {}

  void onSubscribe(Reference<Subscription> subscription) override {
    Subscriber<T>::onSubscribe(subscription);
    subscription->request(requestCount_);
  }

  void onNext(T next) override {
    values_.push_back(std::move(next));
  }

  void onComplete() override {
    Subscriber<T>::onComplete();
    complete_ = true;
  }

  void onError(const std::exception_ptr ex) override {
    Subscriber<T>::onError(ex);
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

  void cancelSubscription() {
    Subscriber<T>::subscription()->cancel();
  }

 private:
  std::vector<T> values_;
  std::string errorMsg_;
  bool complete_{false};
  bool error_{false};
  int64_t requestCount_;
};

/// Construct a pipeline with a collecting subscriber against the supplied
/// flowable.  Return the items that were sent to the subscriber.  If some
/// exception was sent, the exception is thrown.
template <typename T>
std::vector<T> run(
    Reference<Flowable<T>> flowable,
    uint64_t requestCount = 100) {
  auto collector = make_ref<CollectingSubscriber<T>>(requestCount);
  flowable->subscribe(collector);
  return collector->values();
}

} // namespace

TEST(FlowableTest, SingleFlowable) {
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());

  auto flowable = Flowables::just(10);
  EXPECT_EQ(std::size_t{1}, Refcounted::objects());
  EXPECT_EQ(std::size_t{1}, flowable->count());

  flowable.reset();
  EXPECT_EQ(std::size_t{0}, Refcounted::objects());
}

TEST(FlowableTest, JustFlowable) {
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
  EXPECT_EQ(run(Flowables::just(22)), std::vector<int>{22});
  EXPECT_EQ(
      run(Flowables::justN({12, 34, 56, 98})),
      std::vector<int>({12, 34, 56, 98}));
  EXPECT_EQ(
      run(Flowables::justN({"ab", "pq", "yz"})),
      std::vector<const char*>({"ab", "pq", "yz"}));
  EXPECT_EQ(std::size_t{0}, Refcounted::objects());
}

TEST(FlowableTest, JustIncomplete) {
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
  auto flowable = Flowables::justN<std::string>({"a", "b", "c"})->take(2);
  EXPECT_EQ(run(std::move(flowable)), std::vector<std::string>({"a", "b"}));
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());

  flowable = Flowables::justN<std::string>({"a", "b", "c"})->take(2)->take(1);
  EXPECT_EQ(run(std::move(flowable)), std::vector<std::string>({"a"}));
  flowable.reset();
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());

  flowable = Flowables::justN<std::string>(
                 {"a", "b", "c", "d", "e", "f", "g", "h", "i"})
                 ->map([](std::string s) {
                   s[0] = ::toupper(s[0]);
                   return s;
                 })
                 ->take(5);

  EXPECT_EQ(
      run(std::move(flowable)),
      std::vector<std::string>({"A", "B", "C", "D", "E"}));
  flowable.reset();
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
}

TEST(FlowableTest, Range) {
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
  EXPECT_EQ(
      run(Flowables::range(10, 5)),
      std::vector<int64_t>({10, 11, 12, 13, 14}));
  EXPECT_EQ(std::size_t{0}, Refcounted::objects());
}

TEST(FlowableTest, RangeWithMap) {
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
  auto flowable = Flowables::range(1, 3)
                      ->map([](int64_t v) { return v * v; })
                      ->map([](int64_t v) { return v * v; })
                      ->map([](int64_t v) { return std::to_string(v); });
  EXPECT_EQ(
      run(std::move(flowable)), std::vector<std::string>({"1", "16", "81"}));
  EXPECT_EQ(std::size_t{0}, Refcounted::objects());
}

TEST(FlowableTest, RangeWithFilterRequestMoreItems) {
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
  auto flowable =
      Flowables::range(0, 10)->filter([](int64_t v) { return v % 2 != 0; });
  EXPECT_EQ(run(std::move(flowable)), std::vector<int64_t>({1, 3, 5, 7, 9}));
  EXPECT_EQ(std::size_t{0}, Refcounted::objects());
}

TEST(FlowableTest, RangeWithFilterRequestLessItems) {
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
  auto flowable =
      Flowables::range(0, 10)->filter([](int64_t v) { return v % 2 != 0; });
  EXPECT_EQ(run(std::move(flowable), 5), std::vector<int64_t>({1, 3, 5, 7, 9}));
  EXPECT_EQ(std::size_t{0}, Refcounted::objects());
}

TEST(FlowableTest, SimpleTake) {
  ASSERT_EQ(std::size_t{0}, Refcounted::objects());
  EXPECT_EQ(
      run(Flowables::range(0, 100)->take(3)), std::vector<int64_t>({0, 1, 2}));
  EXPECT_EQ(
      run(Flowables::range(10, 5)),
      std::vector<int64_t>({10, 11, 12, 13, 14}));
  EXPECT_EQ(std::size_t{0}, Refcounted::objects());
}

TEST(FlowableTest, FlowableError) {
  auto flowable = Flowables::error<int>(std::runtime_error("something broke!"));
  auto collector = make_ref<CollectingSubscriber<int>>();
  flowable->subscribe(collector);

  EXPECT_EQ(collector->complete(), false);
  EXPECT_EQ(collector->error(), true);
  EXPECT_EQ(collector->errorMsg(), "something broke!");
}

TEST(FlowableTest, FlowableErrorPtr) {
  auto flowable = Flowables::error<int>(
      std::make_exception_ptr(std::runtime_error("something broke!")));
  auto collector = make_ref<CollectingSubscriber<int>>();
  flowable->subscribe(collector);

  EXPECT_EQ(collector->complete(), false);
  EXPECT_EQ(collector->error(), true);
  EXPECT_EQ(collector->errorMsg(), "something broke!");
}

TEST(FlowableTest, FlowableEmpty) {
  auto flowable = Flowables::empty<int>();
  auto collector = make_ref<CollectingSubscriber<int>>();
  flowable->subscribe(collector);

  EXPECT_EQ(collector->complete(), true);
  EXPECT_EQ(collector->error(), false);
}

TEST(FlowableTest, FlowableFromGenerator) {
  EXPECT_EQ(0u, Refcounted::objects());

  auto flowable = Flowables::fromGenerator<std::unique_ptr<int>>(
      [] {return std::unique_ptr<int>();}
  );
  EXPECT_EQ(1u, Refcounted::objects());

  auto collector = make_ref<CollectingSubscriber<std::unique_ptr<int>>>(10);
  flowable->subscribe(collector);

  EXPECT_EQ(collector->complete(), false);
  EXPECT_EQ(collector->error(), false);
  EXPECT_EQ(std::size_t{10}, collector->values().size());

  collector->cancelSubscription();

  flowable.reset();
  collector.reset();
  EXPECT_EQ(0u, Refcounted::objects());
}

TEST(FlowableTest, FlowableFromGeneratorException) {
  EXPECT_EQ(0u, Refcounted::objects());

  int count = 5;
  auto flowable = Flowables::fromGenerator<std::unique_ptr<int>>(
  [&] {
    while (count--) { return std::unique_ptr<int>(); }
    throw std::runtime_error("error from generator");
  });
  EXPECT_EQ(1u, Refcounted::objects());

  auto collector = make_ref<CollectingSubscriber<std::unique_ptr<int>>>(10);
  flowable->subscribe(collector);

  EXPECT_EQ(collector->complete(), false);
  EXPECT_EQ(collector->error(), true);
  EXPECT_EQ(std::size_t{5}, collector->values().size());

  collector.reset();
  flowable.reset();
  EXPECT_EQ(0u, Refcounted::objects());
}

TEST(FlowableTest, SubscribersComplete) {
  EXPECT_EQ(0u, Refcounted::objects());

  auto flowable = Flowables::empty<int>();
  EXPECT_EQ(1u, Refcounted::objects());

  bool completed = false;

  auto subscriber = Subscribers::create<int>(
      [](int) { unreachable(); },
      [](std::exception_ptr) { unreachable(); },
      [&] { completed = true; });

  flowable->subscribe(std::move(subscriber));
  flowable.reset();

  EXPECT_EQ(0u, Refcounted::objects());

  EXPECT_TRUE(completed);
}

TEST(FlowableTest, SubscribersError) {
  EXPECT_EQ(0u, Refcounted::objects());

  auto flowable = Flowables::error<int>(std::runtime_error("Whoops"));
  EXPECT_EQ(1u, Refcounted::objects());

  bool errored = false;

  auto subscriber = Subscribers::create<int>(
      [](int) { unreachable(); },
      [&](std::exception_ptr) { errored = true; },
      [] { unreachable(); });

  flowable->subscribe(std::move(subscriber));
  flowable.reset();

  EXPECT_EQ(0u, Refcounted::objects());

  EXPECT_TRUE(errored);
}

} // flowable
} // yarpl
