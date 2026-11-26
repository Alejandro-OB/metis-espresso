idf_component_register(
    SRCS "app_main.c" "operational_fsm.c" "operational_runtime.c"
         "simulated_plant.c" "simulation_runtime.c" "external_hil_runtime.c" "calibration_command.c" "extraction_profile.c"
         "profile_runtime.c" "extraction_session.c" "pre_start_history.c" "extraction_message.c" "open_extraction.c" "telemetry_message.c" "series_message.c" "event_message.c" "condition_monitor.c" "profile_message.c" "device_config.c" "config_command.c" "firmware_update.c" "firmware_update_runtime.c" "extraction_runtime.c"
         "nvs_outbox.c" "sync_transport.c" "sync_runtime.c"
         "sensor_scaling.c" "signal_quality.c" "signal_filter.c" "sensor_calibration.c" "sensor_calibration_store.c"
         "ads1115.c" "acquisition_runtime.c" "process_variables.c"
         "process_supervisor.c" "supervisor_runtime.c" "actuation_guard.c"
         "stimulus_sequence.c" "identification_mode.c" "identification_tuning.c" "pump_ladder.c" "hydraulic_test.c"
         "control_filter.c" "pi_controller.c" "psm_scheduler.c" "heater_feedforward.c" "heater_recovery_hold.c" "heater_recovery_return.c" "heater_fast_start.c" "pressure_preinfusion.c"
         "control_log.c" "control_log_runtime.c"
         "control_plant.c" "control_tuning.c" "heater_modulator.c"
         "control_runtime.c"
         "indicator_panel.c" "indicator_runtime.c"
         "scale_reading.c" "hx711_runtime.c"
         "button_debounce.c" "button_hold.c" "panel_runtime.c" "panel_gesture.c" "tare_watch.c" "water_reference.c" "water_reference_runtime.c" "first_use_offset.c" "first_use_offset_runtime.c"
         "zero_cross_monitor.c" "zero_cross_runtime.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi esp_event esp_netif esp_psram esp_driver_gpio freertos
                  nvs_flash spi_flash esp_timer esp_http_client lwip esp_driver_i2c
                  esp_app_format app_update esp_partition
)
