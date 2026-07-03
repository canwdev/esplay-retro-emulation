cmake_minimum_required(VERSION 3.20)
project(launcher_sim LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)

include(FetchContent)

FetchContent_Declare(
  SDL2
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-2.30.10
  GIT_SHALLOW TRUE
)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

FetchContent_Declare(
  lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.4.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(lvgl)

add_executable(launcher_sim
  main.c
  ../port/sim/dirent.c
  ../port/sim/audio_sim.c
  ../../esplay-sdk/hal-drivers/eq.c
  ../../esplay-sdk/hal-drivers/id3.c
  ../port/sim/hal_audio_stub.c
  ../port/sim/hal_display_sdl.c
  ../port/sim/hal_input_sdl.c
  ../port/sim/hal_power_stub.c
  ../port/sim/hal_settings_file.c
  ../port/sim/hal_storage_posix.c
  ../port/sim/hal_system_sim.c
  ../port/sim/platform_log_sim.c
  ../port/sim/platform_mem_sim.c
  ../port/sim/platform_time_sim.c
  ../main/ui_font.c
  ../main/fonts/ui_font_esplayfont.c
  ../main/ui_theme.c
  ../main/ui_chrome.c
  ../main/ui_home.c
  ../main/ui_settings.c
  ../main/ui_screen_test.c
  ../main/ui_backlight.c
  ../main/input_bridge.c
  ../main/input_repeat.c
  ../main/file_manager.c
  ../main/preview/preview_registry.c
  ../main/preview/preview_text.c
  ../main/preview/preview_audio.c
  ../main/preview/preview_audio_lrc.c
  ../main/preview/preview_audio_playlist.c
  ../main/preview/preview_bmp.c
)

target_compile_definitions(launcher_sim PRIVATE TARGET_SIM)
target_include_directories(launcher_sim PRIVATE
  ../main
  ../main/preview
  ../hal
  ../port/sim
  ../../esplay-sdk/hal-drivers
  ../../esplay-sdk/hal-drivers/include
  "${lvgl_SOURCE_DIR}/src"
)
target_link_libraries(launcher_sim PRIVATE lvgl SDL2::SDL2)

# ui_font_esplayfont.c contains UTF-8 glyph comments (CJK); avoid MSVC C4819 on CP936.
if(MSVC)
  target_compile_options(launcher_sim PRIVATE /utf-8)
endif()

if(WIN32)
  add_custom_command(TARGET launcher_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL2::SDL2>
            $<TARGET_FILE_DIR:launcher_sim>
  )
endif()
