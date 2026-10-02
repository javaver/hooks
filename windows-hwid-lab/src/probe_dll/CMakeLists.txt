add_library(probe_dll SHARED
    dllmain.cpp
    ../probe_hook/probe_hook.cpp
    ../probe_hook/log_util.cpp
)

target_link_libraries(probe_dll PRIVATE
    minhook
    advapi32
    ole32
    oleaut32
    wbemuuid
)

set_target_properties(probe_dll PROPERTIES OUTPUT_NAME "probe_dll")
