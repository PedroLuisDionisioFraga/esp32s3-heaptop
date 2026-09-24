# This repository IS the heaptop component; examples/ hold the consumer projects.
idf_component_register(SRCS "src/heaptop.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES log)
