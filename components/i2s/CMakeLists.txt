idf_component_register(SRCS "i2s_functions.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_timer wav
                       PRIV_REQUIRES esp_psram)