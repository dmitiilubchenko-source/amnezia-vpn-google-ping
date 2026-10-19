cmake_minimum_required(VERSION 3.25)
project(PremiumBenchmarkUnitTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent Test)
set(CLIENT "${CMAKE_CURRENT_SOURCE_DIR}/../../client")
add_executable(test_premium_benchmark
    ${CLIENT}/tests/testPremiumBenchmark.cpp
    ${CLIENT}/ui/controllers/api/premiumBenchmarkController.cpp
    ${CLIENT}/ui/controllers/api/premiumBenchmarkController.h
    ${CLIENT}/ui/controllers/api/googlePingProbe.cpp
    ${CLIENT}/ui/controllers/api/googlePingProbe.h
    ${CLIENT}/ui/controllers/api/pingStatistics.cpp)
target_include_directories(test_premium_benchmark PRIVATE ${CLIENT})
target_link_libraries(test_premium_benchmark PRIVATE Qt6::Test Qt6::Network Qt6::Concurrent)
if(WIN32)
    target_link_libraries(test_premium_benchmark PRIVATE iphlpapi ws2_32)
endif()
add_test(NAME PremiumBenchmarkTest COMMAND test_premium_benchmark)
add_executable(test_gateway_request_deadline ${CLIENT}/tests/testGatewayRequestDeadline.cpp)
target_include_directories(test_gateway_request_deadline PRIVATE ${CLIENT})
target_link_libraries(test_gateway_request_deadline PRIVATE Qt6::Test Qt6::Network)
add_test(NAME GatewayRequestDeadlineTest COMMAND test_gateway_request_deadline)
