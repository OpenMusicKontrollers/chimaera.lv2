cmake_minimum_required(VERSION 2.8)

project(chimaera.lv2)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/libosc)

set(DEST lib/lv2/chimaera.lv2)
set(LIB_EXT ".so")

find_package(PkgConfig) # ${PKG_CONFIG_FOUND}

pkg_search_module(LV2 REQUIRED lv2>=1.10)
include_directories(${LV2_INCLUDE_DIRS})
set(LIBS ${LIBS} ${LV2_LDFLAGS})

pkg_search_module(EDJE REQUIRED edje>=1.12)
include_directories(${EDJE_INCLUDE_DIRS})
set(LIBS_UI ${LIBS_UI} ${EDJE_LDFLAGS})

option(CHIMAERA_FAST_DISPATCH
	"Use custom dispatch structure instead of LV2 built-ins"
	OFF)

add_library(chimaera MODULE
	chimaera.c
	libosc/osc.c
	dummy_in.c
	dump_in.c
	tuio2_in.c
	filter.c
	mapper.c
	simulator.c
	control_out.c
	midi_out.c
	osc_out.c)
if(CHIMAERA_FAST_DISPATCH)
	target_compile_definitions(chimaera PUBLIC -DCHIMAERA_FAST_DISPATCH)
endif()
target_link_libraries(chimaera ${LIBS})
set_target_properties(chimaera PROPERTIES PREFIX "")
install(TARGETS chimaera DESTINATION ${DEST})

add_library(chimaera_ui MODULE
	chimaera_ui.c
	simulator_ui.c)
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
		${PROJECT_SOURCE_DIR}/pix/48.png
		${PROJECT_SOURCE_DIR}/pix/64.png
		${PROJECT_SOURCE_DIR}/pix/80.png
		${PROJECT_SOURCE_DIR}/pix/96.png
		${PROJECT_SOURCE_DIR}/pix/112.png
		${PROJECT_SOURCE_DIR}/pix/128.png
		${PROJECT_SOURCE_DIR}/pix/144.png
		${PROJECT_SOURCE_DIR}/pix/160.png)
add_custom_target(THEME ALL DEPENDS ${PROJECT_BINARY_DIR}/chimaera_ui.edj)

configure_file(${PROJECT_SOURCE_DIR}/manifest.ttl.in
	${PROJECT_BINARY_DIR}/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/manifest.ttl DESTINATION ${DEST})
install(FILES ${PROJECT_SOURCE_DIR}/chimaera.ttl DESTINATION ${DEST})
install(FILES ${PROJECT_BINARY_DIR}/chimaera_ui.edj DESTINATION ${DEST})
