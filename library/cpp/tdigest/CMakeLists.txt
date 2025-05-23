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

find_package(Protobuf REQUIRED)

add_library(library-cpp-tdigest)


target_compile_options(library-cpp-tdigest PRIVATE
  -DNO_CUSTOM_CHAR_PTR_STD_COMPARATOR
)

target_link_libraries(library-cpp-tdigest PUBLIC
  linux-headers-generic::linux-headers-generic
  contrib-libs-cxxsupp
  yutil
  cpp-protobuf-runtime
  protobuf::libprotobuf
  protobuf::libprotoc
)

target_sources(library-cpp-tdigest PRIVATE
  ${PROJECT_SOURCE_DIR}/library/cpp/tdigest/tdigest.cpp
  ${PROJECT_BINARY_DIR}/library/cpp/tdigest/tdigest.pb.h
)

target_proto_messages(library-cpp-tdigest PRIVATE
  ${PROJECT_SOURCE_DIR}/library/cpp/tdigest/tdigest.proto
)


target_proto_outs(library-cpp-tdigest
  --cpp_out=${PROJECT_BINARY_DIR}/
)

target_proto_addincls(library-cpp-tdigest
  ./
  ${PROJECT_SOURCE_DIR}/
  ${PROJECT_BINARY_DIR}
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_BINARY_DIR}
  ${protobuf_INCLUDE_DIRS}
)
