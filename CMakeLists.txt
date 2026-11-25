cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(turret)

target_sources(app PRIVATE
	src/main.c
    sensors/vl53l0x.c
    sensors/pir.c
    sensors/oled.c
    drivers/coords.c
)

target_include_directories(app PRIVATE sensors drivers)


