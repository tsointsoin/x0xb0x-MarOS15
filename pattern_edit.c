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
#include <string.h>
#include <avr/interrupt.h>
#include "pattern.h"
#include "switch.h"
#include "led.h"
#include "main.h"
#include "eeprom.h"
#include "synth.h"
#include "delay.h"
#include "dinsync.h"
#include "midi.h"
#include "randomizer.h"

// input / bank-LED modes: 



uint8_t edit_mode; 

/*
 further (input) mode flags  
 "submodes"
*/
uint8_t		store_mode;				// while DONE pressed 
static uint8_t	randomizerMode;		
// extern uint8_t		tempoKnobMode;	// in pattern_play.c
// extern uint8_t		live_edit;		// in pattern_play.c







uint8_t		pattern_play_index;
uint8_t		pattern_edit_index;

uint8_t		patt_location;
uint8_t		patt_bank;
uint8_t		pattern_buff[PATT_SIZE+6];	// the 'loaded' pattern buffer
uint8_t		dirtyflag = 0;	// you filthy pattern, have you been modified?

static uint8_t	curUndoType;
static uint8_t	undoPatt[16];

// optimized table of sokkOS shuffling
static const uint8_t __flash shuffleTab[16] = { 6, 7, 8, 11, 12, 13, 14, 15, 1, 2, 3, 4, 5, 0, 9, 10 };
// if I dig hard I might find one more keyword to throw in this declaration... 


void clear_leds_and_note_off(void);

void edit_stop_fn(void)
{
	randomizerMode = 0;

	if(IS_SET2(SETTINGS2_KEEP_STEPMODE))
	{
		if(edit_mode&EDIT_STEP_RUNNING)
			edit_mode=EDIT_STEP_STOPPED;
		else 
			edit_mode=EDIT_NONE; 
		
		note_off(0);
	}
	else 
	{
		clear_leds_and_note_off(); 
		stop_editStepRunning();
		edit_mode=EDIT_NONE; 
	}
}

void edit_start_fn(void)
{
	if(IS_SET2(SETTINGS2_KEEP_STEPMODE))
	{
		if(edit_mode&EDIT_STEP_STOPPED )
			edit_mode=EDIT_STEP_RUNNING;
		else 
			edit_mode=EDIT_RUNNING;
	}
	else
	{
		stop_editStepStopped();
		edit_mode=EDIT_RUNNING;
	}
	note_off(0);
}


/***********************/
void do_pattern_edit(void)
{
	uint8_t i, curr_function;

	curr_function = function;

	// initialize
	if( ! IS_SET0(SETTINGS0_KEEP_PATT ))
		patt_location = 0;
	edit_mode = EDIT_NONE;
	pattern_play_index = 0;
	curr_note = 0;

	set_syncmode();
	start_fn=edit_start_fn;
	stop_fn=edit_stop_fn;

	patt_bank = 255;	// ensure the currently selected bank gets loaded

	while(1)
	{
		read_switches();

		if(function != curr_function)
		{
			// oops i guess they want something else, return!
			midi_stop();
			randomizerMode = 0;
			return;
		}

		if(patt_bank != bank)
		{
			stop_editStepStopped();
			patt_bank = bank;
			load_pattern(patt_bank, patt_location);
			set_bank_leds(bank);
		}

		// if they pressed one of the 8 bottom buttons (location select)
		if(!(edit_mode&EDIT_ALLMODES)) // none of the EDIT sub modes active 
		{
			// display whatever location is selected on number keys
			set_numkey_led(patt_location + 1);

			i = get_lowest_numkey_just_pressed();
			if(i != 0)
			{
				clear_notekey_leds();
				set_numkey_led(i);
				patt_location = i - 1;
				load_pattern(patt_bank, patt_location);
			}
#ifdef HASPATTERNCLRFILL			
			i=get_lowest_functionkey_just_pressed();

			uint8_t e=patt_length;
			uint8_t n=0;
			switch(i)
			{
				case KEYFN_REST:
					n=REST_NOTE; 
					break;
				case KEYFN_SLIDE :
					e=PATT_SIZE;
					n=END_OF_PATTERN;
					break; 
				case KEYFN_UP:
					n=C2;
					break; 
				case KEYFN_DOWN:
					n=C1;
					break;
				default:
					e=0;
					break;
			}
			for(i=0;i<e;++i)
			{
				pattern_buff[i]=n;
				dirtyflag=1;
			}
#endif 

		}
		edit_pattern();
	}	// while loop
}

void load_pattern(uint8_t bank, uint8_t patt_location)
{
	uint16_t	pattern_addr;

	pattern_addr = PATTERN_MEM + bank * BANK_SIZE + patt_location * PATT_SIZE;

	spieeprom_read(pattern_buff,pattern_addr,PATT_SIZE);
	dirtyflag = 0;
}

void write_param_preset( uint8_t idx )
{
	uint8_t		i;
	uint16_t  preset_addr = PRESET_MEM + PRESET_SIZE * idx;
    uint8_t buf[PRESET_SIZE_USED];
 
    // 2 bytes magic number (together 0x441 = 1089 )
    buf[0] = 0x4;
    buf[1] = 0x41;
 
    uint8_t *b = buf + 2;
    for (i=1; i<11; ++i)
        *b++ = *tk_paras[i];
    
	spieeprom_write(buf, preset_addr,PRESET_SIZE_USED);
	internal_eeprom_write8(TEMPO_EEADDR_L, tempo);
}

void read_param_preset( uint8_t idx )
{
	uint8_t		i;
	uint16_t preset_addr = PRESET_MEM + PRESET_SIZE * idx;
    uint8_t buf[PRESET_SIZE_USED];

	spieeprom_read(buf,preset_addr,PRESET_SIZE_USED); 

    if (buf[0] == 0x4 && buf[1]==0x41)
    {
        uint8_t *b = buf + 2;
        for (i=1; i<11; ++i)
            *tk_paras[i] = *b++;
    }
}


void write_pattern(uint8_t bank, uint8_t patt_location)
{
	uint16_t	pattern_addr;
	pattern_addr = PATTERN_MEM + bank * BANK_SIZE + patt_location * PATT_SIZE;
	// modify the buffer with new data
	spieeprom_write(pattern_buff, pattern_addr, PATT_SIZE);
	internal_eeprom_write8(TEMPO_EEADDR_L, tempo);
}

uint8_t inc_dec_pattern_idx(uint8_t *idx, uint8_t next)
{
	if(!next) //previous: 
	{	//		get previous pattern position
		if(*idx == 0)
		{
			// search thru the buffer -forward- to find the EOP byte
			*idx=findEOP();
			if(*idx>=patt_length) // maximum number of steps, so there is no (valid) EOP byte 
				*idx=patt_length-1;  // than set to last active step 
			return 0;	// wrapped 
		}
		else
			(*idx)--;
	}
	else	//	next: 
	{	//		get next note from pattern
		if(((*idx + 1) >= patt_length) || (pattern_buff[*idx] == END_OF_PATTERN))
		{
			*idx = 0;
			return 0;	// wrapped
		}
		else
			(*idx)++;
	}
	return 1;	// not wrapped 
}

void mdAccents(uint8_t i, uint8_t is)
{
	is |= pattern_buff[i] & SLIDE; //add slide if exists

	if( randomIf(30))
		is |= ACCENT;
	pattern_buff[i] = is;
}


/*
	Randomizer modification functions 
*/


void rndm_setC2(uint8_t idx, uint8_t note)
{
	uint8_t n=IS_SET2(SETTINGS2_FILLWITHC2)? C2:REST_NOTE; 
	pattern_buff[idx] = n;
}

void rndm_accent(uint8_t idx, uint8_t note)
{
	if(randomIf(30))
		pattern_buff[idx] |= ACCENT;
	else
		pattern_buff[idx] &= ~ACCENT;
}
void rndm_slide(uint8_t idx, uint8_t note)
{
	if(randomIf(30))
		pattern_buff[idx] |= SLIDE;
	else
		pattern_buff[idx] &= ~SLIDE;
}

void rndm_octUpDown(uint8_t idx, uint8_t note)
{
	if(randomIf(25))
		if(note <= C4) // D5_SHARP is highest possible note, C5 highest note which can be entered.
			pattern_buff[idx] += OCTAVE;
	if(randomIf(25))
		if(note >= C2)   // C1 is lowest possible note
			pattern_buff[idx] -= OCTAVE;
}
void rndm_justOctaves(uint8_t idx, uint8_t note)
{
	uint8_t key = (note - C1 ) % 12;
	pattern_buff[idx] -= key;
}

static int8_t extreme; 
static uint8_t minmax;
void rndm_findHighest(uint8_t idx, uint8_t note)
{
	if(extreme < 0)
		minmax=0; 

	if(note >= minmax && note >= C2)			//prefers later now, but smaller
	{
		extreme = idx;
		minmax = note;
	}
}
void rndm_findLowest(uint8_t idx, uint8_t note)
{
	if(extreme < 0)
		minmax=C4;

	if( note <= minmax && note <= C4)			//prefers later now, but smaller
	{
		extreme = idx;
		minmax = note;
	}

}

static int8_t rndm_shift; 
void rndm_transpose(uint8_t idx, uint8_t note)
{
	int8_t n; 
	n=rndm_shift;
	n+=note;
	if(n>= LOWESTNOTE && n<= HIGHESTNOTE )
		pattern_buff[idx] += rndm_shift;
}



/*
  Randomizer loop function 
  called with one of the above mod functions as parameter 
*/ 

void rndm_loop(void func(uint8_t,uint8_t), uint8_t p_len)
{
	uint8_t note; 
	for(uint8_t i = 0; i < p_len; i++)
	{
		note=pattern_buff[i]&NOTE_MASK; 
		if(note!= REST_NOTE && note!= EOP_NOTE)
				func(i,note); 
	}
}

void transposePattern(int8_t shift, uint8_t p_len)
{
	rndm_shift=shift; 
	rndm_loop(rndm_transpose, p_len);
}

void setDoneLed(void)
{
	clear_led_dim(LED_DONE);
	if(dirtyflag)
	{
		set_led_blink(LED_DONE);
	}
	else
	{
		clear_led_blink(LED_DONE);
		if(edit_mode &(EDIT_STEP_STOPPED|EDIT_STEP_RUNNING)) // Step modes active, which could be ended by DONE key 
			set_led_dim(LED_DONE);
	}

}

void edit_pattern()
{
	static uint8_t inhibit;
	static uint8_t auto_inc=0;
	static uint8_t end_pattern=0;
	static uint8_t rememberStep12=0;

	uint8_t i;
	uint8_t next, prev;
	uint8_t p_len;



	p_len=findEOP();

	// handle all start / stop inputs
	start_stop();
		

	if(just_pressed(KEY_TEMPO) && !is_pressed(KEY_CHAIN))
	{
		if(patt_length == 15)
		{
			patt_length = 16;
			pattern_buff[patt_length - 1] = 0;
			pattern_buff[12] = rememberStep12;
		}
		else
		{
			patt_length = 15;
			pattern_buff[patt_length] = 0x01; // <---????? 
			rememberStep12 = pattern_buff[12];
			pattern_buff[12] = END_OF_PATTERN; // lets have a one bar pattern by default
		}

		dirtyflag = 1;
	}

	if(edit_mode&(EDIT_RUNNING|EDIT_STEP_RUNNING))
	{
		if(just_pressed(KEY_CHAIN))
		{
			randomizerMode = !randomizerMode;
			clear_led(LED_CHAIN);
		}

		if(randomizerMode)
		{
			if(edit_mode&EDIT_STEP_RUNNING)
				stop_editStepRunning();

			set_led(LED_CHAIN);
			
			i=get_lowest_functionkey_just_pressed(); 
			if(i)
			{
				uint8_t undo_ed = 0;

				uint8_t pattern_copy[PATT_SIZE];
				memcpy(pattern_copy, pattern_buff, PATT_SIZE);

				switch(i)
				{
					case KEYFN_C:
					case KEYFN_CS:
					case KEYFN_D:
					case KEYFN_DS:
					case KEYFN_E:
						randomize(--i, p_len);
						break;
					case KEYFN_F:		//UNDO
#ifdef HASDOUBLECLICKF
						if(doublePressTimeOut > 0)
						{
							rndm_loop(rndm_setC2,p_len); 
						}
						else 
#endif 
						if(curUndoType > 0)
						{
							curUndoType = 0;
							memcpy(pattern_buff, undoPatt, p_len);
							undo_ed = 1;
						}
						else
						{
							load_pattern(bank, patt_location);
						}
#ifdef HASDOUBLECLICKF
						doublePressTimeOut = 250;
#endif
						break; 
					case KEYFN_FS:
						randomizeNotes( p_len);		// notes only!
						break;
					case KEYFN_G: // some Octave up/down shifting 
						rndm_loop(rndm_octUpDown,p_len);
						break;
					case KEYFN_GS:
						rndm_loop(rndm_justOctaves,p_len);		
						break;
					case KEYFN_A:
						extreme=-1; 
						rndm_loop(rndm_findHighest,p_len);		
						if(extreme >= 0)
							pattern_buff[extreme] -= OCTAVE;
						break;
					case KEYFN_AS:
						extreme=-1; 
						rndm_loop(rndm_findLowest,p_len);
						if(extreme >= 0)
							pattern_buff[extreme] += OCTAVE;
						break;
					case KEYFN_B:		// -oct shift = all notes in pattern 1 oct down
						transposePattern(-OCTAVE,p_len);
						break;
					case KEYFN_C2:	// +oct shift = all notes in pattern 1 oct up
						transposePattern(OCTAVE,p_len);
						break;
					case KEYFN_REST:
						{
							for(uint8_t i = 0; i < p_len; i++)
							{
								uint8_t note=*(pattern_copy + shuffleTab[i]);
								if(note!=END_OF_PATTERN) // don't shuffle EOPs in
									pattern_buff[i] = note;
							}
						}
						break;
					case KEYFN_ACCENT:
						rndm_loop(rndm_accent,p_len);
						break;
					case KEYFN_SLIDE:
						rndm_loop(rndm_slide,p_len);
						break;
					case KEYFN_PREV:
						simplifyByLoop( p_len);
						break;
					case KEYFN_NEXT:
						{	// new random lenght
							while(p_len<PATT_SIZE&& p_len<patt_length)
							{	// remove all EOPs
								if(pattern_buff[p_len]==END_OF_PATTERN)
									pattern_buff[p_len] = C2;			//what else? we don`t have nonxox´ lossless patterns
								p_len++;
							}
							p_len=1 + random_x(patt_length-1);
							pattern_buff[p_len]=END_OF_PATTERN;
						}
						break;
					case KEYFN_DOWN:		// -shift = all notes in pattern 1 semi down
						transposePattern(-1,p_len);
						break;
					case KEYFN_UP:
						transposePattern(1,p_len);
						break;
				} // switch "loop_key" (here: function_key) 

				for(uint8_t i = 0; i < PATT_SIZE; i++)
				{
					if(pattern_copy[i] != pattern_buff[i] && !undo_ed)	//something has changes
					{
						memcpy(undoPatt, pattern_copy, PATT_SIZE);
						curUndoType = 1;
						dirtyflag = 1;
						break;
					}
				}
			}  // if randomizer function key just pressed

			if(just_pressed(KEY_DONE))		// leave mode, user wants to store!
			{
				randomizerMode = 0;
				clear_led(LED_CHAIN);
			}
			else// don't do any other keys in randomizer-mode
			{	
				setDoneLed(); 
				return; 
			}
		}
	}


	if(edit_mode&EDIT_ALLMODES)
	{
		uint8_t index;
		uint8_t curr_note;

		if(edit_mode&EDIT_RUNNING) // edit the step currently played by the sequencer
			index = pattern_play_index;
		else				//  edit the step selected by "cursor"
			index = pattern_edit_index;

		curr_note = pattern_buff[index] & NOTE_MASK;

		if((curr_note != EOP_NOTE))
		{
			// don't accent or slide 'done' notes, duh!
			if(just_pressed(KEY_ACCENT))
			{
				pattern_buff[index] ^= ACCENT ;
				dirtyflag = 1;	// changed
			}

			if(just_pressed(KEY_SLIDE))
			{
				pattern_buff[index] ^= SLIDE;
				dirtyflag = 1;	// changed
			}
		}

		if(is_pressed(KEY_REST) && is_pressed(KEY_ACCENT) && is_pressed(KEY_SLIDE))
		{
			end_pattern=1; 
			// shorten the pattern length to current index position
//			if(index < patt_length) // would be really bad it that ever goes beyond 
			{
				clear_led(LED_REST);
				clear_led(LED_SLIDE);
				clear_led(LED_ACCENT);
				pattern_buff[index] = END_OF_PATTERN;
				dirtyflag = 1;
			}
		}
		else if(end_pattern && no_keys_pressed())
			end_pattern=0;
		else if(end_pattern==0)	
		{
			int8_t	shift;

			// rests/dones are middle octave (if you hit a note key next)
			// .. and they can't get any octave shift 
			if((curr_note == EOP_NOTE) || curr_note==REST_NOTE) 
				shift = 0;
			else
			{
				if(curr_note < C2)
					shift = -1;
				else if (curr_note <=C3)
					shift= 0;
				else if(curr_note <= C4)
					shift =1; 
				else 
					shift=2; 
				
				if( just_pressed(KEY_UP) && shift < ( IS_SET0(SETTINGS0_NOHIGHOCT)?1:2 ) )
					curr_note += OCTAVE;
				if( just_pressed(KEY_DOWN) && shift > -1)
					curr_note -= OCTAVE;
			}

			if(just_pressed(KEY_REST))
			{
				curr_note = REST_NOTE;
			}

			if(	(i=get_lowest_notekey_just_pressed()) != 0)
				curr_note = shift * OCTAVE + i + C2-1;
				
			for(i = 0; i < 14; i++)
			{	
				if( just_released(loopkey_tab[i]) && IS_SET0(SETTINGS0_AUTO_INC))
					auto_inc=1; 
			}

			// keep SLIDE and ACCENT 
			if(curr_note== EOP_NOTE || p_len!=index ) // but not if we have a new note on the EOP position! 
				curr_note |= (pattern_buff[index] & (SLIDE|ACCENT));
			// ATN: also and intentionally this converts EOP_NOTE back to END_OF_PATTERN (... if index==p_len) 
			

			if(curr_note != pattern_buff[index])	//changed!
			{
				// if the note changed!
				pattern_buff[index] = curr_note;
				dirtyflag = 1;			// changed
				if(edit_mode&EDIT_STEP_STOPPED)	// restrike note
				{
					note_off(0);
					delay_ms(1);
					note_on(curr_note &~SLIDE);		// no slide
					//midi_send_note_on(curr_note);
				}
			}

			if(edit_mode&(EDIT_STEP_STOPPED|EDIT_STEP_RUNNING)) // Step edit modes 
			{
				// in runwrite mode the player in do_tempo() cares for the LEDs 
				if(curr_note != END_OF_PATTERN)
				{
					set_note_led(curr_note);
				}
				else
				{
					clear_note_leds();
				}
			}
#ifdef AUTO_INC_RUNNING
			else 
				auto_inc=0;		// never auto_inc when in runwrite, but do in both stepwrites (R/S)  
#else
			if(!(edit_mode&EDIT_STEP_STOPPED) )		// auto inc only when stopped 
				auto_inc=0; 
#endif 
		}
	} 

	/* Backwards rotating... */
	if( (next=is_pressed(KEY_NEXT)) && just_pressed(KEY_PREV))
	{
		uint8_t i = 0;
		uint8_t first = pattern_buff[0];
		while(i < p_len - 1)
		{
			pattern_buff[i] = pattern_buff[i + 1];
			i++;
		}

		pattern_buff[i] = first;
		dirtyflag = 1;
		inhibit=1; 
	}
	else if( (prev=is_pressed(KEY_PREV)) && just_pressed(KEY_NEXT) )
	{	// forward rotating step 16=>1, 1=>2 ... 
		// uint8_t i = 0;
		// uint8_t new = pattern_buff[p_len - 1];
		// while(i < p_len)
		// {
			// pattern_buff[i]=new; 
			// i++;
			// new=pattern_buff[i];
		// }
		
		uint8_t i = p_len;
		uint8_t last = pattern_buff[i];
		while (i > 0)
		{
			pattern_buff[i] = pattern_buff[i - 1];
			i--;		
		}
		pattern_buff[0] = last;
		
		dirtyflag = 1;
		inhibit=1;
	}
	else if(inhibit && !next && !prev)	// prev can only be undefined when next is true - so no problem. 
	{
		inhibit=0; 
	}
	else if( !inhibit && ( (next=just_released(KEY_NEXT)) || just_released(KEY_PREV)  || (next=auto_inc) ) ) // c is guaranteed for short evaluation! 
	{
		auto_inc=0; 
		// NEXT or PREVIOUS:  
		// if not in one of the step modes: start them. 
		// Else move the index around, optionally: end step modes on wrap.
		// ignore keys, when they have been used for rotating the buffer
		// ... that is why they must trigger on release..	
		
		if(edit_mode & EDIT_STEP_STOPPED)
		{
			// turn off the last note
			note_off(curr_note & SLIDE);
			//midi_send_note_off(curr_note); 
			delay_ms(1);
			prev_note=curr_note; 
		
			if(inc_dec_pattern_idx(&pattern_edit_index,next ) || !STEP17)
			{
				curr_note = pattern_buff[pattern_edit_index];

				if(curr_note == END_OF_PATTERN)
				{
					clear_led(LED_ACCENT);
					clear_led(LED_SLIDE);
				}
				else
				{
					note_on( (curr_note &~SLIDE) | (prev_note&SLIDE) ); 	// slide from last note 
					//midi_send_note_on(curr_note);
					set_note_led(curr_note);
				}
			}
			else
				stop_editStepStopped();
		}
		else if( (edit_mode&EDIT_ALLMODES)==EDIT_NONE ) // start EDIT_STEP_STOPPED, if not running 
		{

			edit_mode = EDIT_STEP_STOPPED;
			start_editStep(next); 
			note_off(0);
			//midi_notesoff();

			curr_note = pattern_buff[pattern_edit_index];
			if(curr_note != END_OF_PATTERN)
			{
				note_on(curr_note &~SLIDE); 	 // no slide on first note
				//midi_send_note_on(curr_note);
				set_note_led(curr_note);

			}
		}
		else if(edit_mode&EDIT_STEP_RUNNING)
		{
			if(inc_dec_pattern_idx(&pattern_edit_index,next)==0 && STEP17)
				stop_editStepRunning();
		}
		else // start EDIT_STEP_RUNNING
		{
			edit_mode = EDIT_STEP_RUNNING;
			start_editStep(next);
		}
		set_current_index_led();
	}

	// if they hit done, save buffer to memory
	if(just_pressed(KEY_DONE))
	{
		stop_editStepStopped();
		stop_editStepRunning();
		clear_led_blink(LED_DONE);
		set_led(LED_DONE);
		store_mode=1;
		clear_note_leds(); 
		set_numkey_led_blink(patt_location+1);
		while(is_pressed(KEY_DONE))
		{
			read_switches();
			patt_bank = bank;
			i = get_lowest_numkey_pressed();
			if(i != 0)
			{
				clear_note_leds();
				set_numkey_led(i);
				
				patt_location = i - 1;
				store_mode=2; 
			}
		}
		clear_note_leds(); 
		if(store_mode==2) 
		{
			write_pattern(patt_bank, patt_location);
			dirtyflag = 0;	// not dirty anymore, saved!
		}
		store_mode=0;
		clear_led(LED_DONE);
	}
	setDoneLed(); 
}

void edit_live()
{
	if(edit_mode==EDIT_NONE)
		edit_mode=EDIT_RUNNING;
	edit_pattern();
}

void clear_leds_and_note_off()
{
	clear_note_leds();
	set_bank_leds(bank);
	note_off(0);
	//midi_notesoff();
}


void stop_editStepStopped()
{
	if(edit_mode&EDIT_STEP_STOPPED)
	{
		edit_mode = EDIT_NONE;
		clear_leds_and_note_off();
		clear_led_dim(LED_NEXT);
		clear_led_dim(LED_PREV);
	}
}
void stop_editStepRunning()
{
	if(edit_mode&EDIT_STEP_RUNNING)
		edit_mode = EDIT_RUNNING;
	else 
		edit_mode = EDIT_NONE;
	pattern_edit_index = 0;
	clear_led_dim(LED_NEXT);
	clear_led_dim(LED_PREV);
}

void start_editStep(uint8_t next)
{
	set_led_dim(LED_NEXT);
	set_led_dim(LED_PREV);
	pattern_edit_index = 0;
	if(!next)	// PREV pressed => select last idx
		inc_dec_pattern_idx(&pattern_edit_index,0);
}
