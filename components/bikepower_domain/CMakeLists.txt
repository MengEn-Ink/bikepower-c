idf_component_register(
    SRCS "profile.c"
         "domain_rng.c"
         "hr_model.c"
         "power_engine.c"
         "running_engine.c"
         "config_store.c"
         "device_template.c"
         "json_miniparse.c"
         "button_logic.c"
         "led_pattern.c"
         "button_debounce.c"
    INCLUDE_DIRS "include"
)
