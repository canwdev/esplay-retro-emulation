idf_component_register(SRCS "main.c" "file_browser.c" "audio_player.c" "event.c" "file_ops.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esplay-ui esplay-hal acodecs)
