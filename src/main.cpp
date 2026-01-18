#include <I2S/Teensy4i2s.h>
#include <Wire.h>
#include <SPI.h>
#include <arm_math.h>
#include <WaveGen.h>

void setup(void)
{
	Serial.begin(9600);
	
	// Start the I2S interrupts
	BeginI2s();
}


void loop(void)
{
	delay(1);
}