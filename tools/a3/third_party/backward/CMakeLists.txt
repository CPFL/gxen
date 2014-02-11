add_definitions(
    "-Wno-unused-function"
    "-Wno-sign-compare"
    "-Wno-uninitialized"
    )

add_library(backward STATIC backward.cpp)
