# The smoke test: a document using every feature, rendered with Dear ImGui's null backend (no window, no GPU).
#   cmake -S tests -B build_tests -DCMAKE_BUILD_TYPE=Debug && cmake --build build_tests && ctest --test-dir build_tests
# Build it in Debug, so that the assertions of Dear ImGui and of the library are active.
cmake_minimum_required(VERSION 3.20)
project(imgui_rich_md_tests CXX C)
set(CMAKE_CXX_STANDARD 17)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/imgui_richmd_fetch_imgui.cmake)
imgui_richmd_fetch_imgui()
if(NOT TARGET imgui)  # Dear ImGui's core only: the test needs no platform backend
    add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp)
    target_include_directories(imgui PUBLIC ${IMGUI_DIR})
endif()
if(NOT TARGET imgui_rich_md)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/.. imgui_rich_md_build)
endif()

add_executable(imgui_rich_md_smoke_test ${CMAKE_CURRENT_LIST_DIR}/smoke_test.cpp ${IMGUI_DIR}/backends/imgui_impl_null.cpp)
target_include_directories(imgui_rich_md_smoke_test PRIVATE ${IMGUI_DIR}/backends)
target_link_libraries(imgui_rich_md_smoke_test PRIVATE imgui_rich_md)
enable_testing()
add_test(NAME smoke_test COMMAND imgui_rich_md_smoke_test)
