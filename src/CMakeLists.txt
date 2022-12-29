get_filename_component(replicate_proto "../proto/replicate.proto" ABSOLUTE)
get_filename_component(replicate_proto_path "${replicate_proto}" PATH)

get_filename_component(health_proto "../proto/health.proto" ABSOLUTE)
get_filename_component(health_proto_path "${health_proto}" PATH)

set(replicate_proto_sources "${CMAKE_CURRENT_BINARY_DIR}/replicate.pb.cc")
set(replicate_proto_headers "${CMAKE_CURRENT_BINARY_DIR}/replicate.pb.h")
set(replicate_grpc_sources "${CMAKE_CURRENT_BINARY_DIR}/replicate.grpc.pb.cc")
set(replicate_grpc_headers "${CMAKE_CURRENT_BINARY_DIR}/replicate.grpc.pb.h")

set(health_proto_sources "${CMAKE_CURRENT_BINARY_DIR}/health.pb.cc")
set(health_proto_headers "${CMAKE_CURRENT_BINARY_DIR}/health.pb.h")
set(health_grpc_sources "${CMAKE_CURRENT_BINARY_DIR}/health.grpc.pb.cc")
set(health_grpc_headers "${CMAKE_CURRENT_BINARY_DIR}/health.grpc.pb.h")

add_custom_command(
        OUTPUT "${replicate_proto_sources}" "${replicate_proto_headers}" "${replicate_grpc_sources}" "${replicate_grpc_headers}"
        COMMAND ${_PROTOBUF_PROTOC}
        ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}" --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${replicate_proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${replicate_proto}"
        DEPENDS "${replicate_proto}")

add_custom_command(
        OUTPUT "${health_proto_sources}" "${health_proto_headers}" "${health_grpc_sources}" "${health_grpc_headers}"
        COMMAND ${_PROTOBUF_PROTOC}
        ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}" --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${health_proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${health_proto}"
        DEPENDS "${health_proto}")

include_directories("${CMAKE_CURRENT_BINARY_DIR}")

add_library(replicate_grpc_proto
        ${replicate_proto_sources}
        ${replicate_proto_headers}
        ${replicate_grpc_sources}
        ${replicate_grpc_headers})

add_library(health_grpc_proto
        ${health_proto_sources}
        ${health_proto_headers}
        ${health_grpc_sources}
        ${health_grpc_headers})

target_link_libraries(replicate_grpc_proto
        ${_PROTOBUF_LIBPROTOBUF}
        ${_REFLECTION}
        ${_GRPC_GRPCPP})

target_link_libraries(health_grpc_proto
        ${_PROTOBUF_LIBPROTOBUF}
        ${_REFLECTION}
        ${_GRPC_GRPCPP})

foreach(_target main)
    add_executable(${_target} "${_target}.cpp")
    target_link_libraries(${_target}
            replicate_grpc_proto
            health_grpc_proto
            ${_PROTOBUF_LIBPROTOBUF}
            ${_REFLECTION}
            ${_GRPC_GRPCPP})
endforeach()
