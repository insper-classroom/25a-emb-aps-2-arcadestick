add_executable(pico_emb
        hc06.c
        main.c
)

set_target_properties(pico_emb PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

target_link_libraries(pico_emb pico_stdlib hardware_adc oled1_lib freertos)
pico_add_extra_outputs(pico_emb)
