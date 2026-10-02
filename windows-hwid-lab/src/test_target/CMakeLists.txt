add_executable(test_target main.cpp)
target_link_libraries(test_target PRIVATE advapi32 ole32 oleaut32 wbemuuid)
