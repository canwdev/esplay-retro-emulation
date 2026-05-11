cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS
    ./esplay-components/esplay-hal
    ./esplay-components/esplay-ui
    ./esplay-components/ugui)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esplay_sd_minimal)
