# x0xb0x-MarOS15
MarOS15 Operating System for x0xb0x - see https://forums.adafruit.com/viewtopic.php?f=13&t=33914

Compile with AtmelStudio.
Flash x0xb0x with avrdude:

`avrdude -p atmega2561 -c stk500v2 -P COM3 -b 57600 -e -U flash:w:MarOS_2561.hex`

(replace COM3 with the actual port used by the x0xb0x)

Compilation for x0xlarge CPU (Atmega2561).

Original code by Limor Fried - https://www.adafruit.com/ - http://www.ladyada.net/
Modifications Sokkan - Mario1089 - Nordcore
