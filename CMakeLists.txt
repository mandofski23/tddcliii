cmake_minimum_required(VERSION 2.8)

add_subdirectory("tdc")

set (CMAKE_C_FLAGS "-fno-strict-aliasing -fwrapv -Wall -Wextra -W -Wno-unused-parameter -Wno-deprecated-declarations -Wno-sign-compare -fno-omit-frame-pointer")
set (CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wconversion -Wno-sign-conversion -std=c++14 -DUSE_READLINE=1 -DTD_USE_GTEST=1 -fno-omit-frame-pointer")
set (CMAKE_C_FLAGS_RELEASE "-flto -O3")
set (CMAKE_CXX_FLAGS_RELEASE "-flto -O3")

set (TDCLI_SOURCE
  main.c
  interface.c
  loop.c
)

add_executable (tdcli ${TDCLI_SOURCE})
target_link_libraries (tdcli tdlibc ${OPENSSL_LIBRARIES} ${ZLIB_LIBRARIES} ${READLINE_LIBRARY} -lpthread -levent -lconfig)
