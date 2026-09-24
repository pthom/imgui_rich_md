# Stock Dear ImGui, fetched for the examples and the tests (the library itself uses its parent's `imgui` target).
# Sets IMGUI_DIR to its sources; each caller makes the `imgui` target with the backends it needs.
macro(imgui_richmd_fetch_imgui)
    include(FetchContent)
    FetchContent_Declare(imgui_stock GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.92.9 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(imgui_stock)
    set(IMGUI_DIR ${imgui_stock_SOURCE_DIR})
endmacro()
