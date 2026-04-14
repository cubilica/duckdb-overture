PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

EXT_NAME=overture
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

include makefiles/duckdb_extension.Makefile
