cmake_minimum_required(VERSION 2.8)

set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake" ${CMAKE_MODULE_PATH})

add_subdirectory("td/")
include_directories ("td/")
include_directories ("td/td/generate/")
include_directories ("td/td/generate/default_td_api/")

#find_package (libevent)
find_package(LibConfig)

if (LIBCONFIG_FOUND)
  add_definitions("-DHAVE_LIBCONFIG=1")
  include_directories(${LIBCONFIG_INCLUDE_DIR})
endif (LIBCONFIG_FOUND)

option (USE_LIBEDIT "use libedit instead of libreadline" OFF)
if (USE_LIBEDIT) 
  set (EDIT_LIBS tdedit -lncursesw -lbsd )
else (USE_LIBEDIT)
  find_package(Readline REQUIRED)
  if (READLINE_FOUND)
    add_definitions("-DREADLINE_GNU=1")
    include_directories(${READLINE_INCLUDE_DIR})
  endif (READLINE_FOUND)
  set (EDIT_LIBS ${READLINE_LIBRARY})
endif (USE_LIBEDIT)

find_package(LibEvent2)
if (LIBEVENT2_FOUND)
  add_definitions("-DEVENT_V2=1")
  include_directories(${LIBEVENT2_INCLUDE_DIR})
else (LIBEVENT2_FOUND)
  find_package(LibEvent1 REQUIRED)
  add_definitions("-DEVENT_V1=1")
  include_directories(${LIBEVENT1_INCLUDE_DIR})
endif (LIBEVENT2_FOUND)

option (USE_JSON "allow json serialization. Needs libjansson" ON)
if (USE_JSON) 
  find_package(LibJansson REQUIRED)
  if (LIBJANSSON_FOUND)
    add_definitions("-DUSE_JSON=1")
    include_directories(${LIBJANSSON_INCLUDE_DIR})
  endif (LIBJANSSON_FOUND)
endif (USE_JSON)

option (USE_LUA "allows lua callbacks. Needs liblua" ON)
if (USE_LUA) 
  find_package(Lua REQUIRED)
  if (LUA_FOUND)
    add_definitions("-DUSE_LUA=1")
    include_directories(${LUA_INCLUDE_DIR})
  endif (LUA_FOUND)
endif (USE_LUA)

include (CheckIncludeFiles)
check_include_files (execinfo.h HAVE_EXECINFO_H)
if (HAVE_EXECINFO_H) 
  add_definitions("-DHAVE_EXECINFO_H=1")
endif (HAVE_EXECINFO_H)

set (CMAKE_C_FLAGS "-fno-strict-aliasing -fwrapv -Wall -Wextra -W -Wno-unused-parameter -Wno-deprecated-declarations -Wconversion -Wno-sign-conversion -Wno-sign-compare -fno-omit-frame-pointer -Werror=switch-enum")
set (CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wconversion -Wno-sign-conversion -std=c++14 -fno-omit-frame-pointer")
set (CMAKE_C_FLAGS_RELEASE "-O3")
set (CMAKE_CXX_FLAGS_RELEASE "-O3")

set (TDCLI_SOURCE
  main.c
  interface.c
  loop.c
  lua-tg.c
  json-tg.c
)

set (LIBTDEDIT_SOURCE
  libedit/chared.c 
  libedit/common.c 
  libedit/el.c 
  libedit/eln.c 
  libedit/emacs.c 
  libedit/hist.c 
  libedit/keymacro.c 
  libedit/map.c 
  libedit/chartype.c 
  libedit/parse.c 
	libedit/prompt.c 
  libedit/read.c 
  libedit/refresh.c 
  libedit/search.c 
  libedit/sig.c 
  libedit/terminal.c 
  libedit/tty.c 
  libedit/vi.c 
  libedit/wcsdup.c 
	libedit/tokenizer.c 
  libedit/tokenizern.c 
	libedit/history.c 
  libedit/historyn.c 
	libedit/filecomplete.c 
  libedit/readline.c 
)

set (TDCLI_CURSES_SOURCE
  telegram-curses.cpp
  td-window.cpp
  td-curses-utils.cpp
  td-window-input.cpp
  td-window-root.cpp
  td-window-log.cpp
  td-window-dialogs.cpp
  td-line-edit.cpp
  td-window-chat.cpp
)


add_library (tdedit STATIC ${LIBTDEDIT_SOURCE})
add_executable (telegram-cli ${TDCLI_SOURCE})
#add_executable (telegram-curses ${TDCLI_CURSES_SOURCE})
target_link_libraries (telegram-cli tdc tdclient ${OPENSSL_LIBRARIES} ${ZLIB_LIBRARIES} ${EDIT_LIBS} ${LIBCONFIG_LIBRARY} ${LIBEVENT2_LIBRARY} ${LIBEVENT1_LIBRARY} ${LIBJANSSON_LIBRARY} ${LUA_LIBRARIES} -lpthread )
#target_link_libraries (telegram-curses tdc tdclient ${OPENSSL_LIBRARIES}
#  ${ZLIB_LIBRARIES} ${LIBCONFIG_LIBRARY} ${LIBEVENT2_LIBRARY}
#  ${LIBEVENT1_LIBRARY} ${LIBJANSSON_LIBRARY} ${LUA_LIBRARIES} -lpthread
#  -lpanel -lncursesw -ltermkey)
install (TARGETS telegram-cli
    RUNTIME DESTINATION bin)
