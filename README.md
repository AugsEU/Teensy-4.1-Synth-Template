# Teensy4.1 Synth Template

This repo shows how to get a basic synth working using the Teensy4.1 microcontroller and an external DAC with 16-bit audio. This project is mainly intended for demonstration purposes.

I2S code is based on https://github.com/GhostNoteAudio/Teensy4i2s which itself is based on https://github.com/PaulStoffregen/Audio but has been modified to use 16-bit audio instead of 32-bit.

## Wiring setup

The i2s protocol uses 3 wires, the clock line(BCLK), the word select(WSEL), and the data line(DIN). The sample data is in the data line word select is used to indicate which channel is being sent to the DAC. Look at the teensy 4.1 [pinout](https://www.pjrc.com/teensy/pinout.html) to see which pins are used for I2S. Here we assume the following pins:

```
   PIN 21 (BCLK1)  -> BCLK 
   PIN 20 (LRCLK1) -> WSEL
   PIN 7  (OUT1A)  -> DIN
```

If you wish to use other pins you need to modify this code.

## Usage:

This program is broken down into a few important files:

**main.cpp:** The top level of your program. Add input code here.

**OutputI2s.cpp:** Contains DMA configuration code. Modify this to change which pins are used and if you want to use 32-bit audio(see full [audio library](https://github.com/PaulStoffregen/Audio)).

**WaveGen.cpp:** This file contains the logic for generating your samples. By default it just generates a sine wave at 440Hz