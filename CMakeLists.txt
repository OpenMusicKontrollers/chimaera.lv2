cmake_minimum_required(VERSION 2.8)

project(chimaera.lv2)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/libosc)
include_directories(${PROJECT_SOURCE_DIR}/libosc_stream)
include_directories(${PROJECT_SOURCE_DIR}/inlist)
include_directories(${PROJECT_SOURCE_DIR}/eo_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/cjson)
include_directories(${PROJECT_SOURCE_DIR}/varchunk)

set(CMAKE_C_FLAGS "-std=gnu99 ${CMAKE_C_FLAGS}")

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

option(CHIMAERA_UI_PLUGINS "Build Chimaera UI plugins" ON)

add_library(chimaera MODULE
	cjson/cJSON.c

	chimaera.c
	filter.c
	mapper.c
	simulator.c
	visualizer.c
	comm.c
	control_out.c
	midi_out.c
	osc_out.c)
if(CHIMAERA_UI_PLUGINS)
	target_compile_definitions(chimaera PUBLIC -DCHIMAERA_UI_PLUGINS)
endif()
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
		comm_ui.c
		visualizer_ui.c)
	target_compile_definitions(chimaera_ui PUBLIC -DCHIMAERA_UI_PLUGINS)
	if(CHIMAERA_FAST_DISPATCH)
		target_compile_definitions(chimaera_ui PUBLIC -DCHIMAERA_FAST_DISPATCH)
	endif()
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
