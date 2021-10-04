cmake_minimum_required(VERSION 3.14)
project(omphalos VERSION 0.99.13
  DESCRIPTION "Network enumeration and domination"
  HOMEPAGE_URL "https://nick-black.com/dankwiki/index.php/Omphalos"
  LANGUAGES C)
set(CMAKE_C_STANDARD 11)

include(CTest)
include(GNUInstallDirs)
include(FeatureSummary)

###################### USER-SELECTABLE OPTIONS ###########################
option(USE_PANDOC "Use pandoc to write man pages" ON)
option(USE_NOTCURSES "Use Notcurses to build fullscreen UI" ON)
option(USE_READLINE "Use libreadline to build line-based UI" ON)
option(USE_SUDO "Use sudo to bless binaries" OFF)
#################### END USER-SELECTABLE OPTIONS #########################

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the build mode." FORCE)
endif()

add_compile_definitions(_FORTIFY_SOURCE=2)
add_compile_options(-Wall -Wextra -W -Wshadow -Wformat -fexceptions)

find_package(PkgConfig REQUIRED)
find_package(Threads)
set_package_properties(Threads PROPERTIES TYPE REQUIRED)
feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
pkg_check_modules(LIBCAP REQUIRED libcap>=2.24)
pkg_check_modules(LIBNL REQUIRED libnl-3.0>=3.4.0)
pkg_check_modules(LIBNLGENL REQUIRED libnl-genl-3.0>=3.4.0)
pkg_check_modules(LIBPCAP REQUIRED libpcap>=1.9.1)
pkg_check_modules(LIBPCIACCESS REQUIRED pciaccess>=0.13.1)
pkg_check_modules(LIBREADLINE REQUIRED readline)
pkg_check_modules(LIBUDEV REQUIRED libudev>=175)
pkg_check_modules(LIBZ REQUIRED zlib>=1.2.11)
if(${USE_NOTCURSES})
  find_package(Notcurses 2.4.4 CONFIG)
  set_package_properties(Notcurses PROPERTIES TYPE REQUIRED)
endif()

# don't cache these, or installing them requires clearing the cache to be found.
# this is going to be true for anything lacking pkg-config/CMake support.
unset(HAVE_UNISTRING_H CACHE)
check_include_file("iwlib.h" HAVE_LIBIW_H)
if(NOT "${HAVE_LIBIW_H}")
  message(FATAL_ERROR "Couldn't find iwlib.h")
endif()
set_property(GLOBAL APPEND PROPERTY PACKAGES_FOUND libiw)
set_package_properties(libiw PROPERTIES TYPE REQUIRED)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

include(CMakePackageConfigHelpers)
configure_file(src/version.h.in include/version.h)

set(COMMON_INCLUDE_DIRS
  "${LIBCAP_INCLUDE_DIRS}"
  "${LIBNL_INCLUDE_DIRS}"
  "${LIBNLGENL_INCLUDE_DIRS}"
  "${LIBPCAP_INCLUDE_DIRS}"
  "${LIBPCIACCESS_INCLUDE_DIRS}"
  "${LIBUDEV_INCLUDE_DIRS}"
  "${LIBZ_INCLUDE_DIRS}"
)

set(COMMON_LIBRARY_DIRS
  "${LIBCAP_LIBRARY_DIRS}"
  "${LIBIW_LIBRARY_DIRS}"
  "${LIBNL_LIBRARY_DIRS}"
  "${LIBNLGENL_LIBRARY_DIRS}"
  "${LIBPCAP_LIBRARY_DIRS}"
  "${LIBPCIACCESS_LIBRARY_DIRS}"
  "${LIBREADLINE_LIBRARY_DIRS}"
  "${LIBUDEV_LIBRARY_DIRS}"
  "${LIBZ_LIBRARY_DIRS}"
  "${Notcurses_LIBRARY_DIRS}"
)

set(COMMON_LIBRARIES
  "${LIBCAP_LIBRARIES}"
  "${LIBIW_LIBRARIES}"
  "${LIBNL_LIBRARIES}"
  "${LIBNLGENL_LIBRARIES}"
  "${LIBPCAP_LIBRARIES}"
  "${LIBPCIACCESS_LIBRARIES}"
  "${LIBUDEV_LIBRARIES}"
  "${LIBZ_LIBRARIES}"
  Threads::Threads
  -liw
)

file(GLOB SRCS CONFIGURE_DEPENDS src/omphalos/*.c)
file(GLOB NCSRCS CONFIGURE_DEPENDS src/ui/notcurses/*.c)
file(GLOB RLSRCS CONFIGURE_DEPENDS src/ui/tty/*.c)
file(GLOB CTSRCS CONFIGURE_DEPENDS src/ui/coretest/*.c)
file(GLOB ADDCAPS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tools/addcaps")
add_executable(omphalos ${SRCS} ${NCSRCS})
add_executable(omphalos-readline ${SRCS} ${RLSRCS})
add_executable(omphalos-coretest ${SRCS} ${CTSRCS})
target_include_directories(omphalos
  PRIVATE
    src
    "${PROJECT_BINARY_DIR}/include"
    "${COMMON_INCLUDE_DIRS}"
    "${Notcurses_INCLUDE_DIRS}"
)
target_link_libraries(omphalos
  PRIVATE
    ${COMMON_LIBRARIES}
    "${Notcurses_LIBRARIES}"
)
target_compile_definitions(omphalos
  PRIVATE
    _GNU_SOURCE _DEFAULT_SOURCE _XOPEN_SOURCE=600
)
target_include_directories(omphalos-readline
  PRIVATE
    src
    "${PROJECT_BINARY_DIR}/include"
    "${COMMON_INCLUDE_DIRS}"
    "${LIBREADLINE_INCLUDE_DIRS}"
)
target_link_libraries(omphalos-readline
  PRIVATE
    ${COMMON_LIBRARIES}
    ${LIBREADLINE_LIBRARIES}
)
target_compile_definitions(omphalos-readline
  PRIVATE
    _GNU_SOURCE _DEFAULT_SOURCE _XOPEN_SOURCE=600
)
target_include_directories(omphalos-coretest
  PRIVATE
    src
    "${PROJECT_BINARY_DIR}/include"
    "${COMMON_INCLUDE_DIRS}"
)
target_link_libraries(omphalos-coretest
  PRIVATE
    ${COMMON_LIBRARIES}
)
target_compile_definitions(omphalos-coretest
  PRIVATE
    _GNU_SOURCE _DEFAULT_SOURCE _XOPEN_SOURCE=600
)

if(${USE_SUDO})
foreach(o omphalos omphalos-coretest omphalos-readline)
  add_custom_command(TARGET ${o}
    POST_BUILD COMMAND ${ADDCAPS} ${o}
    COMMENT "Blessing ${o}"
  )
endforeach()
endif()

file(GLOB PCAPS CONFIGURE_DEPENDS test/*.pcap)
enable_testing()
foreach(p ${PCAPS})
get_filename_component(pe ${p} NAME_WLE)
add_test(
  NAME ${pe}
  COMMAND omphalos-readline --mode=silent -u "" -f ${p} --plog=plog.pcap --ouis=../ieee-oui.txt
)
endforeach()

add_custom_target(livetest
  omphalos -u '' --plog=plog.pcap --ouis=../ieee-oui.txt
)

install(
  TARGETS
    omphalos
    omphalos-readline
  DESTINATION
    bin
)
