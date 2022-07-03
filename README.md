# Neon Timing Serial Example

This is an example of the [Neon Timing Protocol](https://github.com/NextLevelTiming/neon-timing-protocol) over a serial connection.

This example will establish a connection to another Neon Timing client and wait for race events. Once race events are received the device will use the attached neopixels to display the current race state.

- Race Stating: Solid red lights
- Countdown Started: Animated flashing red lights
- Countdown End Delay Started: Turns off all lights
- Race Started: Solid green lights
- Race Completed: Animated flashing white lights for 10 seconds, then red lights

## Animated Light Shows
It should be noted this code is more complex due to the animated light shows: countdown started and race completed. This is done to allow processing new Neon Timing Protocol messages while an these light shows are still being displayed.

Most example code for NeoPixel animated lights will end up blocking the entire processor from doing anything during the animation. This example will intentionally interupt animations as we process new race events.

## Components
While this code will work for many ESP32 devices and can be ported to other types of microcontrollers- here are the components we tested with.

> The 470 ohm resistor between the ESP32 and NeoPixels can help prevent damage to your first pixel.
> Consider reading the [NeoPixel Uberguide](https://learn.adafruit.com/adafruit-neopixel-uberguide/basic-connections) to connect other types of NeoPixels.

- [ESP32](https://www.adafruit.com/product/5325)
- [NeoPixel Stick](https://www.adafruit.com/product/1426)
- Connect board A0 -> ~470ohm resistor -> pixels DIN
- Connect board V5-> pixels VDC
- Connect board GND -> pixels GND

