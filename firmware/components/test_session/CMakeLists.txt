idf_component_register(SRCS "test_session.c" INCLUDE_DIRS "include" REQUIRES control)

# No Node runtime is needed in the firmware toolchain. Regenerate on the host.
include("${CMAKE_CURRENT_LIST_DIR}/workouts.check.cmake")
add_custom_target(workout_catalog_check ALL
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_LIST_DIR}/workouts.check.cmake")
add_dependencies(${COMPONENT_LIB} workout_catalog_check)
