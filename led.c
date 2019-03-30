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
#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdio.h>
#include "main.h"
#include "switch.h"
#include "led.h"
#include "synth.h"
#include "pattern.h"

static uint8_t leds[5] = { 0, 0, 0, 0, 0 };
static uint8_t blinkleds[5] = { 0, 0, 0, 0, 0 };
static uint8_t dimblinkleds[5] = { 0, 0, 0, 0, 0 };
static uint8_t dimleds[5] = { 0, 0, 0, 0, 0 };
	

/*
[0] 0..7
[1] 8..15
[2] 16..23
[3] 24..31
[4] 32..40
(LED_BANK1); 38 [4]
(LED_BANK4); 30 [3]
(LED_BANK7); 18 [2]
(LED_BANK10); 20 [2]

(LED_BANK1); 38 [4]
(LED_BANK5); 31 [3]
(LED_BANK9); 19 [2]
(LED_BANK13); 23 [2]
*/
static const __flash uint8_t	darkleds_0[5+0]= {0,0,0,0,0,
/*
											0x00, 0x00,0x00,0x00,0x00,0x00,0xDE,0xAD,0xDA,0x7A,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
												  0x00,0x0A,0xDD,0x00,0xC0,0xDE,0x00,0x00,0x0A,0xDD,0x00,0xDA,0x7A,0x00,0x00,0x00,
												  0xFE,0xED,0x00,0xC0,0x01,0x03,0x03,0x00,0xBA,0x55,0x00,0xC0,0xDe,0x00,0x00,0x00,
												  0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,
*/
// 15872 Bytes max prog size 

												 };
												 
static const __flash uint8_t	darkleds_t[5] = { 0, 0, BITBYTE(LED_BANK10)|BITBYTE(LED_BANK7), BITBYTE(LED_BANK4), BITBYTE(LED_BANK1) };
static const __flash uint8_t	darkleds_q[5] = { 0, 0, BITBYTE(LED_BANK13)|BITBYTE(LED_BANK9), BITBYTE(LED_BANK5), BITBYTE(LED_BANK1) };


uint8_t blink_led_on; // current blink state
uint8_t dimblink_led_on; // current blink state
uint8_t dimblink_led_ctr;   // PWM and blink ctr for dimmed or blinking leds   

static uint8_t showingSteps; // Bank/Step-LEDs are showing a Step, so the help LEDs might be on 

#ifdef WITHRANGECHECK
	#define IFRANGE(a) if(a)  // check if LED set commands are within LED, if not do nothing. 
#else 
	#define IFRANGE(a)	// saves 112 bytes precious program space! 
	// But if non exsistent LEDs are accessed, some random memory is changed. 
	// note: the "255" for "no LED" is OK because *that* is only used in compares, which just never deliver true. 
	// but that has to be checked thoroughly... 
#endif 

 static const uint8_t	bank_led_tab[16] =
{
	LED_BANK1,
	LED_BANK2,
	LED_BANK3,
	LED_BANK4,
	LED_BANK5,
	LED_BANK6,
	LED_BANK7,
	LED_BANK8,
	LED_BANK9,
	LED_BANK10,
	LED_BANK11,
	LED_BANK12,
	LED_BANK13,
	LED_BANK14,
	LED_BANK15,
	LED_BANK16
};


static const uint8_t	key_led_tab[24] =
{
	LED_CHAIN,
	LED_RS,
	LED_TEMPO,
	LED_PREV,
	LED_C,
	LED_D,
	LED_E,
	LED_F,
	LED_NEXT,
	LED_CS,
	LED_DS,
	LED_FS,
	LED_GS,
	LED_AS,
	LED_DOWN,
	LED_UP,
	LED_B,
	LED_G,
	LED_A,
	LED_C2,
	LED_REST,
	LED_ACCENT,
	LED_SLIDE,
	LED_DONE
};

// table for converting notes (C = 0, C2 = 12) into leds
static const uint8_t	notekey_led_tab[13 + 5] =
{
	LED_C,
	LED_CS,
	LED_D,
	LED_DS,
	LED_E,
	LED_F,
	LED_FS,
	LED_G,
	LED_GS,
	LED_A,
	LED_AS,
	LED_B,
	LED_C2,

	// make the list complete so we can clear all "note function" leds from a single table
	LED_DOWN,
	LED_UP,
	LED_REST,
	LED_ACCENT,
	LED_SLIDE
};

// table for converting numbered keys into leds
static const uint8_t numkey_led_tab[8] = { LED_C, LED_D, LED_E, LED_F, LED_G, LED_A, LED_B, LED_C2 };

static void set_led_proto(uint8_t ledno, uint8_t *led_array)
{
	IFRANGE(ledno < MAX_LED) 
		led_array[ledno >> 3] |= 1 << (ledno & 7);
}

void set_led(uint8_t ledno)
{
	set_led_proto(ledno, leds);
}

void set_led_blink(uint8_t ledno)
{
	set_led_proto(ledno, blinkleds);
}

void set_led_dim(uint8_t ledno)
{
	set_led_proto(ledno, dimblinkleds);
}
/*
void set_led_dark(uint8_t ledno)
{
	set_led_proto(ledno, darkleds);
}
*/

void clear_led_proto(uint8_t ledno, uint8_t *led_array)
{
	IFRANGE(ledno < MAX_LED) 
		led_array[ledno / 8] &= ~_BV(ledno % 8);
}

void clear_led_blink(uint8_t ledno)
{
	clear_led_proto(ledno, blinkleds);
}

void clear_led_dim(uint8_t ledno)
{
	clear_led_proto(ledno, dimblinkleds);
}
/*
void clear_led_dark(uint8_t ledno)
{
	clear_led_proto(ledno, darkleds);
}
*/

void clear_led(uint8_t ledno)
{
	clear_led_proto(ledno, leds);
}

uint8_t is_led_proto(uint8_t ledno, uint8_t *led_array)
{
	IFRANGE(ledno < MAX_LED) 
		return led_array[ledno / 8] & _BV(ledno % 8);
	return 0;
}

uint8_t is_led_blink(uint8_t ledno)
{
	return is_led_proto(ledno, blinkleds);
}

int is_led_set(uint8_t ledno)
{
	return is_led_proto(ledno, leds);
}

void clear_all_leds(void)
{
	uint8_t i;
	for(i = 0; i < 5; i++)
	{
		leds[i] = 0;
		blinkleds[i] = 0;
		dimblinkleds[i]=0;
		dimleds[i]=0;
	}
}
/*
void show_quarter_note_leds(uint8_t triplets)
{
	if (triplets)
	{
		set_led_dark(LED_BANK1);
		set_led_dark(LED_BANK4);
		set_led_dark(LED_BANK7);
		set_led_dark(LED_BANK10);
	}
	else
	{
		set_led_dark(LED_BANK1);
		set_led_dark(LED_BANK5);
		set_led_dark(LED_BANK9);
		set_led_dark(LED_BANK13);
	}
}
*/
void set_single_proto(uint8_t num, uint8_t cnt, const uint8_t *tab, uint8_t *led_array)
{
	uint8_t z, i;
	for(i = 0; i < cnt; i++)
	{
		z = tab[i];
		if(num == i)
			set_led_proto(z, led_array);
		else
			clear_led_proto(z, led_array);
	}
}

void set_single_led(uint8_t num, uint8_t cnt, const uint8_t *tab)
{
	set_single_proto(num, cnt, tab, leds);
}

void set_single_blink(uint8_t num, uint8_t cnt, const uint8_t *tab)
{
	set_single_proto(num, cnt, tab, blinkleds);
}


void set_2bank_leds(uint8_t num, uint8_t dim)
{
	showingSteps= (dim!=255); 
	set_single_led(num, 16, bank_led_tab);
	set_single_proto(dim, 16, bank_led_tab , dimblinkleds); 
}


void set_current_index_led(void)
{
	uint8_t idx=pattern_play_index;
	uint8_t dim=idx;

	showingSteps=1; 
	
	if(!playing)
		idx=255; 
	set_single_led(idx, 16, bank_led_tab);

	while (pattern_buff[dim]!=END_OF_PATTERN && dim<patt_length)
		dim++;
	set_single_proto(dim, 16, bank_led_tab , dimleds);

	if(edit_mode& (EDIT_STEP_STOPPED|EDIT_STEP_RUNNING))
		dim=pattern_edit_index; 
	else 
		dim =255;
	set_single_proto(dim, 16, bank_led_tab , dimblinkleds);
}


void set_bank_leds(uint8_t num)
{
	showingSteps=0; 
	set_single_led(num, 16, bank_led_tab);
}


void set_bank_led(uint8_t num)
{
	IFRANGE(num < 16) 
		set_led(bank_led_tab[num]);
}

void set_bank_led_blink(uint8_t num)
{
	IFRANGE(num < 16) 
		set_led_blink(bank_led_tab[num]);
}

uint8_t is_bank_led_set(uint8_t num)
{
	IFRANGE(num < 16) 
		return is_led_set(bank_led_tab[num]);
	return 0;
}

uint8_t is_bank_led_blink(uint8_t num)
{
	IFRANGE(num < 16) 
		return is_led_blink(bank_led_tab[num]);
	return 0;
}

// key leds (all but tempo/bank)
void set_key_led(uint8_t num)
{
	IFRANGE(num < 24) 
		set_led(key_led_tab[num]);
}

/* set Key-LED if key is pressed */
void set_keypressed_led(uint8_t key)
{
	if (is_pressed( key))
		set_key_led(key );
	else
		clear_key_led(key);
}

void set_key_led_blink(uint8_t num)
{
	IFRANGE(num < 24) 
		set_led_blink(key_led_tab[num]);
}

void clear_key_led(uint8_t num)
{
	IFRANGE(num < 24) 
		clear_led(key_led_tab[num]);
}
/*
void clear_key_leds(void)
{
	uint8_t i;
	for(i = 0; i < 24; i++)
	{
		clear_led(key_led_tab[i]);
	}
}
*/
// numbered keys (bottom row 1 thru 8)
void set_numkey_led(uint8_t num)
{
	IFRANGE((num >= 1) && (num <= 8)) 
		set_led(numkey_led_tab[num - 1]);		// num is 1 thru 8
}

void set_single_numkey_led(uint8_t num)
{
	set_single_led(num - 1, 8, numkey_led_tab);
}

void clear_numkey_led(uint8_t num)
{
	IFRANGE((num >= 1) && (num <= 8)) 
		clear_led(numkey_led_tab[num - 1]);	// num is 1 thru 8
}

void set_numkey_led_blink(uint8_t num)
{
	IFRANGE((num >= 1) && (num <= 8)) 
		set_led_blink(numkey_led_tab[num - 1]);
}

uint8_t is_numkey_led_blink(uint8_t num)
{
	IFRANGE((num >= 1) && (num <= 8)) 
		return is_led_blink(numkey_led_tab[num - 1]);
	return 0;
}

uint8_t is_numkey_led_set(uint8_t num)
{
	IFRANGE((num >= 1) && (num <= 8)) 
		return is_led_set(numkey_led_tab[num - 1]);
	return 0;
}

// note keys (C thru C')
void set_notekey_led(uint8_t num)
{
	IFRANGE(num <= 12) 
		set_led(notekey_led_tab[num]);
}

void clear_notekey_led(uint8_t num)
{
	IFRANGE(num <= 12) 
		clear_led(notekey_led_tab[num]);
}

void set_notekey_led_blink(uint8_t num)
{
	set_single_blink(num, 13, notekey_led_tab);
}

uint8_t is_notekey_led_blink(uint8_t num)
{
	return is_led_blink(notekey_led_tab[num]);
}

void set_notekey_leds(uint8_t note)
{
	set_single_led(note, 13, notekey_led_tab);
}

void clear_notekeytab_leds(uint8_t n)
{
	set_single_led(255, n, notekey_led_tab);
	set_single_blink(255, n, notekey_led_tab);
}

// note leds (notes, U, D, RAS)
void clear_note_leds(void)
{
	clear_notekeytab_leds(18); 
}
void clear_notekey_leds(void)
{
	clear_notekeytab_leds(13);
}
void set_note_led(uint8_t note)
{
	int8_t	shift;	// our octave shift

	// if slide, turn on slide LED
	if(note & SLIDE)
		set_key_led(KEY_SLIDE);
	else
		clear_key_led(KEY_SLIDE);

	// if accent, turn on accent LED
	if(note & ACCENT)
		set_key_led(KEY_ACCENT);
	else
		clear_key_led(KEY_ACCENT);

	// figure out what led to light
	note &= 0x3F;
	if(note == REST_NOTE)
	{
		shift = 0;
		set_key_led(KEY_REST);
	}
	else
	{
		shift = -1;

		while(note > C2 || (shift < 0 && note == C2))
		{
			note -= OCTAVE;
			++shift;
		}

		clear_key_led(KEY_REST);
	}

	set_notekey_leds(note-C1);
	display_octave_shift(shift);
}



void clock_leds()
{
	uint8_t p,i;
	const  uint8_t __flash *dleds;

	if(!(dimblink_led_ctr%32) && showingSteps  && IS_SET1( SETTINGS1_HELPLED )  )
	{
		if( patt_length == 15)
			dleds=&darkleds_t[0];
		else
			dleds=&darkleds_q[0];
	}
	else 
		dleds=&darkleds_0[0];

	cli();
	cbi(LED_LATCH_PORT, LED_LATCH_PIN);
	
	for(i = 0; i < 5; i++)
	{
		p=leds[i];
		if(blink_led_on) 
			p ^= blinkleds[i];

		if(showingSteps)  // should be done not here ... but this is the smallest one... 
		{
			if (!(dimblink_led_ctr%4))
				p |= dimleds[i];		

			if (dimblink_led_on)
				p ^= dimblinkleds[i];
			else if (!(dimblink_led_ctr%4))
				p |= dimblinkleds[i];
			
			p |= *dleds++;
		}
		
		SPDR = p;
		while(!(SPSR & (1 << SPIF)))
			;
	}
	sbi(LED_LATCH_PORT, LED_LATCH_PIN);
	sei();
}


void clear_blinking_leds(void)
{
	uint8_t i;

	for(i = 0; i < 5; i++)
	{
		blinkleds[i] = 0;
		dimblinkleds[i] = 0;
	}
}
/*
void clear_dark_leds(void)
{
	uint8_t i;

	for(i = 0; i < 5; i++)
	darkleds[i] = 0;
}
*/

void display_octave_shift(int8_t shift)
{
	clear_led(LED_DOWN);
	if(shift == 2)
	{
		set_led_blink(LED_UP);
	}
	else
	{
		clear_led(LED_UP);
		clear_led_blink(LED_UP);

		if(shift == 1)
			set_led(LED_UP);
		else if(shift == -1)
			set_led(LED_DOWN);
	}
}
