# This file was generated by the YaTool build system (https://github.com/yandex/yatool),
# from a source YaTool build configuration provided in ya.make files.
#
# If the repository supports both CMake and ya build configurations, please modify both of them.
#
# If only CMake build configuration is supported then modify only CMake files and note that only
# simple modifications are allowed like adding source-files to targets or adding simple properties
# like target_include_directories. These modifications will be ported to original ya.make files
# by maintainers. Any complex modifications which can't be easily ported back to the ya build
# system may be rejected.
#
# Please refer to the build instructions in the repository for more information about manual
# changes in this file.

find_package(linux-headers-generic REQUIRED)

add_library(mapreduce-library-user_job_statistics)


target_compile_options(mapreduce-library-user_job_statistics PRIVATE
  -DNO_CUSTOM_CHAR_PTR_STD_COMPARATOR
)

target_link_libraries(mapreduce-library-user_job_statistics PUBLIC
  linux-headers-generic::linux-headers-generic
  contrib-libs-cxxsupp
  yutil
  cpp-mapreduce-common
)

target_sources(mapreduce-library-user_job_statistics PRIVATE
  ${PROJECT_SOURCE_DIR}/yt/cpp/mapreduce/library/user_job_statistics/user_job_statistics.cpp
)

