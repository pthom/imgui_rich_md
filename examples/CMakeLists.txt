# The examples: stock Dear ImGui (fetched, v1.92.9) + GLFW + OpenGL3, no other framework, the assets embedded.
#   cmake -S examples -B build && cmake --build build   (or IMGUI_RICHMD_BUILD_EXAMPLES=ON at the root)
cmake_minimum_required(VERSION 3.20)
project(imgui_rich_md_examples CXX C)
set(CMAKE_CXX_STANDARD 17)

find_package(OpenGL REQUIRED)
find_package(glfw3 QUIET)
if(NOT TARGET glfw)  # no system GLFW (e.g. Windows): fetched
    include(FetchContent)
    set(GLFW_BUILD_DOCS OFF)
    set(GLFW_BUILD_EXAMPLES OFF)
    set(GLFW_BUILD_TESTS OFF)
    FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glfw)
endif()
if(NOT TARGET imgui)
    include(FetchContent)
    FetchContent_Declare(imgui_stock GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.92.9 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(imgui_stock)
    set(IMGUI_DIR ${imgui_stock_SOURCE_DIR})
    add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp)  # InputText with std::string (editor)
    target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc/cpp)
    target_compile_definitions(imgui PUBLIC IMGUI_USE_WCHAR32)  # characters above U+FFFF, e.g. most emoji (fonts)
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
endif()
if(NOT TARGET imgui_rich_md)
    set(IMGUI_RICHMD_EMBED_ASSETS ON)
    set(IMGUI_RICHMD_STB_IMAGE_IMPLEMENTATION ON)
    set(IMGUI_RICHMD_EMBED_EXTRA_FILES images/world.png)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/.. imgui_rich_md_build)
endif()

# The fonts example merges fonts that the library does not ship: they are downloaded once into the build folder
# (pinned versions, checked by hash). Without network access, the fonts example is skipped.
set(_fonts_dir ${CMAKE_CURRENT_BINARY_DIR}/fonts_example)
set(_fonts_ok ON)
foreach(_download
        "fonts/NotoSansJP-Regular.otf|https://raw.githubusercontent.com/notofonts/noto-cjk/165c01b46ea533872e002e0785ff17e44f6d97d8/Sans/SubsetOTF/JP/NotoSansJP-Regular.otf|dff723ba59d57d136764a04b9b2d03205544f7cd785a711442d6d2d085ac5073"
        "fonts/NotoEmoji.ttf|https://raw.githubusercontent.com/google/fonts/b979dba422e445492b0eb9951ac52ee0b4d648c3/ofl/notoemoji/NotoEmoji%5Bwght%5D.ttf|de6c18832938afc99caf132b39d6a30a19bac7f2e812e28db2535b4608d27551"
        "fonts/fa-solid-900.ttf|https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.7.2/webfonts/fa-solid-900.ttf|af19d135d3a935b3ebfbd80320716ffe1202052c5f68dc2c5f1abc57005ac605"
        "include/IconsFontAwesome6.h|https://raw.githubusercontent.com/juliettef/IconFontCppHeaders/8a381189ecfe58e732466cc52e79ca887dd6a297/IconsFontAwesome6.h|1986b023825b269fb8c60ccc2419c4594280249596e828e573d5c8ef309e265d")
    string(REPLACE "|" ";" _download "${_download}")
    list(GET _download 0 _file)
    list(GET _download 1 _url)
    list(GET _download 2 _sha256)
    file(DOWNLOAD ${_url} ${_fonts_dir}/${_file} EXPECTED_HASH SHA256=${_sha256} STATUS _status)
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
        message(WARNING "imgui_rich_md: the fonts example is skipped, ${_url} could not be downloaded (${_status})")
        set(_fonts_ok OFF)
    endif()
endforeach()

# One folder per example, with its main.cpp; common/ holds the window shared by the examples
set(_examples minimal tour editor custom_host)
if(_fonts_ok)
    list(APPEND _examples fonts)
endif()
foreach(example ${_examples})
    add_executable(imgui_rich_md_${example} ${CMAKE_CURRENT_LIST_DIR}/${example}/main.cpp)
    target_include_directories(imgui_rich_md_${example} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    target_compile_options(imgui_rich_md_${example} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)  # UTF-8 sources
    target_link_libraries(imgui_rich_md_${example} PRIVATE imgui_rich_md glfw OpenGL::GL)
endforeach()
if(_fonts_ok)
    target_include_directories(imgui_rich_md_fonts PRIVATE ${_fonts_dir}/include)
    target_compile_definitions(imgui_rich_md_fonts PRIVATE FONTS_EXAMPLE_ASSETS="${_fonts_dir}")
endif()
