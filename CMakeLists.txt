cmake_minimum_required(VERSION 2.8.12...3.29)

project(OvertureExtension)

include_directories(src/include)

set(EXTENSION_SOURCES src/overture_extension.cpp)

build_static_extension(overture ${EXTENSION_SOURCES})
set(PARAMETERS "-warnings")
build_loadable_extension(overture ${PARAMETERS} ${EXTENSION_SOURCES})

install(
  TARGETS overture_extension
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
