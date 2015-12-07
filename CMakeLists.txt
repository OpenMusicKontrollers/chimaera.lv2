cmake_minimum_required(VERSION 2.8)

project(chimaera.lv2)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/libosc)
include_directories(${PROJECT_SOURCE_DIR}/inlist)
include_directories(${PROJECT_SOURCE_DIR}/eo_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/tlsf-3.0)
include_directories(${PROJECT_SOURCE_DIR}/cjson)
include_directories(${PROJECT_SOURCE_DIR}/varchunk)

set(CMAKE_C_FLAGS "-std=gnu11 -Wextra -Wno-unused-parameter -ffast-math -fvisibility=hidden ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wshadow -Wimplicit-function-declaration -Wmissing-prototypes -Wstrict-prototypes ${CMAKE_C_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-z,nodelete ${CMAKE_MODULE_LINKER_FLAGS}")
add_definitions("-D_GNU_SOURCE=1") # asprintf

set(CHIMAERA_MAJOR_VERSION 0)
set(CHIMAERA_MINOR_VERSION 1)
set(CHIMAERA_MICRO_VERSION 1)
set(CHIMAERA_VERSION "${}.${}.${}")

set(DEST lib/lv2/chimaera.lv2)
if(WIN32)
	set(LIB_EXT ".dll")
elseif(APPLE)
	set(LIB_EXT ".so")
else()
	set(LIB_EXT ".so")
endif()

find_package(PkgConfig) # ${PKG_CONFIG_FOUND}

pkg_search_module(LV2 REQUIRED lv2>=1.10)
include_directories(${LV2_INCLUDE_DIRS})
set(LIBS ${LIBS} ${LV2_LDFLAGS})

pkg_search_module(UV REQUIRED libuv>=1.0)
include_directories(${UV_INCLUDE_DIRS})
set(LIBS ${LIBS} ${UV_LDFLAGS})

pkg_search_module(LIBBSD REQUIRED libbsd)
include_directories(${LIBBSD_INCLUDE_DIRS})
set(LIBS ${LIBS} ${LIBBSD_LDFLAGS})

option(CHIMAERA_UI_PLUGINS "Build Chimaera UI plugins" ON)

include(CheckCSourceCompiles)
CHECK_C_SOURCE_COMPILES("int main(int argc, char **argv)
	{void *src; void *dst = __builtin_assume_aligned(src, 16); return 0;}"
	HAS_BUILTIN_ASSUME_ALIGNED)
if(${HAS_BUILTIN_ASSUME_ALIGNED})
	add_definitions("-DHAS_BUILTIN_ASSUME_ALIGNED")
endif()

add_library(chimaera MODULE
	tlsf-3.0/tlsf.c

	chimaera.c
	filter.c
	mapper.c
	control_out.c
	midi_out.c
	mpe_out.c
	supercollider_out.c
	simulator.c
	visualizer.c
	driver.c
	mogrifier.c)
target_link_libraries(chimaera ${LIBS})
set_target_properties(chimaera PROPERTIES PREFIX "")
install(TARGETS chimaera DESTINATION ${DEST})

if(CHIMAERA_UI_PLUGINS)
	pkg_search_module(ELM REQUIRED elementary>=1.8)
	include_directories(${ELM_INCLUDE_DIRS})
	set(LIBS_UI ${LIBS_UI} ${ELM_LDFLAGS})
	
	pkg_search_module(ECORE_X OPTIONAL ecore-x)

	if(${ECORE_X_FOUND} AND ((${ELM_VERSION} VERSION_EQUAL "1.13.0") OR (${ELM_VERSION} VERSION_GREATER "1.13.0")))
		set(X11_UI_WRAP "")
		add_definitions("-DX11_UI_WRAP")
	else()
		set(X11_UI_WRAP "#")
	endif()

	set(EO_UI_WRAP "")
	set(UI_UI_WRAP "")
	set(KX_UI_WRAP "")

	add_library(chimaera_ui MODULE
		chimaera_ui.c
		simulator_ui.c
		visualizer_ui.c)
	target_compile_definitions(chimaera_ui PUBLIC -DCHIMAERA_UI_PLUGINS)
	target_link_libraries(chimaera_ui ${LIBS_UI})
	set_target_properties(chimaera_ui PROPERTIES PREFIX "")
	install(TARGETS chimaera_ui DESTINATION ${DEST})

	find_program(EDJE_CC NAMES edje_cc)
	if(EDJE_CC_NOTFOUND)
		message(SEND_ERROR "edje_cc not found")
	else(EDJE_CC_NOTFOUND)
		message(STATUS "edje_cc found: " ${EDJE_CC})
	endif(EDJE_CC_NOTFOUND)

	add_custom_command(
		OUTPUT ${PROJECT_BINARY_DIR}/chimaera_ui.edj
		COMMAND ${EDJE_CC} ARGS
			"-fd" ${PROJECT_SOURCE_DIR}/font
			"-id" ${PROJECT_SOURCE_DIR}/pix
			${PROJECT_SOURCE_DIR}/chimaera_ui.edc
			${PROJECT_BINARY_DIR}/chimaera_ui.edj
		DEPENDS
			${PROJECT_SOURCE_DIR}/chimaera_ui.edc
			${PROJECT_SOURCE_DIR}/simulator_ui.edc
			${PROJECT_SOURCE_DIR}/visualizer_ui.edc
			${PROJECT_SOURCE_DIR}/pix/48.png
			${PROJECT_SOURCE_DIR}/pix/64.png
			${PROJECT_SOURCE_DIR}/pix/80.png
			${PROJECT_SOURCE_DIR}/pix/96.png
			${PROJECT_SOURCE_DIR}/pix/112.png
			${PROJECT_SOURCE_DIR}/pix/128.png
			${PROJECT_SOURCE_DIR}/pix/144.png
			${PROJECT_SOURCE_DIR}/pix/160.png)
	add_custom_target(THEME ALL DEPENDS ${PROJECT_BINARY_DIR}/chimaera_ui.edj)
	install(FILES ${PROJECT_BINARY_DIR}/chimaera_ui.edj DESTINATION ${DEST})
else()
	set(EO_UI_WRAP "#")
	set(UI_UI_WRAP "#")
	set(KX_UI_WRAP "#")
	set(X11_UI_WRAP "#")
endif()

configure_file(${PROJECT_SOURCE_DIR}/manifest.ttl.in
	${PROJECT_BINARY_DIR}/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/manifest.ttl DESTINATION ${DEST})
install(FILES ${PROJECT_SOURCE_DIR}/chimaera.ttl DESTINATION ${DEST})
