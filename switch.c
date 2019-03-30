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
#include <avr/interrupt.h>
#include <stdio.h>
#include "switch.h"
#include "led.h"
#include "main.h"
#include "delay.h"
#include "dinsync.h"
#include "pattern.h"
#include "compcontrol.h"

uint8_t		switches[3];
uint8_t		last_switches[3];

uint8_t		pressed_switches[3];
uint8_t		released_switches[3];

uint8_t		bank_switched = 0;	// has the bank knob been moved?
uint8_t		function, bank;
uint8_t		last_func, last_bank;



const uint8_t loopkey_tab[20] ={KEY_C,KEY_CS,KEY_D,KEY_DS,KEY_E,KEY_F,KEY_FS,KEY_G,KEY_GS,KEY_A,KEY_AS,KEY_B,KEY_C2,KEY_REST,KEY_ACCENT,KEY_SLIDE,  KEY_PREV, KEY_NEXT, KEY_DOWN, KEY_UP  };

const uint8_t numkey_tab[8] = { KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_A, KEY_B, KEY_C2 };
const uint8_t blackkey_tab[5]= {KEY_CS,KEY_DS,KEY_FS, KEY_GS, KEY_AS }; 

uint8_t read_switches(void)
{
	uint8_t i, s, l, t;
	uint8_t temp_switches[3];
	uint8_t before_s;

	clock_ticks();
	do_uart_cmd();

	for(i = 0; i < 3; i++)
	{
		pressed_switches[i] = 0;
		released_switches[i] = 0;
	}
	

	// to debounce switches, check if it's been more than DEBOUNCETIME ms since
	// the last check, and that the switch is the same as it was 20ms ago.
	if(debounce_timer < DEBOUNCETIME) // timer is in 1ms incr
	{
		clock_leds();
		return 1;			
	}
	debounce_timer = 0; // reset the timer

	// check if temp knob has bee moved (only every DEBOUNCETIME ms - so there is always enough time for send_tempo and EEPROM write, even if encoder turned very moved fast  ) 
	if(tk_value)
	{
		int16_t val; 
		cli();
		val=tk_value;
		tk_value=0;
		sei();
		if(val)
		{
			if(!tempoKnobMode)
				change_tempo(tempo+val);
			else
				changeTempoKnobValue(val);
		}
	}

	random(); // walk the random counter. 
	// The only real entropy we have is *when* the user does something. 
	// Moving the "random" state from time to time ensures that the user sees "real" random, 
	// when he presses a key, as it is not predictable where the "not so random counter" is sampled. 

	read_keypad(temp_switches);

	for(i = 0; i < 3; i++)
	{
		before_s= s = switches[i];	// debounced state
		l = last_switches[i];		// last input state
		t = temp_switches[i];		// new input state

		switches[i] = s = (s & (l | t)) | (~s & l & t);
		last_switches[i] = t;

		pressed_switches[i] = (before_s ^ s) & s;
		released_switches[i] = (before_s ^ s) & before_s;
	}

	// bank and function reads are "pipelined": they are read in one 20ms cycle, than the select pin 
	// is moved to the other switch. Actual just about 5µs needed - but this is not larger code, will  
	// work even with very slow selection/pull up and is easy to watch on the scope. 
  	if( (FUNC_COMMON_PORT & (1<<FUNC_COMMON_PIN)) ==0 )
  	{
		static const __flash uint8_t bankPinShuffle[]={15,7,11,3,13,5,9,1,14,6,10,2,12,4,8,0};
	  	i = BANK_PIN;	// get bank input 
	  	i = bankPinShuffle[(i >> 2) & 0xF];	// unshuffle pins: (pins[2..5], mirrored and inverted) 

	  	if((i != bank) && (i == last_bank))
	  	{
		  	bank = i;
		  	bank_switched = 1;
	  	}
	  	last_bank = i;
	  	FUNC_COMMON_PORT |= _BV(FUNC_COMMON_PIN);	// select func read
  	}
  	else
  	{
	  	i = FUNC_PIN;		// get function input 
	  	i = (i >> 2) & 0xF;	// unshuffle pins 

	  	if((i != function) && (i == last_func))
		  	function = i;
	  	last_func = i;
	  	BANK_COMMON_PORT &= ~_BV(BANK_COMMON_PIN);	// select bank read
  	}
	clock_leds();
	return 0;
}

void read_keypad(uint8_t *switchinput)
{
	uint8_t i;
	cli();
	cbi(SWITCH_LATCH_PORT, SWITCH_LATCH_PIN);
	NOP;
	NOP;
	NOP;
	NOP;
	sbi(SWITCH_LATCH_PORT, SWITCH_LATCH_PIN);
	for(i = 0; i < 3; i++)
	{
		SPDR = 0;
		while(!(SPSR & (1 << SPIF)));
			switchinput[i] = SPDR;
	}
	sei();
}

/************************************************************************************/
//
//	These four functions are not used any more - just to save a couple of bytes. 
//	they would have been called only once in read_switches. 
//
// we need to call this, then wait a bit, then read the value off the pins
void select_bank_read(void)
{
	BANK_COMMON_PORT &= ~_BV(BANK_COMMON_PIN);
}
// we need to call this, then wait a bit, then read the value off the pins
void select_func_read(void)
{
	FUNC_COMMON_PORT |= _BV(FUNC_COMMON_PIN);
}
uint8_t read_bank()
{
	uint8_t val;

	val = ~BANK_PIN;
	val = ((val >> 5) & 1) | ((val >> 3) & 0x2) | ((val >> 1) & 0x4) | ((val << 1) & 0x8);
	return val;
}
uint8_t read_function()
{
	uint8_t val;

	val = FUNC_PIN;
	val = (val >> 2) & 0xF;

	return val;
}
/************************************************************************************/


// prototype for key pressing/releasing detection
uint8_t key_action(uint8_t key, uint8_t *keyvec)
{
	if(key >= 24)
		return 0;

	if((keyvec[key / 8] & (1 << key % 8)) != 0)
		return 1;
	else
		return 0;
}

// returns 1 if that key is pressed
uint8_t is_pressed(uint8_t key)
{
	return key_action(key, switches);
}

uint8_t just_pressed(uint8_t key)
{
	return key_action(key, pressed_switches);
}

uint8_t just_released(uint8_t key)
{
	return key_action(key, released_switches);
}

uint8_t no_keys_pressed(void)
{
	if((switches[0] == 0) && (switches[1] == 0) && (switches[2] == 0))
		return 1;
	return 0;
}

static uint8_t get_lowest_key_action(const uint8_t *key_tab, uint8_t size, uint8_t *keyvec)
{
	uint8_t i;
	for(i = 0; i < size; i++)
	{
		if(key_action(key_tab[i],keyvec))
			return i + 1;
	}
	return 0;
}

uint8_t get_lowest_key_pressed(const uint8_t *key_tab, uint8_t size)
{
	return get_lowest_key_action(key_tab,size, switches);
}
static uint8_t get_lowest_key_just_pressed(const uint8_t *key_tab, uint8_t size)
{
	return get_lowest_key_action(key_tab,size, pressed_switches);
}


int8_t get_lowest_notekey_pressed(void)
{
	return get_lowest_key_pressed(loopkey_tab, 13) - 1;
}


uint8_t get_lowest_numkey_pressed(void)
{
	return get_lowest_key_pressed(numkey_tab, 8);
}

uint8_t get_lowest_blackkey_just_pressed(void)
{
	return get_lowest_key_just_pressed(blackkey_tab, 5);
}

uint8_t get_lowest_numkey_just_pressed(void)
{
	return get_lowest_key_just_pressed(numkey_tab, 8);
}

uint8_t get_lowest_loopkey_just_pressed(void)
{
	return get_lowest_key_just_pressed(loopkey_tab, 16);
}

uint8_t get_lowest_functionkey_just_pressed(void)
{
	return get_lowest_key_just_pressed(loopkey_tab, 20);
}

uint8_t get_lowest_notekey_just_pressed(void)
{
	return get_lowest_key_just_pressed(loopkey_tab, 13);
}

uint8_t has_bank_knob_changed(void)
{
	uint8_t temp = bank_switched;
	bank_switched = 0;
	return temp;
}
