/* Simple soundengine for the BitBox
 * Copyright 2015, Makapuf <makapuf2@gmail.com>
 * Copyright 2014, Adrien Destugues <pulkomandy@pulkomandy.tk>
 * Copyright 2007, Linus Akesson
 * Based on the "Hardware Chiptune" project
 *
 * This main file is a player for the packed music format used in the original
 * "hardware chiptune"
 * http://www.linusakesson.net/hardware/chiptune.php
 * There is a tracker for this but the format here is slightly different (mostly
 * because of the different replay rate - 32KHz instead of 16KHz).
 *
 * Because of this the sound in the tracker will be a bit different, but it can
 * easily be tweaked. This version has a somewhat bigger but much simplified song format.
 */
#include "chiptune.h"

volatile struct  oscillator osc[8];

static uint8_t quicksin(int32_t x)
{
	// Magic from http://www.coranac.com/2009/07/sines/

	// S(x) = x * ( (3<<p) - (x*x>>r) ) >> s
	// n : Q-pos for quarter circle             13
	// A : Q-pos for output                     5
	// p : Q-pos for parentheses intermediate   15
	// r = 2n-p
	// s = A-1-p-n

	static const int qN = 14, qA= 7, qP= 15, qR= 2*qN-qP, qS= qN+qP+1-qA;

	x= x<<(30-qN);			// shift to full s32 range (Q13->Q30)

	if( (x^(x<<1)) < 0)		// test for quadrant 1 or 2
		x= (1<<31) - x;

	x= x>>(30-qN);

	return x * ( (3<<qP) - (x*x>>qR) ) >> qS;
}

// This function generates one audio sample for all 8 oscillators. The returned
// value is a 2*8bit stereo audio sample ready for putting in the audio buffer.
//
// You most likely want to call this from game_snd_buffer to fill the buffers.
uint16_t gen_sample()
{
	int16_t acc[2]; // accumulators for each channel
	static uint32_t noiseseed = 1;
	int32_t values[8];

	// This is a simple noise generator based on an LFSR (linear feedback shift
	// register). It is fast and simple and works reasonably well for audio.
	// Note that we always run this so the noise is not dependant on the
	// oscillators frequencies.
	uint32_t newbit;
	newbit = 0;
	if(noiseseed & 0x80000000L) newbit ^= 1;
	if(noiseseed & 0x01000000L) newbit ^= 1;
	if(noiseseed & 0x00000040L) newbit ^= 1;
	if(noiseseed & 0x00000200L) newbit ^= 1;
	noiseseed = (noiseseed << 1) | newbit;

	// Now compute the value of each oscillator and mix them
	for(int i=0; i < 8; i++) {
		int8_t value; // [-128,127]

		switch(osc[i].waveform) {
			case WF_TRI:
				// Triangle: the part before 0x8000 raises, then it goes back
				// down.
				if(osc[i].phase < 0x8000) {
					value = -128 + (osc[i].phase >> 7);
				} else {
					value = 127 - ((osc[i].phase - 0x8000) >> 7);
				}
				break;
			case WF_SAW:
				// Sawtooth: always raising.
				value = -128 + (osc[i].phase >> 8);
				break;
			case WF_PUL:
				// Pulse: max value until we reach "duty", then min value.
				value = (osc[i].phase > osc[i].duty)? -128 : 127;
				break;
			case WF_NOI:
				// Noise: from the generator. Only the low order bits are used.
				value = (noiseseed & 255) - 128;
				break;

			case WF_SIN:
				if (osc[i].phase < osc[i].duty || osc[i].phase > 0xFFFF-osc[i].duty)
					value = quicksin(osc[i].phase);
				else
					value = 0;
				break;
			case WF_ABSSIN:
				if (osc[i].phase < osc[i].duty)
					value = quicksin(osc[i].phase);
				else
					value = quicksin(0xFFFF-osc[i].phase);
				break;
			case WF_QSIN:
				if (osc[i].phase & 0x4000)
					value = 0;
				else
					value = quicksin(osc[i].phase);
				break;

			default:
				value = 0;
				break;
		}

		// Compute the oscillator phase (position in the waveform) for next time
		osc[i].phase += osc[i].freq / 4;

		// bit crusher effect
		if (osc[i].bitcrush) {
			value |= ((1<<osc[i].bitcrush) - 1);
		}

		// Store oscillator value pre-multiplied by volume [-32640;32385]
		values[i] = value * osc[i].volume;

		// Phase-modulate oscillators 0-3 with 4-7
		if (i > 3)
		{
			osc[i - 4].phase += values[i] >> 4;
		}

	}
	
	// Ring-modulation of each channel with channel+4
	// And mixing into "left" and "right" output channels
	// And also scaling to 8 bits, while we are at it.
	acc[0] = ((int32_t)values[0] + (int32_t)values[2]) >> 9; // [-128;127]
	acc[1] = ((int32_t)values[1] + (int32_t)values[3]) >> 9;

	return (128 + acc[0]) | ((128 + acc[1]) << 8);    // [0,255]
}

