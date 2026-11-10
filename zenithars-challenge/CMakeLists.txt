cmake_minimum_required(VERSION 3.22)

project(
	ZenitharsChallenge
	VERSION 0.1.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
	message(FATAL_ERROR "in-source builds are not allowed")
endif()

# CommonLibSSE-NG (git submodule, pinned to a release tag so CI can use the prebuilt bundle).
set(COMMONLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/commonlibsse-ng")
if(NOT EXISTS "${COMMONLIB_DIR}/CMakeLists.txt")
	message(FATAL_ERROR "CommonLibSSE-NG submodule missing. Run: git submodule update --init --recursive")
endif()
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(REX_OPTION_INI ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "" FORCE)
add_subdirectory("${COMMONLIB_DIR}" CommonLibSSE EXCLUDE_FROM_ALL)
if(NOT COMMAND add_commonlibsse_plugin)
	include("${COMMONLIB_DIR}/cmake/CommonLibSSE.cmake")
endif()

set(SOURCES
	src/main.cpp
	src/Choice.cpp
	src/Events.cpp
	src/InventoryMarks.cpp
	src/Rules.cpp
	src/RunFile.cpp
	src/Ledger.cpp
	src/Settings.cpp
	src/Stats.cpp
)
set(HEADERS
	src/PCH.h
	src/Choice.h
	src/Events.h
	src/InventoryMarks.h
	src/Rules.h
	src/RunFile.h
	src/Sha256.h
	src/Ledger.h
	src/Settings.h
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES} ${HEADERS})

add_commonlibsse_plugin(
	${PROJECT_NAME}
	NAME "Zenithars Challenge"
	AUTHOR "Dave Hughes"
	VERSION ${PROJECT_VERSION}
	USE_ADDRESS_LIBRARY
	MINIMUM_SKSE_VERSION 2.2.3
	SOURCES ${SOURCES} ${HEADERS}
)

target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

if(MSVC)
	target_compile_options(
		${PROJECT_NAME}
		PRIVATE
			/W4
			/Zi
			/permissive-
			/Zc:preprocessor
			/utf-8
			/wd4100 # unreferenced formal parameter
	)
	target_link_options(
		${PROJECT_NAME}
		PRIVATE
			"$<$<CONFIG:Release>:/DEBUG:FULL;/OPT:REF;/OPT:ICF>"
	)
endif()

# Stage a ready-to-zip mod folder: <build>/package/SKSE/Plugins/{dll,ini}
set(PACKAGE_DIR "${CMAKE_BINARY_DIR}/package")
add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}/SKSE/Plugins"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${PACKAGE_DIR}/SKSE/Plugins/"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${PACKAGE_DIR}/SKSE/Plugins/"
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist" "${PACKAGE_DIR}"
	COMMENT "Staging ${PROJECT_NAME} package"
)
