add_library(hook_dll SHARED
    dllmain.cpp
    ../fake_data/fake_data.cpp
    ../reg_hook/reg_hook.cpp
    ../wmi_hook/wmi_hook.cpp
)

target_link_libraries(hook_dll PRIVATE
    minhook
    advapi32
    ole32
    oleaut32
    wbemuuid
)

set_target_properties(hook_dll PROPERTIES OUTPUT_NAME "hook_dll")
