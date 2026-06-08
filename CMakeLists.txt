cmake_minimum_required(VERSION 3.21)

project(
    SkyrimCombatLogger
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    CommonLibSSE
    GIT_REPOSITORY https://github.com/CharmedBaryon/CommonLibSSE-NG.git
    GIT_TAG        main
)

set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(CommonLibSSE)

find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h" REQUIRED)

add_library(${PROJECT_NAME} SHARED src/Plugin.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE
    CommonLibSSE::CommonLibSSE
)

target_include_directories(${PROJECT_NAME} PRIVATE ${SIMPLEINI_INCLUDE_DIRS})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
)

target_compile_options(${PROJECT_NAME} PRIVATE
    /W4
    /WX-
    /permissive-
    /Zc:preprocessor
    /utf-8
)

install(
    FILES "$<TARGET_FILE:${PROJECT_NAME}>"
    DESTINATION "SKSE/Plugins"
)

install(
    FILES "SkyrimCombatLogger.ini"
    DESTINATION "SKSE/Plugins"
)
