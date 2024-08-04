add_executable(Time-Heart-Lumina
        Time-Heart-Lumina.c
        )

# pull in common dependencies
# Specify the target libraries
target_link_libraries(Time-Heart-Lumina pico_stdlib hardware_i2c hardware_gpio hardware_uart pico_multicore)

# Enable USB output, disable UART output
pico_enable_stdio_uart(Time-Heart-Lumina 1)
pico_enable_stdio_usb(Time-Heart-Lumina 0)


# create map/bin/hex file etc.
pico_add_extra_outputs(Time-Heart-Lumina)

# add url via pico_set_program_url
example_auto_set_url(Time-Heart-Lumina)
