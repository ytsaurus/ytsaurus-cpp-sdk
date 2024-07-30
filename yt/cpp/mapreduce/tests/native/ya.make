RECURSE(
    misc
    misc_core_http
    operations
    operations_core_http
)

IF (NOT OPENSOURCE)
    RECURSE(
        check_initialized
        check_initialized/test_program
        crash_writer_on_exception
        crash_writer_on_exception/test_program
        error_exit
        error_exit/test_program
        job_on_exit_function
        parallel_cache_upload
        parallel_cache_upload/test_program
        portals
        proto_lib
        remote_copy
        remote_copy/recipe
        server_yt_name_conflicts
    )
ENDIF()
