project(emclient)
cmake_minimum_required(VERSION 3.13)

set(TARGET_NAME wasm-client)

if(EMSCRIPTEN)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

add_library(emclient STATIC)

target_sources(emclient
    PRIVATE
        emclient.cpp
)

if(EMSCRIPTEN)
    target_compile_definitions(emclient PUBLIC EMSCRIPTEN)
    target_link_libraries(emclient wallet_client)

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-value")

    add_executable(${TARGET_NAME} emclient.cpp)
    target_link_libraries(${TARGET_NAME} PRIVATE emclient)

    add_subdirectory(${PROJECT_SOURCE_DIR}/../3rdparty/secp256k1-zkp secp256k1-zkp)
    add_subdirectory(${PROJECT_SOURCE_DIR}/../wallet/client wallet_client)
    target_include_directories(wallet_client PUBLIC 
        "${PROJECT_SOURCE_DIR}/.." 
        "${PROJECT_SOURCE_DIR}/../3rdparty" 
        "${PROJECT_SOURCE_DIR}/../3rdparty/libuv/include")
        # $ENV{BOOST_ROOT})

    set_target_properties(${TARGET_NAME} PROPERTIES LINK_FLAGS "--bind -s WASM=1 -s ERROR_ON_UNDEFINED_SYMBOLS=0 -s MODULARIZE=1 -s USE_BOOST_HEADERS=1")
else()
    target_link_libraries(emclient PRIVATE wallet_client)
endif()
