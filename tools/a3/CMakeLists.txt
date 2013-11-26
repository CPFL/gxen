cmake_minimum_required(VERSION 2.8)

project(a3)

if(COMMAND cmake_policy)
    cmake_policy(SET CMP0003 NEW)  # don't split absolute link paths
    cmake_policy(SET CMP0012 NEW)  # recognize number & boolean literals
    cmake_policy(SET CMP0015 NEW)  # convert relative link paths to absolute
endif(COMMAND cmake_policy)

include_directories(
    .
    /usr/local/include
    /opt/local/include
    )

macro(include_if_exists directory)
    if(EXISTS "${directory}/")
        include_directories(${directory})
    endif()
endmacro()

macro(link_directories_if_exists directory)
    if(EXISTS "${directory}/")
        link_directories(${directory})
    endif()
endmacro()

# include local directories
include_if_exists(/usr/local/include)
include_if_exists(/opt/local/include)

add_definitions("-pipe")
add_definitions(
    "-Wall"
    "-Wextra"
    "-Wno-unused-parameter"
    "-Wwrite-strings"
    "-Wreturn-type"
    "-Wpointer-arith"
    "-Wno-unused-variable"
    "-Wwrite-strings"
    "-Wno-long-long"
    "-Wno-missing-field-initializers"
    )
add_definitions(
    "-D__STDC_LIMIT_MACROS"
    "-D__STDC_CONSTANT_MACROS"
    "-D__STDC_FORMAT_MACROS"
    )

if (CMAKE_BUILD_TYPE MATCHES Release)
    add_definitions("-DNDEBUG")
endif()

if (CMAKE_BUILD_TYPE MATCHES Debug)
    add_definitions("-Werror" "-g3")
endif()

set(CMAKE_C_FLAGS_RELEASE "")
set(CMAKE_CXX_FLAGS_RELEASE "")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -flto")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -flto")

# using GCC
execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion
                OUTPUT_VARIABLE GCC_VERSION)
if (GCC_VERSION VERSION_GREATER 4.7 OR GCC_VERSION VERSION_EQUAL 4.7)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
endif()

add_executable(a3
    a3_band_scheduler.cc
    a3_bar1_channel.cc
    a3_bar3_channel.cc
    a3_barrier.cc
    a3_channel.cc
    a3_context_bar0.cc
    a3_context_bar1.cc
    a3_context_bar3.cc
    a3_context_bar4.cc
    a3_context_barrier.cc
    a3_context.cc
    a3_context_sched.cc
    a3_credit_scheduler.cc
    a3_device_bar1.cc
    a3_device_bar3.cc
    a3_device.cc
    a3_device_table.cc
    a3_direct_scheduler.cc
    a3_fifo_scheduler.cc
    a3_flags.cc
    a3_instruments.cc
    a3_main.cc
    a3_page.cc
    a3_playlist.cc
    a3_pmem.cc
    a3_poll_area.cc
    a3_registers.cc
    a3_scheduler.cc
    a3_session.cc
    a3_shadow_page_table.cc
    a3_software_page_table.cc
    a3_vram.cc
    a3_xen.c
    )

target_link_libraries(a3
    pciaccess
    rt
    boost_system
    boost_thread
    boost_date_time
    pthread
    xenlight
    xenctrl
    )
