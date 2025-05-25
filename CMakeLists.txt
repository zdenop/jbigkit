cmake_minimum_required(VERSION 3.5)
project(jbigkit C)

option(BUILD_PROGRAMS "Build programs." ON)
option(BUILD_TOOLS "Build pbm tools." ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libjbig)
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -DNDEBUG")
set(CMAKE_C_FLAGS_DEBUG    "-O2 -Ob0")

########################################
# JBIG-KIT library
########################################

if (WIN32)
  SET(CMAKE_DEBUG_POSTFIX "d")
  if (MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  endif()
else()
  set(CMAKE_C_FLAGS_DEBUG    "${CMAKE_C_FLAGS_DEBUG} -g -noalign")
endif()

set(libjbig_src 
  "${CMAKE_CURRENT_SOURCE_DIR}/libjbig/jbig.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/libjbig/jbig_ar.c"
)
add_library(libjbig ${libjbig_src})

set(libjbig85_src 
  "${CMAKE_CURRENT_SOURCE_DIR}/libjbig/jbig85.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/libjbig/jbig_ar.c"
)
add_library(libjbig85 ${libjbig85_src})
install(TARGETS libjbig RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(TARGETS libjbig85 RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/libjbig/jbig.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/libjbig/jbig_ar.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/libjbig/jbig85.h"
  DESTINATION include/)

########################################
# Programs
########################################

if(BUILD_PROGRAMS)
  add_executable (tstcodec "${CMAKE_CURRENT_SOURCE_DIR}/libjbig/tstcodec.c")
  set_target_properties(tstcodec PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
  target_link_libraries (tstcodec libjbig$<$<CONFIG:Debug>:d>)
  install(TARGETS tstcodec RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)

  add_executable (tstcodec85 "${CMAKE_CURRENT_SOURCE_DIR}/libjbig/tstcodec85.c")
  set_target_properties(tstcodec85 PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
  target_link_libraries (tstcodec85 libjbig85$<$<CONFIG:Debug>:d>)
  install(TARGETS tstcodec85 RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
endif(BUILD_PROGRAMS)

########################################
# pbm tools
########################################

if(BUILD_TOOLS)
  add_executable (pbmtojbg "${CMAKE_CURRENT_SOURCE_DIR}/pbmtools/jbgtopbm.c")
  set_target_properties(pbmtojbg PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
  target_link_libraries (pbmtojbg libjbig$<$<CONFIG:Debug>:d>)
  install (TARGETS pbmtojbg RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)

  add_executable (jbgtopbm85 "${CMAKE_CURRENT_SOURCE_DIR}/pbmtools/jbgtopbm85.c")
  set_target_properties(jbgtopbm85 PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
  target_link_libraries (jbgtopbm85 libjbig85$<$<CONFIG:Debug>:d>)
  install (TARGETS jbgtopbm85 RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)

  add_executable (jbgtopbm "${CMAKE_CURRENT_SOURCE_DIR}/pbmtools/jbgtopbm.c")
  set_target_properties(jbgtopbm PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
  target_link_libraries (jbgtopbm libjbig$<$<CONFIG:Debug>:d>)
  install (TARGETS jbgtopbm RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)

  add_executable (pbmtojbg85 "${CMAKE_CURRENT_SOURCE_DIR}/pbmtools/pbmtojbg85.c")
  set_target_properties(pbmtojbg85 PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
  target_link_libraries (pbmtojbg85 libjbig85$<$<CONFIG:Debug>:d>)
  install (TARGETS pbmtojbg85 RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
endif(BUILD_TOOLS)
