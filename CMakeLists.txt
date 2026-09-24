# This repository IS the heaptop component; examples/ hold the consumer projects.
idf_component_register(SRCS "src/heaptop.c"
                            "src/heaptop_calc.c"
                            "src/heaptop_console.c"
                            "src/heaptop_heap.c"
                            "src/heaptop_render.c"
                            "src/heaptop_tasks.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES console esp_timer heap log)
