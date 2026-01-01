project(bit-utility)

# 设置 CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称及语言设置
project(MyStaticLibProject VERSION 1.0 LANGUAGES CXX)

# 设置 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置输出目录（静态库输出到 lib，可执行文件输出到 bin）
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# 指定头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# # 收集 src 目录下的所有 .cpp 文件
# file(GLOB SRC_FILES "src/*.cpp")

# # 添加静态库目标
# add_library(simple_timer STATIC ${SRC_FILES})

# # 设置静态库的输出路径
# set_target_properties(simple_timer PROPERTIES
#     ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}
# )

# 处理 test 目录下的每个 .cpp 文件
file(GLOB TEST_SOURCES "test/*.cpp")

foreach(TEST_FILE ${TEST_SOURCES})
    # 提取测试文件名（不带路径和扩展名）
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)

    # 为每个测试文件生成可执行文件
    add_executable(${TEST_NAME} ${TEST_FILE})

    # # 链接静态库
    # target_link_libraries(${TEST_NAME} PRIVATE simple_timer)

    # 设置可执行文件输出路径
    set_target_properties(${TEST_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    )
endforeach()