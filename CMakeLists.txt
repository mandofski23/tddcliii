cmake_minimum_required(VERSION 2.8)

set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake" ${CMAKE_MODULE_PATH})

add_subdirectory("tdc")

#find_package (libevent)
find_package(LibConfig)

if (LIBCONFIG_FOUND)
  add_definitions("-DHAVE_LIBCONFIG=1")
endif (LIBCONFIG_FOUND)

find_package(Readline REQUIRED)
if (READLINE_FOUND)
  add_definitions("-DREADLINE_GNU=1")
endif (READLINE_FOUND)

find_package(LibEvent2)
if (LIBEVENT2_FOUND)
  add_definitions("-DEVENT_V2=1")
else (LIBEVENT2_FOUND)
  find_package(LibEvent1 REQUIRED)
  add_definitions("-DEVENT_V1=1")
endif (LIBEVENT2_FOUND)

option (USE_JSON "allow json serialization. Needs libjansson" ON)
if (USE_JSON) 
  find_package(LibJansson REQUIRED)
  if (LIBJANSSON_FOUND)
    add_definitions("-DUSE_JSON=1")
  endif (LIBJANSSON_FOUND)
endif (USE_JSON)

include (CheckIncludeFiles)
check_include_files (execinfo.h HAVE_EXECINFO_H)
if (HAVE_EXECINFO_H) 
  add_definitions("-DHAVE_EXECINFO_H=1")
endif (HAVE_EXECINFO_H)

set (CMAKE_C_FLAGS "-fno-strict-aliasing -fwrapv -Wall -Wextra -W -Wno-unused-parameter -Wno-deprecated-declarations -Wconversion -Wno-sign-conversion -Wno-sign-compare -fno-omit-frame-pointer")
set (CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wconversion -Wno-sign-conversion -std=c++14 -fno-omit-frame-pointer")
set (CMAKE_C_FLAGS_RELEASE "-flto -O3")
set (CMAKE_CXX_FLAGS_RELEASE "-flto -O3")

set (TDCLI_SOURCE
  main.c
  interface.c
  loop.c
  lua-tg.c
  json-tg.c
  extension.c
)


add_executable (telegram-cli ${TDCLI_SOURCE})
target_link_libraries (telegram-cli tdlibc ${OPENSSL_LIBRARIES} ${ZLIB_LIBRARIES} ${READLINE_LIBRARY} ${LIBCONFIG_LIBRARY} ${LIBEVENT2_LIBRARY} ${LIBEVENT1_LIBRARY} ${LIBJANSSON_LIBRARY} -lpthread )
install (TARGETS telegram-cli
    RUNTIME DESTINATION bin)
