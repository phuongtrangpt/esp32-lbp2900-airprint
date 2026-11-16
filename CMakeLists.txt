cmake_minimum_required(VERSION 3.16.0)
# explicit version: ESP-IDF otherwise runs `git describe`, which fails in a
# repository without commits
set(PROJECT_VER "0.6.0-hp-2bit-preview")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(usb_host_probe)
