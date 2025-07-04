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

add_library(library-profiling-solomon)


target_compile_options(library-profiling-solomon PRIVATE
  -Wdeprecated-this-capture
  -Wimplicit-fallthrough
  -Wparentheses
  -Wno-logical-op-parentheses
  -Wno-bitwise-op-parentheses
  -DNO_CUSTOM_CHAR_PTR_STD_COMPARATOR
)

target_link_libraries(library-profiling-solomon PUBLIC
  linux-headers-generic::linux-headers-generic
  contrib-libs-cxxsupp
  yutil
  yt-library-profiling
  yt-yt-core
  yt-core-http
  library-cpp-cgiparam
  cpp-monlib-metrics
  monlib-encode-prometheus
  monlib-encode-spack
  monlib-encode-json
  cpp-yt-threading
  protobuf::libprotobuf
  protobuf::libprotoc
)

target_sources(library-profiling-solomon PRIVATE
  ${PROJECT_SOURCE_DIR}/yt/yt/library/profiling/solomon/config.cpp
  ${PROJECT_SOURCE_DIR}/yt/yt/library/profiling/solomon/cube.cpp
  ${PROJECT_SOURCE_DIR}/yt/yt/library/profiling/solomon/exporter.cpp
  ${PROJECT_SOURCE_DIR}/yt/yt/library/profiling/solomon/helpers.cpp
  ${PROJECT_SOURCE_DIR}/yt/yt/library/profiling/solomon/producer.cpp
  ${PROJECT_SOURCE_DIR}/yt/yt/library/profiling/solomon/proxy.cpp
  ${PROJECT_SOURCE_DIR}/yt/yt/library/profiling/solomon/remote.cpp
  ${PROJECT_SOURCE_DIR}/yt/yt/library/profiling/solomon/sensor_service.cpp
  ${PROJECT_SOURCE_DIR}/yt/yt/library/profiling/solomon/sensor_set.cpp
  ${PROJECT_SOURCE_DIR}/yt/yt/library/profiling/solomon/sensor.cpp
  ${PROJECT_SOURCE_DIR}/yt/yt/library/profiling/solomon/tag_registry.cpp
  ${PROJECT_BINARY_DIR}/yt/yt/library/profiling/solomon/sensor_dump.pb.h
)

target_proto_messages(library-profiling-solomon PRIVATE
  ${PROJECT_SOURCE_DIR}/yt/yt/library/profiling/solomon/sensor_dump.proto
)


target_proto_outs(library-profiling-solomon
  --cpp_out=${PROJECT_BINARY_DIR}/
)

target_proto_addincls(library-profiling-solomon
  ./
  ${PROJECT_SOURCE_DIR}/
  ${PROJECT_BINARY_DIR}
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/yt
  ${PROJECT_BINARY_DIR}
  ${protobuf_INCLUDE_DIRS}
)

add_global_library_for(library-profiling-solomon.global
  library-profiling-solomon)


target_compile_options(library-profiling-solomon.global PRIVATE
  -Wdeprecated-this-capture
  -Wimplicit-fallthrough
  -Wparentheses
  -Wno-logical-op-parentheses
  -Wno-bitwise-op-parentheses
  -DNO_CUSTOM_CHAR_PTR_STD_COMPARATOR
)

target_link_libraries(library-profiling-solomon.global PUBLIC
  linux-headers-generic::linux-headers-generic
  contrib-libs-cxxsupp
  yutil
  yt-library-profiling
  yt-yt-core
  yt-core-http
  library-cpp-cgiparam
  cpp-monlib-metrics
  monlib-encode-prometheus
  monlib-encode-spack
  monlib-encode-json
  cpp-yt-threading
  protobuf::libprotobuf
  protobuf::libprotoc
)

target_sources(library-profiling-solomon.global PRIVATE
  ${PROJECT_SOURCE_DIR}/yt/yt/library/profiling/solomon/registry.cpp
)

