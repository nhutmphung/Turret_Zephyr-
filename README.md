## This is the normal way to flash the zephyr files, just a reminder for me 

When you plug and replug the STLink, you have to bind and attach the usb file to WSL to run the flash 

> usbipd bind --busid 2-7
>
> usbipd attach --wsl --busid 2-7

then run these: 

> source ~/zephyrproject/.venv/bin/activate
>
> cd ~/zephyrproject/zephyr

source activates the .venv and then you cd into the zephyr repo

>
> west build -p auto -b "board" path/to/app
>
>west flash --runner openocd

west build is what builds your source code, and the west flash is what flashes into your microcontroller.

The links below are the documentation I used for the sensors: 



-[VF53L0X ToF Sensor](https://docs.zephyrproject.org/latest/doxygen/html/vl53l0x_8h.html)

-HC-SR501 (this is pretty simple, doesn't need drivers, just attach it to gpio pin and have it sent high if someone is in range or low if idle)

