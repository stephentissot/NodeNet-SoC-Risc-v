# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.* ${CMAKE_SOURCE_DIR}/../plcLink/*.cpp)

idf_component_register(SRCS ${app_sources}
					   INCLUDE_DIRS "../include" "../../plcLink")

if(NOT CMAKE_SCRIPT_MODE_FILE)
	FILE(GLOB_RECURSE web_asset_sources
		 ${CMAKE_SOURCE_DIR}/www/app/*
		 ${CMAKE_SOURCE_DIR}/www/startup/*
		 ${CMAKE_SOURCE_DIR}/www/package.json
		 ${CMAKE_SOURCE_DIR}/www/build.mjs)

	find_program(NPM_EXECUTABLE npm REQUIRED)

	set(WEB_ROOT_DIR ${CMAKE_SOURCE_DIR}/www)
	set(WEB_BUILD_DIR ${CMAKE_BINARY_DIR}/www_image)
	set(WEB_INSTALL_STAMP ${CMAKE_BINARY_DIR}/www_npm_install.stamp)
	set(WEB_BUILD_STAMP ${WEB_BUILD_DIR}/.build_stamp)

	add_custom_command(
		OUTPUT ${WEB_INSTALL_STAMP}
		COMMAND ${NPM_EXECUTABLE} install
		COMMAND ${CMAKE_COMMAND} -E touch ${WEB_INSTALL_STAMP}
		WORKING_DIRECTORY ${WEB_ROOT_DIR}
		DEPENDS ${WEB_ROOT_DIR}/package.json
		COMMENT "Installing ESP32 web UI dependencies"
	)

	add_custom_command(
		OUTPUT ${WEB_BUILD_STAMP}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${WEB_BUILD_DIR}
		COMMAND ${NPM_EXECUTABLE} run build -- ${WEB_BUILD_DIR}
		COMMAND ${CMAKE_COMMAND} -E touch ${WEB_BUILD_STAMP}
		WORKING_DIRECTORY ${WEB_ROOT_DIR}
		DEPENDS ${WEB_INSTALL_STAMP} ${web_asset_sources}
		COMMENT "Building ESP32 web assets"
	)

	add_custom_target(web_assets ALL DEPENDS ${WEB_BUILD_STAMP})
	add_dependencies(${COMPONENT_LIB} web_assets)
	littlefs_create_partition_image(www ${WEB_BUILD_DIR} FLASH_IN_PROJECT DEPENDS web_assets)
endif()
