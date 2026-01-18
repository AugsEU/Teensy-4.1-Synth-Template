/* Audio Library for Teensy 3.X  (adapted for Teensy 4.x)
 * Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
// High-level explanation of how this I2S & DMA code works:
// https://forum.pjrc.com/threads/65229?p=263104&viewfull=1#post263104

// ============================================================================
// Include
// ============================================================================
#include <Arduino.h>
#include <cstdlib>
#include <cmath>
#include "utility/imxrt_hw.h"
#include "imxrt.h"

#include "AudioConfig.h"
#include "OutputI2s.h"
#include "I2sTimers.h"



// ============================================================================
// Forward decl
// ============================================================================
void ConfigI2s(bool only_bclk = false);
void OnDmaTransmit();





// ============================================================================
// Globals
// ============================================================================
DMAChannel gDma(false);
DMAMEM __attribute__((aligned(32))) static uint32_t i2s_tx_buffer[AUDIO_BLOCK_SAMPLES*NUM_DMA_SECTIONS];








// ============================================================================
// Public funcs
// ============================================================================

/// @brief Begin I2S setup DMA and interrupts.
void BeginI2s()
{
	gDma.begin(true); // Allocate the DMA channel first
	ConfigI2s();

	// Minor loop = each individual transmission, in this case, 4 bytes of data
	// Major loop = the buffer size, events can run when we hit the half and end of the major loop
	// To reset Source address, trigger interrupts, etc.
	CORE_PIN7_CONFIG  = 3;  //1:TX_DATA0
	gDma.TCD->SADDR = i2s_tx_buffer;
	gDma.TCD->SOFF = 2; // how many bytes to jump from current address on the next move
	gDma.TCD->ATTR = DMA_TCD_ATTR_SSIZE(1) | DMA_TCD_ATTR_DSIZE(1); // 1=16bits, 2=32 bits. size of source, size of dest
	gDma.TCD->NBYTES_MLNO = 2; // number of bytes to move, (minor loop?)
	gDma.TCD->SLAST = -sizeof(i2s_tx_buffer); // how many bytes to jump when hitting the end of the major loop. In this case, jump back to start of buffer
	gDma.TCD->DOFF = 0; // how many bytes to move the destination at each minor loop. In this case we're always writing to the same memory register.
	gDma.TCD->CITER_ELINKNO = sizeof(i2s_tx_buffer) / 2; // how many iterations are in the major loop
	gDma.TCD->DLASTSGA = 0; // how many bytes to jump the destination address at the end of the major loop
	gDma.TCD->BITER_ELINKNO = sizeof(i2s_tx_buffer) / 2; // beginning iteration count
	gDma.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR; // Tells the DMA mechanism to trigger interrupt at half and full population of the buffer
	gDma.TCD->DADDR = (void *)((uint32_t)&I2S1_TDR0 + 2); // Destination address. for 16 bit values we use +2 byte offset from the I2S register. for 32 bits we use a zero offset.
	gDma.triggerAtHardwareEvent(DMAMUX_SOURCE_SAI1_TX); // run DMA at hardware event when new I2S data transmitted.
	gDma.enable();

	// Enabled transmitting and receiving
	I2S1_RCSR |= I2S_RCSR_RE | I2S_RCSR_BCE;
	I2S1_TCSR = I2S_TCSR_TE | I2S_TCSR_BCE | I2S_TCSR_FRDE;
	gDma.attachInterrupt(OnDmaTransmit);
}




// ============================================================================
// Private funcs
// ============================================================================


/// @brief This gets called twice per block, when buffer is half full and completely full
// Every other call, after we've pushed the second half of the current block onto 
// the tx_buffer, we trigger the process() call again, computing a new block of data
void OnDmaTransmit()
{
	uint16_t* dest;
	uint32_t saddr;

	saddr = (uint32_t)(gDma.TCD->SADDR);
	gDma.clearInterrupt();
	if (saddr < (uint32_t)i2s_tx_buffer + sizeof(i2s_tx_buffer) / NUM_DMA_SECTIONS) 
	{
		// DMA is transmitting the first half of the buffer
		// so we must fill the second half
		dest = (uint16_t *)&i2s_tx_buffer[AUDIO_BLOCK_SAMPLES];
	}
	else
	{
		// DMA is transmitting the second half of the buffer
		// so we must fill the first half
		dest = (uint16_t *)i2s_tx_buffer;
	}

	Timers::ResetFrame();

	// Write wave to destination buffer
	GenerateWave(dest, AUDIO_BLOCK_SAMPLES * NUM_CHANNELS);
	arm_dcache_flush_delete(dest, sizeof(i2s_tx_buffer) / 2 );

	Timers::LapInner(Timers::TIMER_TOTAL);
}



/// @brief This function sets all the necessary PLL and I2S flags necessary for running
void ConfigI2s(bool only_bclk)
{
	CCM_CCGR5 |= CCM_CCGR5_SAI1(CCM_CCGR_ON);

	// if either transmitter or receiver is enabled, do nothing
	if ((I2S1_TCSR & I2S_TCSR_TE) != 0 || (I2S1_RCSR & I2S_RCSR_RE) != 0)
	{
		if (!only_bclk) // if previous transmitter/receiver only activated BCLK, activate the other clock pins now
		{
			CORE_PIN23_CONFIG = 3;  //1:MCLK
			CORE_PIN20_CONFIG = 3;  //1:RX_SYNC (LRCLK)
		}
		return;
	}

	//PLL:
	int fs = SAMPLERATE;
	// PLL between 27*24 = 648MHz und 54*24=1296MHz
	int n1 = 4; //SAI prescaler 4 => (n1*n2) = multiple of 4
	int n2 = 1 + (24000000 * 27) / (fs * 256 * n1);

	double C = ((double)fs * 256 * n1 * n2) / 24000000;
	int c0 = C;
	int c2 = 10000;
	int c1 = C * c2 - (c0 * c2);
	set_audioClock(c0, c1, c2);

	// clear SAI1_CLK register locations
	CCM_CSCMR1 = (CCM_CSCMR1 & ~(CCM_CSCMR1_SAI1_CLK_SEL_MASK))
		   | CCM_CSCMR1_SAI1_CLK_SEL(2); // &0x03 // (0,1,2): PLL3PFD0, PLL5, PLL4
	CCM_CS1CDR = (CCM_CS1CDR & ~(CCM_CS1CDR_SAI1_CLK_PRED_MASK | CCM_CS1CDR_SAI1_CLK_PODF_MASK))
		   | CCM_CS1CDR_SAI1_CLK_PRED(n1-1) // &0x07
		   | CCM_CS1CDR_SAI1_CLK_PODF(n2-1); // &0x3f

	// Select MCLK
	IOMUXC_GPR_GPR1 = (IOMUXC_GPR_GPR1
		& ~(IOMUXC_GPR_GPR1_SAI1_MCLK1_SEL_MASK))
		| (IOMUXC_GPR_GPR1_SAI1_MCLK_DIR | IOMUXC_GPR_GPR1_SAI1_MCLK1_SEL(0));

	if (!only_bclk)
	{
	  CORE_PIN23_CONFIG = 3;  //1:MCLK
	  CORE_PIN20_CONFIG = 3;  //1:RX_SYNC  (LRCLK)
	}
	CORE_PIN21_CONFIG = 3;  //1:RX_BCLK

	int rsync = 0;
	int tsync = 1;

	I2S1_TMR = 0;
	//I2S1_TCSR = (1<<25); //Reset
	I2S1_TCR1 = I2S_TCR1_RFW(1);
	I2S1_TCR2 = I2S_TCR2_SYNC(tsync) | I2S_TCR2_BCP // sync=0; tx is async;
		    | (I2S_TCR2_BCD | I2S_TCR2_DIV((1)) | I2S_TCR2_MSEL(1));
	I2S1_TCR3 = I2S_TCR3_TCE;
	I2S1_TCR4 = I2S_TCR4_FRSZ((2-1)) | I2S_TCR4_SYWD((32-1)) | I2S_TCR4_MF
		    | I2S_TCR4_FSD | I2S_TCR4_FSE | I2S_TCR4_FSP;
	I2S1_TCR5 = I2S_TCR5_WNW((32-1)) | I2S_TCR5_W0W((32-1)) | I2S_TCR5_FBT((32-1));

	I2S1_RMR = 0;
	//I2S1_RCSR = (1<<25); //Reset
	I2S1_RCR1 = I2S_RCR1_RFW(1);
	I2S1_RCR2 = I2S_RCR2_SYNC(rsync) | I2S_RCR2_BCP  // sync=0; rx is async;
		    | (I2S_RCR2_BCD | I2S_RCR2_DIV((1)) | I2S_RCR2_MSEL(1));
	I2S1_RCR3 = I2S_RCR3_RCE;
	I2S1_RCR4 = I2S_RCR4_FRSZ((2-1)) | I2S_RCR4_SYWD((32-1)) | I2S_RCR4_MF
		    | I2S_RCR4_FSE | I2S_RCR4_FSP | I2S_RCR4_FSD;
	I2S1_RCR5 = I2S_RCR5_WNW((32-1)) | I2S_RCR5_W0W((32-1)) | I2S_RCR5_FBT((32-1));
}
