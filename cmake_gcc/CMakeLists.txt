# Define minimal required version of CMake.
cmake_minimum_required(VERSION "3.25")

# Project definition
project(
	IMU_Pen
	VERSION 1.0
	LANGUAGES C CXX ASM
)

# Include the definition of the slc_IMU_Pen target,
# which contains the content of the SLC project
include(IMU_Pen.cmake)

add_executable(IMU_Pen
    # Add additional sources here
)

target_include_directories(IMU_Pen PUBLIC
    # Add additional include paths here
)

target_compile_definitions(IMU_Pen PUBLIC
    # Add additional macros here
)

target_compile_options(IMU_Pen PUBLIC
    # Set additional compiler flags here
)

target_link_options(IMU_Pen PUBLIC
    # Set additional linker flags here
)

# Link with the content defined in the SLC project
target_link_libraries(IMU_Pen PRIVATE
    slc
)

# Include managed project content if available
include(IMU_Pen_project.cmake OPTIONAL RESULT_VARIABLE managed_project)
if(managed_project)
    message(STATUS "Using managed project content from ${managed_project}")
endif()

# Force the gcc linker command
set_target_properties(IMU_Pen PROPERTIES LINKER_LANGUAGE C)

# Create .bin, .hex and .s37 artifacts after building the project
add_custom_command(TARGET IMU_Pen
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_SREC_CMD} "$<TARGET_FILE:IMU_Pen>" "$<TARGET_FILE_DIR:IMU_Pen>/$<TARGET_FILE_BASE_NAME:IMU_Pen>.s37"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_IHEX_CMD} "$<TARGET_FILE:IMU_Pen>" "$<TARGET_FILE_DIR:IMU_Pen>/$<TARGET_FILE_BASE_NAME:IMU_Pen>.hex"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_BIN_CMD}  "$<TARGET_FILE:IMU_Pen>" "$<TARGET_FILE_DIR:IMU_Pen>/$<TARGET_FILE_BASE_NAME:IMU_Pen>.bin" 
)

# Run post-build pipeline to perform additional post-processing
if(post_build_command)
add_custom_command(TARGET IMU_Pen
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
)
endif()