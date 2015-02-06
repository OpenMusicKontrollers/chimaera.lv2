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

add_library(chimaera MODULE
	chimaera.c
	libosc/osc.c
	event_in.c
	filter.c
	control_out.c
	midi_out.c
	osc_out.c)
target_link_libraries(chimaera ${LIBS})
set_target_properties(chimaera PROPERTIES PREFIX "")
install(TARGETS chimaera DESTINATION ${DEST})

configure_file(${PROJECT_SOURCE_DIR}/manifest.ttl.in ${PROJECT_BINARY_DIR}/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/manifest.ttl DESTINATION ${DEST})
install(FILES ${PROJECT_SOURCE_DIR}/chimaera.ttl DESTINATION ${DEST})
