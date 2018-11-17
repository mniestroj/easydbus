#
# Copyright 2016, Grinn
#
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 2.6)
project(easydbus C)

add_definitions("-Wall -Wextra -Wno-unused-parameter")
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

add_subdirectory(src)
