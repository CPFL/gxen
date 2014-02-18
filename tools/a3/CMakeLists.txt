cmake_minimum_required(VERSION 2.8)

project(a3)

if(COMMAND cmake_policy)
    cmake_policy(SET CMP0003 NEW)  # don't split absolute link paths
    cmake_policy(SET CMP0012 NEW)  # recognize number & boolean literals
    cmake_policy(SET CMP0015 NEW)  # convert relative link paths to absolute
endif(COMMAND cmake_policy)

add_definitions(
    # for backward
    "-DBACKWARD_HAS_DW=1"
    "-DBACKWARD_HAS_BFD=1"
    )
add_subdirectory(third_party/backward)

include_directories(
    .
    /usr/local/include
    /opt/local/include
    )

# Include third_party libraries
include_directories(
    ./third_party/backward/
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
     "-g3"
    "-rdynamic"
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
    add_definitions("-Werror")
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
    band_scheduler.cc
    bar1_channel.cc
    bar3_channel.cc
    barrier.cc
    channel.cc
    chipset.cc
    context_bar0.cc
    context_bar1.cc
    context_bar3.cc
    context_bar4.cc
    context_barrier.cc
    context.cc
    context_sched.cc
    credit_scheduler.cc
    device_bar1.cc
    device_bar3.cc
    device.cc
    device_table.cc
    direct_scheduler.cc
    fifo_scheduler.cc
    flags.cc
    instruments.cc
    main.cc
    page.cc
    pfifo.cc
    playlist.cc
    pmem.cc
    poll_area.cc
    registers.cc
    sampler.cc
    scheduler.cc
    session.cc
    shadow_page_table.cc
    software_page_table.cc
    utility.cc
    vram.cc
    xen.c
    )

target_link_libraries(a3
    # backward dependencies
    backward
    dw
    bfd
    dl

    # a3 dependencies
    pciaccess
    rt
    boost_system
    boost_thread
    boost_date_time
    pthread
    xenlight
    xenctrl
    )
