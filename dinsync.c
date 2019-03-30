/* 
 * The software for the x0xb0x is available for use in accordance with the 
 * following open source license (MIT License). For more information about
 * OS licensing, please visit -> http://www.opensource.org/
 *
 * For more information about the x0xb0x project, please visit
 * -> http://www.ladyada.net/make/x0xb0x
 *
 *                                     *****
 * Copyright (c) 2005 Limor Fried
 *
 * Permission is hereby granted, free of charge, to any person obtaining a 
 * copy of this software and associated documentation files (the "Software"), 
 * to deal in the Software without restriction, including without limitation 
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the 
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in 
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS 
 * IN THE SOFTWARE.
 *                                     *****
 *
 */
#include <inttypes.h>
#include <avr/io.h>
#include "dinsync.h"
#include "midi.h"
#include "main.h"
#include "delay.h"

// counter: counts up DINSYNC_PPM per beat, for dinsync out
uint8_t		dinsync_counter = 0;
uint8_t		swing_counter = 0;




/* output functions (dinsync_start/stop) start and stop dinsync
   that is clocked from the internal tempo function */
void dinsync_start(void)
{
	// make sure we're not in a "dinsync in" mode
	if(sync != DIN_SYNC)
	{
		TCNT3 = OCR3A-delay_clock9ms; // next clock in 9ms; 
		// set the clock low (rising edge is a clock)
		cbi(DINSYNC_DATA, DINSYNC_CLK);
		// send start signal
		DINSYNC_DATA |= _BV(DINSYNC_START);
		dinsync_counter = 0;
		swing_counter = 0;
		note_counter = 0;
		clearPendingDinPulses();
	}
}

void dinsync_stop(void)
{
#ifdef SYNC_OUT
	if(sync != DIN_SYNC)
	{	// make sure we're not input mode
		cbi(DINSYNC_DATA, DINSYNC_START);	// easy, just set Start low.
	}
#endif 
}

/* input functions are for keeping track of whether an event occured
   on the dinsync port */

/* dinsync_started returns TRUE if the start pin is high and the previous
   call to this function was FALSE (ie. since the last function call, dinsync
   has started */
uint8_t dinsync_started(void)
{
	// keep track of the dinsync pin states for dinsync in
	static uint8_t	last_dinsync_start;
	uint8_t curr_dinsync_s;
	curr_dinsync_s = DINSYNC_PINS & (1<< DINSYNC_START);

	if(!last_dinsync_start && curr_dinsync_s)
	{
		last_dinsync_start = curr_dinsync_s;
		return TRUE;
	}

	last_dinsync_start = curr_dinsync_s;
	return FALSE;
}

/* dinsync_stopped returns TRUE if the start pin is low and the previous
   call to this function was FALSE (ie. since the last function call, dinsync
   has stopped */
uint8_t dinsync_stopped(void)
{
	static uint8_t last_dinsync_stop;
	uint8_t curr_dinsync_s;
	curr_dinsync_s = DINSYNC_PINS & (1<<DINSYNC_START);

	if(last_dinsync_stop && !curr_dinsync_s)
	{
		last_dinsync_stop = curr_dinsync_s;
		return TRUE;
	}

	last_dinsync_stop = curr_dinsync_s;
	return FALSE;
}

uint8_t dinsync_rs(void)
{
	static uint8_t last_dinsync;
	uint8_t curr_dinsync_s;

	curr_dinsync_s = DINSYNC_PINS & (1<<DINSYNC_START);
	
	if(curr_dinsync_s!=last_dinsync)
	{
		last_dinsync=curr_dinsync_s;
		if(curr_dinsync_s)
			return MIDI_START;
		else
			return MIDI_STOP;
	}
	return 0; 
	
}


/* these functions set the input/output descriptors */
void dinsync_set_out()
{
	DINSYNC_DDR |= _BV(DINSYNC_START) | _BV(DINSYNC_CLK) | _BV(DINSYNC_4) | _BV(DINSYNC_5);

	DINSYNC_DATA &= ~(_BV(DINSYNC_START) | _BV(DINSYNC_CLK) | _BV(DINSYNC_4) | _BV(DINSYNC_5));
}

void dinsync_set_in()
{
	DINSYNC_DDR &= ~(_BV(DINSYNC_START) | _BV(DINSYNC_CLK) | _BV(DINSYNC_4) | _BV(DINSYNC_5));

	DINSYNC_DATA &= ~(_BV(DINSYNC_START) | _BV(DINSYNC_CLK) | _BV(DINSYNC_4) | _BV(DINSYNC_5));
}
