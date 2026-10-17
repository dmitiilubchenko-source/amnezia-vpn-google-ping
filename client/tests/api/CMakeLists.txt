add_executable(test_ui_api_services_model_and_controller
    testUiApiServicesModelAndController.cpp
)

target_link_libraries(test_ui_api_services_model_and_controller PRIVATE
    Qt6::Test
    test_common
)

add_executable(test_ui_news_model_and_controller
    testUiNewsModelAndController.cpp
)

target_link_libraries(test_ui_news_model_and_controller PRIVATE
    Qt6::Test
    test_common
)

add_test(NAME UiApiServicesModelAndControllerTest COMMAND test_ui_api_services_model_and_controller)
add_test(NAME UiNewsModelAndControllerTest COMMAND test_ui_news_model_and_controller)

add_executable(test_premium_latency ../testPremiumLatency.cpp)
target_link_libraries(test_premium_latency PRIVATE Qt6::Test test_common)
add_test(NAME PremiumLatencyTest COMMAND test_premium_latency)
add_executable(test_premium_benchmark ../testPremiumBenchmark.cpp)
target_link_libraries(test_premium_benchmark PRIVATE Qt6::Test test_common)
add_test(NAME PremiumBenchmarkTest COMMAND test_premium_benchmark)
