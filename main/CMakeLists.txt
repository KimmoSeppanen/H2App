idf_component_register(SRCS "main.c" "matter_bridge.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_adc driver nvs_flash esp_matter)

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")