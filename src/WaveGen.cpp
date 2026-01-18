// ============================================================================
// Includes
// ============================================================================
#include <waveGen.h>
#include <I2S/AudioConfig.h>
#include <arm_math.h>

// ============================================================================
// Globals
// ============================================================================
uint32_t gAcc = 0;
volatile float_t gFreq = 440.0f;
volatile float_t gVol = 1.0f;


// ============================================================================
// Public funcs
// ============================================================================

/// @brief Fill sound buffer with sounds.
void GenerateWave(uint16_t* out, size_t len)
{
	float_t period = SAMPLERATE * (1.0f/gFreq);

	for (size_t i = 0; i < len; i+=2)
	{
		gAcc++;

		// Calculate wave
		float32_t wave = (float)gAcc / period;
		wave = sinf(wave * TWO_PI) * gVol;
		int16_t sig = (int16_t)(4000.0f * wave);
		
		// Write left & right
		out[i] = (uint16_t)sig;
        out[i+1] = (uint16_t)sig;

		if (gAcc >= period)
		{
			gAcc -= period;
		}
	}
}

