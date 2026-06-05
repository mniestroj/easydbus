#
# Copyright 2016, Grinn
#
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.10...4.3)
project(easydbus C)

set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")

add_subdirectory(src)
