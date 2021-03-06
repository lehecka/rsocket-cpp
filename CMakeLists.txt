cmake_minimum_required(VERSION 3.2)
project(ReactiveSocket)

# Cmake modules.
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")

# Generate compilation database for use by YouCompleteMe.
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

# make sure to bail on in-source builds for cleanliness
if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
  message(FATAL_ERROR "In-source builds not allowed. Please make a new directory (called a build directory)"
          "and run CMake from there. You may need to remove CMakeCache.txt.")
endif()

# default built type is Release
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
endif(NOT CMAKE_BUILD_TYPE)

# default is to use ReactiveStreams location from github directly
if(NOT REACTIVE_STREAMS_GIT_URL)
  set(REACTIVE_STREAMS_GIT_URL "https://github.com/ReactiveSocket/reactive-streams-cpp.git" CACHE STRING "Location of the ReactiveStreams C++ git repo" FORCE)
endif(NOT REACTIVE_STREAMS_GIT_URL)

option(BUILD_BENCHMARKS "Build benchmarks" OFF)

enable_testing()

# Add a OSS macro.  This is mainly to get gflags working with travis.
add_definitions(-DOSS)

include(ExternalProject)
include(CTest)

# gmock
ExternalProject_Add(
  gmock
  URL ${CMAKE_CURRENT_SOURCE_DIR}/googletest-release-1.8.0.zip
  INSTALL_COMMAND ""
)

ExternalProject_Get_Property(gmock source_dir)
set(GMOCK_SOURCE_DIR ${source_dir})
ExternalProject_Get_Property(gmock binary_dir)
set(GMOCK_BINARY_DIR ${binary_dir})

set(GMOCK_LIBS
  ${GMOCK_BINARY_DIR}/${CMAKE_CFG_INTDIR}/googlemock/${CMAKE_STATIC_LIBRARY_PREFIX}gmock${CMAKE_STATIC_LIBRARY_SUFFIX}
  ${GMOCK_BINARY_DIR}/${CMAKE_CFG_INTDIR}/googlemock/${CMAKE_STATIC_LIBRARY_PREFIX}gmock_main${CMAKE_STATIC_LIBRARY_SUFFIX}
  )

# ReactiveStreams C++
ExternalProject_Add(
  ReactiveStreams
  GIT_REPOSITORY ${REACTIVE_STREAMS_GIT_URL}
  GIT_TAG d2fd61252b51a57a2916ee52fcd54b7f5d563591
  CMAKE_ARGS "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/reactivestreams"
)

find_package(Threads)

if(APPLE)
  set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
endif()

if(NOT META_CXX_STD)
  # Defaults to C++14 if not set:
  set(META_CXX_STD 14)
endif()

# Common configuration for all build modes.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++${META_CXX_STD} -pedantic")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter -Woverloaded-virtual")

set(EXTRA_CXX_FLAGS ${EXTRA_CXX_FLAGS} -Werror)

string(TOLOWER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_LOWER)
if("${BUILD_TYPE_LOWER}" MATCHES "debug")
  message("debug mode was set")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unreachable-code")
else()
  message("release mode was set")
endif()

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  set(TEST_CXX_FLAGS ${TEST_CXX_FLAGS} -Wno-inconsistent-missing-override)
endif()

find_library(DOUBLE-CONVERSION double-conversion)

find_package(Folly REQUIRED)
find_package(OpenSSL REQUIRED)

# Find glog and gflags libraries specifically
find_path(GLOG_INCLUDE_DIR glog/logging.h)
find_path(GFLAGS_INCLUDE_DIR gflags/gflags.h)

find_library(GLOG_LIBRARY glog)
find_library(GFLAGS_LIBRARY gflags)

message("gflags include_dir <${GFLAGS_INCLUDE_DIR}> lib <${GFLAGS_LIBRARY}>")
message("glog include_dir <${GLOG_INCLUDE_DIR}> lib <${GLOG_LIBRARY}>")

include_directories(SYSTEM ${FOLLY_INCLUDE_DIR})
include_directories(SYSTEM ${OPENSSL_INCLUDE_DIR})

include_directories(SYSTEM ${GFLAGS_INCLUDE_DIR})
include_directories(SYSTEM ${GLOG_INCLUDE_DIR})

include_directories(${CMAKE_SOURCE_DIR})

include_directories(${CMAKE_CURRENT_BINARY_DIR}/reactivestreams/include)
include_directories(${GMOCK_SOURCE_DIR}/googlemock/include)
include_directories(${GMOCK_SOURCE_DIR}/googletest/include)

add_subdirectory(experimental/yarpl)

add_library(
  ReactiveSocket
  src/automata/ChannelRequester.cpp
  src/automata/ChannelRequester.h
  src/automata/ChannelResponder.cpp
  src/automata/ChannelResponder.h
  src/automata/ConsumerBase.cpp
  src/automata/ConsumerBase.h
  src/automata/PublisherBase.h
  src/automata/RequestResponseRequester.cpp
  src/automata/RequestResponseRequester.h
  src/automata/RequestResponseResponder.cpp
  src/automata/RequestResponseResponder.h
  src/automata/StreamAutomatonBase.cpp
  src/automata/StreamAutomatonBase.h
  src/automata/StreamRequester.cpp
  src/automata/StreamRequester.h
  src/automata/StreamResponder.cpp
  src/automata/StreamResponder.h
  src/ClientResumeStatusCallback.h
  src/Common.cpp
  src/Common.h
  src/ConnectionAutomaton.cpp
  src/ConnectionAutomaton.h
  src/ConnectionSetupPayload.cpp
  src/ConnectionSetupPayload.h
  src/DuplexConnection.h
  src/EnableSharedFromThis.h
  src/Executor.cpp
  src/Executor.h
  src/folly/FollyKeepaliveTimer.cpp
  src/folly/FollyKeepaliveTimer.h
  src/Frame.cpp
  src/Frame.h
  src/framed/FramedDuplexConnection.cpp
  src/framed/FramedDuplexConnection.h
  src/framed/FramedReader.cpp
  src/framed/FramedReader.h
  src/framed/FramedWriter.cpp
  src/framed/FramedWriter.h
  src/FrameProcessor.h
  src/FrameSerializer.cpp
  src/FrameSerializer.h
  src/FrameTransport.cpp
  src/FrameTransport.h
  src/NullRequestHandler.cpp
  src/NullRequestHandler.h
  src/Payload.cpp
  src/Payload.h
  src/ReactiveStreamsCompat.h
  src/RequestHandler.h
  src/ResumeCache.cpp
  src/ResumeCache.h
  src/ServerConnectionAcceptor.cpp
  src/ServerConnectionAcceptor.h
  src/ReactiveSocket.cpp
  src/ReactiveSocket.h
  src/Stats.cpp
  src/Stats.h
  src/StreamsFactory.cpp
  src/StreamsFactory.h
  src/StreamsHandler.h
  src/StreamState.cpp
  src/StreamState.h
  src/SubscriberBase.h
  src/SubscriptionBase.h
  src/tcp/TcpDuplexConnection.cpp
  src/tcp/TcpDuplexConnection.h
  src/versions/FrameSerializer_v0.cpp
  src/versions/FrameSerializer_v0.h
  src/versions/FrameSerializer_v0_1.cpp
  src/versions/FrameSerializer_v0_1.h
  src/versions/FrameSerializer_v1_0.cpp
  src/versions/FrameSerializer_v1_0.h)

target_include_directories(ReactiveSocket PUBLIC "${PROJECT_SOURCE_DIR}/experimental")
target_include_directories(ReactiveSocket PUBLIC "${PROJECT_SOURCE_DIR}/experimental/yarpl/include")
target_include_directories(ReactiveSocket PUBLIC "${PROJECT_SOURCE_DIR}/experimental/yarpl/src")

target_link_libraries(
  ReactiveSocket
  ${FOLLY_LIBRARIES}
  ${GFLAGS_LIBRARY}
  ${GLOG_LIBRARY})

add_dependencies(ReactiveSocket ReactiveStreams yarpl)

target_compile_options(
  ReactiveSocket
  PRIVATE ${EXTRA_CXX_FLAGS})

enable_testing()

# EXCLUDE_FROM_ALL so 'make install' doesn't attempt installation of googletest
#add_subdirectory(external/googletest EXCLUDE_FROM_ALL)

add_executable(
  tests
  test/ConnectionAutomatonTest.cpp
  test/framed/FramedReaderTest.cpp
  test/framed/FramedWriterTest.cpp
  test/automata/PublisherBaseTest.cpp
  test/FrameTest.cpp
  test/InlineConnection.cpp
  test/InlineConnection.h
  test/InlineConnectionTest.cpp
  test/MockKeepaliveTimer.h
  test/MockRequestHandler.h
  test/MockStats.h
  test/ReactiveSocketConcurrencyTest.cpp
  test/ReactiveSocketTest.cpp
  test/SubscriberBaseTest.cpp
  test/Test.cpp
  test/folly/FollyKeepaliveTimerTest.cpp
  test/ReactiveSocketResumabilityTest.cpp
  test/AllowanceSemaphoreTest.cpp
  test/ResumeIdentificationTokenTest.cpp
  test/ServerConnectionAcceptorTest.cpp
  test/PayloadTest.cpp
  test/ResumeCacheTest.cpp
  test/StreamStateTest.cpp
  test/integration/ClientUtils.h
  test/integration/ServerFixture.h
  test/integration/ServerFixture.cpp
  test/integration/WarmResumptionTest.cpp
  test/streams/Mocks.h
  test/FrameTransportTest.cpp)

target_link_libraries(
  tests
  ReactiveSocket
  yarpl
  ${FOLLY_LIBRARIES}
  ${GMOCK_LIBS}
  ${GFLAGS_LIBRARY}
  ${GLOG_LIBRARY}
  ${CMAKE_THREAD_LIBS_INIT})

target_compile_options(
  tests
  PRIVATE ${TEST_CXX_FLAGS})

add_dependencies(tests gmock ReactiveSocket)

add_test(NAME ReactiveSocketTests COMMAND tests)

add_executable(
        tcpclient
        test/tcp/TcpClient.cpp
        test/simple/PrintSubscriber.cpp
        test/simple/PrintSubscriber.h
        src/ReactiveSocket.cpp
        src/ReactiveSocket.h
        test/simple/StatsPrinter.cpp
        test/simple/StatsPrinter.h)

target_link_libraries(
        tcpclient
        ReactiveSocket
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GMOCK_LIBS}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})

add_dependencies(tcpclient gmock)

add_executable(
        tcpserver
        test/tcp/TcpServer.cpp
        test/simple/PrintSubscriber.cpp
        test/simple/PrintSubscriber.h
        test/simple/StatsPrinter.cpp
        test/simple/StatsPrinter.h)

target_link_libraries(
        tcpserver
        ReactiveSocket
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GMOCK_LIBS}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})

add_dependencies(tcpserver gmock)

add_executable(
        tckclient
        tck-test/client.cpp
        tck-test/TestFileParser.cpp
        tck-test/TestFileParser.h
        tck-test/TestSubscriber.cpp
        tck-test/TestSubscriber.h
        tck-test/TestSuite.cpp
        tck-test/TestSuite.h
        tck-test/TestInterpreter.cpp
        tck-test/TestInterpreter.h
        tck-test/TypedCommands.h)

target_link_libraries(
        tckclient
        ReactiveSocket
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})

add_executable(
        tckserver
        tck-test/server.cpp
        tck-test/MarbleProcessor.cpp
        tck-test/MarbleProcessor.h
        test/simple/StatsPrinter.cpp
        test/simple/StatsPrinter.h)

target_link_libraries(
        tckserver
        ReactiveSocket
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GMOCK_LIBS}
        ${GLOG_LIBRARY}
        ${DOUBLE-CONVERSION}
        ${CMAKE_THREAD_LIBS_INIT})

add_executable(
        tcpresumeclient
        test/resume/TcpResumeClient.cpp
        test/simple/PrintSubscriber.cpp
        test/simple/PrintSubscriber.h
        src/ReactiveSocket.cpp
        src/ReactiveSocket.h
        test/simple/StatsPrinter.cpp
        test/simple/StatsPrinter.h)

target_link_libraries(
        tcpresumeclient
        ReactiveSocket
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GMOCK_LIBS}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})

add_dependencies(tcpresumeclient gmock)

add_executable(
        tcpresumeserver
        test/resume/TcpResumeServer.cpp
        test/simple/PrintSubscriber.cpp
        test/simple/PrintSubscriber.h
        test/simple/StatsPrinter.cpp
        test/simple/StatsPrinter.h)

target_link_libraries(
        tcpresumeserver
        ReactiveSocket
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GMOCK_LIBS}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})

add_dependencies(tcpresumeserver gmock)

########################################
# RSocket Experimental
########################################

add_library(
        rsocket_experimental
        experimental/rsocket/RSocket.h
        # TODO remove when ReactiveStreams all synced
        experimental/rsocket/OldNewBridge.h
        experimental/rsocket-src/RSocket.cpp
        experimental/rsocket/RSocketServer.h
        experimental/rsocket-src/RSocketServer.cpp
        experimental/rsocket/RSocketClient.h
        experimental/rsocket-src/RSocketClient.cpp
        experimental/rsocket/RSocketRequester.h
        experimental/rsocket-src/RSocketRequester.cpp
        experimental/rsocket/RSocketErrors.h
        experimental/rsocket/ConnectionAcceptor.h
        experimental/rsocket/ConnectionFactory.h
        experimental/rsocket/ConnectionSetupRequest.h
        experimental/rsocket-src/ConnectionSetupRequest.cpp
        experimental/rsocket/ConnectionResumeRequest.h
        experimental/rsocket-src/ConnectionResumeRequest.cpp
        experimental/rsocket/transports/TcpConnectionAcceptor.h
        experimental/rsocket-src/transports/TcpConnectionAcceptor.cpp
        experimental/rsocket/transports/TcpConnectionFactory.h
        experimental/rsocket-src/transports/TcpConnectionFactory.cpp
        experimental/rsocket/RSocketResponder.h
        experimental/rsocket/RSocketConnectionHandler.h
        experimental/rsocket-src/RSocketConnectionHandler.cpp
)

add_dependencies(rsocket_experimental ReactiveStreams yarpl)

# include the experimental includes for usage
target_include_directories(rsocket_experimental PUBLIC "${PROJECT_SOURCE_DIR}/experimental")
# yarpl in its current experimental paths
target_include_directories(rsocket_experimental PUBLIC "${PROJECT_SOURCE_DIR}/experimental/yarpl/include")
target_include_directories(rsocket_experimental PUBLIC "${PROJECT_SOURCE_DIR}/experimental/yarpl/src")
#include_directories(${CMAKE_CURRENT_BINARY_DIR}/experimental)


add_executable(
        rsocket_tests
        experimental/rsocket-test/RSocketClientServerTest.cpp
        experimental/rsocket-test/handlers/HelloStreamRequestHandler.h
        experimental/rsocket-test/handlers/HelloStreamRequestHandler.cpp
)

target_link_libraries(
        rsocket_tests
        rsocket_experimental
        yarpl
        ReactiveSocket
        ${FOLLY_LIBRARIES}
        ${GMOCK_LIBS}
        ${GFLAGS_LIBRARY}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})

add_dependencies(rsocket_tests gmock rsocket_experimental)

add_test(NAME RSocketTests COMMAND rsocket_tests)

########################################
# Examples
########################################

add_library(
        reactivesocket_examples_util
        examples/util/ExampleSubscriber.cpp
        examples/util/ExampleSubscriber.h
)

target_link_libraries(
        reactivesocket_examples_util
        rsocket_experimental
        yarpl
        ReactiveSocket
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT}
)

# request-response-hello-world

add_executable(
        example_request-response-hello-world-server
        examples/request-response-hello-world/RequestResponseHelloWorld_Server.cpp
)

target_link_libraries(
        example_request-response-hello-world-server
        ReactiveSocket
        rsocket_experimental
        reactivesocket_examples_util
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})

add_executable(
        example_request-response-hello-world-client
        examples/request-response-hello-world/RequestResponseHelloWorld_Client.cpp
)

target_link_libraries(
        example_request-response-hello-world-client
        ReactiveSocket
        rsocket_experimental
        reactivesocket_examples_util
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})

# fire-and-forget-hello-world

add_executable(
        example_fire-and-forget-hello-world-server
        examples/fire-and-forget-hello-world/FireAndForgetHelloWorld_Server.cpp
)

target_link_libraries(
        example_fire-and-forget-hello-world-server
        ReactiveSocket
        rsocket_experimental
        reactivesocket_examples_util
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})

add_executable(
        example_fire-and-forget-hello-world-client
        examples/fire-and-forget-hello-world/FireAndForgetHelloWorld_Client.cpp
)

target_link_libraries(
        example_fire-and-forget-hello-world-client
        ReactiveSocket
        rsocket_experimental
        reactivesocket_examples_util
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})


# stream-hello-world

add_executable(
        example_stream-hello-world-server
        examples/stream-hello-world/StreamHelloWorld_Server.cpp
)

target_link_libraries(
        example_stream-hello-world-server
        ReactiveSocket
        rsocket_experimental
        reactivesocket_examples_util
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})

add_executable(
        example_stream-hello-world-client
        examples/stream-hello-world/StreamHelloWorld_Client.cpp
)

target_link_libraries(
        example_stream-hello-world-client
        ReactiveSocket
        rsocket_experimental
        reactivesocket_examples_util
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})


# channel-hello-world

add_executable(
        example_channel-hello-world-server
        examples/channel-hello-world/ChannelHelloWorld_Server.cpp
)

target_link_libraries(
        example_channel-hello-world-server
        ReactiveSocket
        rsocket_experimental
        reactivesocket_examples_util
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})

add_executable(
        example_channel-hello-world-client
        examples/channel-hello-world/ChannelHelloWorld_Client.cpp
)

target_link_libraries(
        example_channel-hello-world-client
        ReactiveSocket
        rsocket_experimental
        reactivesocket_examples_util
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})

# stream-observable-to-flowable

add_executable(
        example_observable-to-flowable-server
        examples/stream-observable-to-flowable/StreamObservableToFlowable_Server.cpp
)

target_link_libraries(
        example_observable-to-flowable-server
        ReactiveSocket
        rsocket_experimental
        reactivesocket_examples_util
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})

add_executable(
        example_observable-to-flowable-client
        examples/stream-observable-to-flowable/StreamObservableToFlowable_Client.cpp
)

target_link_libraries(
        example_observable-to-flowable-client
        ReactiveSocket
        rsocket_experimental
        reactivesocket_examples_util
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})

# conditional-request-handling

add_executable(
        example_conditional-request-handling-server
        examples/conditional-request-handling/ConditionalRequestHandling_Server.cpp
        examples/conditional-request-handling/TextRequestHandler.h
        examples/conditional-request-handling/TextRequestHandler.cpp
        examples/conditional-request-handling/JsonRequestHandler.cpp
        examples/conditional-request-handling/JsonRequestHandler.h
)

target_link_libraries(
        example_conditional-request-handling-server
        ReactiveSocket
        rsocket_experimental
        reactivesocket_examples_util
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})

add_executable(
        example_conditional-request-handling-client
        examples/conditional-request-handling/ConditionalRequestHandling_Client.cpp
)

target_link_libraries(
        example_conditional-request-handling-client
        ReactiveSocket
        rsocket_experimental
        reactivesocket_examples_util
        yarpl
        ${FOLLY_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${GLOG_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT})


########################################
# End Examples
########################################

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif(BUILD_BENCHMARKS)

# EOF
