add_executable(pico_emb
        button.c
        pot.c
        hc06.c
        fsr.c
        bluetooth.c
        main.c
)

set_target_properties(pico_emb PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

pico_add_extra_outputs(pico_emb)

# Link the Project to extra libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
  pico_emb
  pico_stdlib
  hardware_adc
  oled1_lib
  freertos
  pico_btstack_classic
  pico_btstack_cyw43
  pico_cyw43_arch_none
)

# Including header files directly from project directory
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
