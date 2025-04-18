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

add_executable(simple_map_protobuf)


target_compile_options(simple_map_protobuf PRIVATE
  -Wdeprecated-this-capture
  -Wimplicit-fallthrough
  -Wparentheses
  -Wno-logical-op-parentheses
  -Wno-bitwise-op-parentheses
  -DNO_CUSTOM_CHAR_PTR_STD_COMPARATOR
)

target_link_libraries(simple_map_protobuf PUBLIC
  linux-headers-generic::linux-headers-generic
  contrib-libs-cxxsupp
  yutil
  library-cpp-cpuid_check
  cpp-mapreduce-client
  protobuf::libprotobuf
  protobuf::libprotoc
)

target_allocator(simple_map_protobuf
  cpp-malloc-tcmalloc
  libs-tcmalloc-no_percpu_cache
)

target_link_options(simple_map_protobuf PRIVATE
  -ldl
  -lrt
  -Wl,--no-as-needed
  -fPIC
  -fPIC
  -lrt
  -ldl
  -lutil
)

target_sources(simple_map_protobuf PRIVATE
  ${PROJECT_SOURCE_DIR}/yt/cpp/mapreduce/examples/tutorial/simple_map_protobuf/main.cpp
  ${PROJECT_BINARY_DIR}/yt/cpp/mapreduce/examples/tutorial/simple_map_protobuf/data.pb.h
)

target_proto_messages(simple_map_protobuf PRIVATE
  ${PROJECT_SOURCE_DIR}/yt/cpp/mapreduce/examples/tutorial/simple_map_protobuf/data.proto
)


target_proto_outs(simple_map_protobuf
  --cpp_out=${PROJECT_BINARY_DIR}/
)

target_proto_addincls(simple_map_protobuf
  ./
  ${PROJECT_SOURCE_DIR}/
  ${PROJECT_BINARY_DIR}
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/yt
  ${PROJECT_BINARY_DIR}
  ${protobuf_INCLUDE_DIRS}
)

vcs_info(simple_map_protobuf)
