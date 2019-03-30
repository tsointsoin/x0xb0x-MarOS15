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
#include <string.h>
#include "pattern.h"
#include "switch.h"
#include "led.h"
#include "main.h"
#include "synth.h"
#include "delay.h"
#include "dinsync.h"
#include "midi.h"


uint8_t		tempoKnobMode;
uint8_t		scaleCorrType;
uint8_t		scaleCorrRoot;
uint8_t     variationOn = 1;

/* Pitch shift variables:
 * These define the 'current' pitch shift (for the currently playing pattern)
 * and the 'next' pitch shift, which will take effect on the next loop 
 * A pitch shift of 0 means no shift, -12 is octave down, 12 is octave up, etc.
 */
int8_t		curr_pitch_shift = 0;
int8_t		next_pitch_shift = 0;

uint8_t		curr_chain[MAX_CHAIN];
uint8_t		curr_chain_index;
uint8_t		next_chain[MAX_CHAIN];
uint8_t		buff_chain[MAX_CHAIN];
uint8_t		buff_chain_len = 0;




uint8_t		patt_length = PATT_SIZE;
uint8_t		all_accent = 0;
uint8_t		all_slide = 0;
uint8_t		all_rest = 0;			
uint8_t		curr_bank = 0;
uint8_t		next_bank = 0;
uint8_t		swingPercent = 0;		//  swing 0...100%
uint8_t		gateLen = 18;			//  adjustable gate length
static uint8_t		live_edit = FALSE;
uint8_t		loop = FALSE;
uint8_t		loop_start = 1;
uint8_t		loop_end = PATT_SIZE;
/* 8th note stuff */
uint8_t		eighths = FALSE;

void (*start_fn) (void) ;
void (*stop_fn) (void) ;


static uint8_t	loop_countdown = FALSE;
static int8_t	countdown = 0;
static uint8_t	prev_pattern_index;

static uint8_t	playingChange;	//  for stop or start by midi notes 
static uint8_t	latch;			// midi note triggered latch active? 
static uint8_t	numHeldNotes;

static const uint8_t __flash midiToPatt[13] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6, 7 };

// little helper to squeeze out some bytes 
void cpy_chain(uint8_t *dest, uint8_t *source)
{
	memcpy(dest,source,MAX_CHAIN);
}
void cpy_curr_chain(void)
{
	cpy_chain((uint8_t*)curr_chain, (uint8_t*)next_chain);
}

void all_ras(uint8_t key, uint8_t *which)
{
	if(just_pressed(key))
	{
		*which = 0xff; // must be 0xff, as it might be evaluated with "if(*which&SLIDE)" or "if(*which&ACCENT)" ...  
	}
	if(just_released(key))
	{
		*which =0;
	}
	set_keypressed_led(key);
}

/*
*	Midi functions in Pattern Play
*
*	NOTE OFF 
*
*/
void pattern_note_off(uint8_t note)
{
	if(numHeldNotes)
	{
		//pattern keys and transpose keys count for running state
	//	if(isTriggerNote(note))
		if(note > 35 && note < 97 && note != 37 && note != 39 && note != 42)
		{
			numHeldNotes--;
			if(!numHeldNotes)
				playingChange = 2;
		}
	}
}
/*
*	NOTE ON
*/
void pattern_note_on(uint8_t note, uint8_t velocity)
{	// ATN: velocity 0 calls note off, not us! 

	// note 0..35 scale correction mode
	if(note < 36)	//TODO turn off if not in midiS
	{
		scaleCorrType = note / 12; // 0,1,2  == off, major, minor
		scaleCorrRoot = note % 12;
	}
	// latch key, stop key, only look for noteOns
	else if(note == 37)			//stop
	{
		if(numHeldNotes)
		{
			numHeldNotes = 0;
			playingChange = 2;
		}
	}
	else if(note == 39)			// latch
	{
		if(!latch)
		{
			latch = 1;
			if(numHeldNotes)
			numHeldNotes++;
		}
		else
		{
			latch = 0;
			if(numHeldNotes)
			{
				numHeldNotes--;
				if(!numHeldNotes)
				playingChange = 2;
			}
		}
	}
	// note 36..48 - 8 patterns to select on white keys
	else if(note > 35 && note < 49)
	{
		if(!numHeldNotes)
		{
			playingChange = 1;
			if(latch)
				numHeldNotes++;
		}

		numHeldNotes++;

		uint8_t pattIdx = 0;

		pattIdx = midiToPatt[note - 36];

		if(!tempoKnobMode)
			clear_all_leds();

		next_chain[0] = curr_chain[0] = pattIdx;
		next_chain[1] = curr_chain[1] = 0xFF;

		load_pattern(curr_bank, curr_chain[0]);

		// fix for EOP
		if(pattern_play_index > findEOP())
			pattern_play_index = 0;
	}	// note 49..96
	else if(note > 48 && note < 97)
	{
		curr_pitch_shift = next_pitch_shift = note - 60;
		if(!numHeldNotes)
		{
			playingChange = 1;
			if(latch)
				numHeldNotes++;
		}

		numHeldNotes++;
	}
}
/*
*	MIDI CC (Controller) 
*/
void pattern_controller(uint8_t data1, uint8_t data2)
{
	switch(data1)
	{
		// case 120: // all sound off
		case 123:	// all notes off
		numHeldNotes = 0;
		playingChange = 2;
		break;
		case 1: // ModWheel to GateLen
		{
			uint8_t val = data2 / 3;
			if (val>38) val = 38;
			gateLen = val;
		}  break;
							
		case 2: //cc2 to turn off/on variations
		variationOn = data2 > 63 ? 1 : 0;
		break;
	}
}

void pattern_progChange(uint8_t c)
{
	uint8_t midiChain, midiBank; 
	
	midiBank= c / 8;
	if(midiBank!=bank)
	{
		next_bank = midiBank;
		if(!playing)
			curr_bank = next_bank;
	}

	midiChain=c%8; 
	
	clear_notekey_leds();
	buff_chain[0] = next_chain[0] = midiChain ;
	buff_chain[1] = next_chain[1] = 0xFF;

	if(!playing)
	{
		cpy_curr_chain();
		set_bank_leds(next_bank);
//		curr_pitch_shift = next_pitch_shift;
	}
}

void play_start_fn(void)
{
	if(has_bank_knob_changed())
	{		//on start!!
		load_pattern(bank, curr_chain[0]);
	}
	else
	{
		load_pattern(curr_bank, curr_chain[0]);
	}
}
void play_stop_fn(void)
{
	; 
}

void start_stop(void)
{
	uint8_t cmd; 
	
	cmd=midi_realtime_cmd;
	midi_realtime_cmd=0;
	
	if(!IS_SET1(SETTINGS1_STARTMODE) && sync ==MIDI_SYNC)
	{
		if( cmd != MIDI_STOP )
			cmd = 0;

		if(playing)	
		{
			if(playingChange == 2 )
				cmd = MIDI_STOP;
		}
		else
		{
			if(playingChange == 1 )
				cmd = MIDI_START;
		}
	}
	else 
		playingChange=0; 

#ifdef DIN_SYNC_IN
	if(sync == DIN_SYNC)
	{
		if(playing && !DINSYNC_IS_START )
			cmd = MIDI_STOP;
		if(!playing && DINSYNC_IS_START )
			cmd = MIDI_START;
	}
#endif
	
	if(just_pressed(KEY_RS))
	{
		if(		sync==INTERNAL_SYNC
			||	tempoMeasure==0xff 
			||	(		midi_run==0 
#ifdef DIN_SYNC_IN
					&&	0==(DINSYNC_PINS & (1<< DINSYNC_START))
#endif 
				) 
		)
		{
			if(playing)
				cmd = MIDI_STOP;
			else
				cmd = MIDI_START;
		}
		else if(	sync==MIDI_SYNC 
#ifdef DIN_SYNC_IN
				|| sync == DIN_SYNC
#endif
			   )
		{
			if(sync_startstop)
			{
				sync_startstop=0;
			}
			else if(!playing)
			{
				sync_startstop=1; 
			}
			else
			{
				sync_startstop=-1;
			}
		}
	}
	
	

	
	if	(playing && cmd == MIDI_STOP  )
	{	//	STOP ! 

		stop_fn(); 		
		
		playing = FALSE;
		
		// pattern play 
		numHeldNotes = 0;
		loop = FALSE;
		loop_countdown = FALSE;
		loop_start = 1;
		loop_end = patt_length;
		countdown = 0;


		note_off(0);
		midi_stop();
		dinsync_stop();


		clearPendingDinPulses();

		clear_led(LED_RS);
		clear_note_leds();
		
		if (!tempoKnobMode )
		{	
			if( IS_SET2(SETTINGS2_KEEP_STEPMODE) && (edit_mode&EDIT_STEP_STOPPED))
				set_current_index_led(); 
			else
				set_bank_leds(bank);
		}
	
	}	//Stop!
	
	if( !playing && cmd == MIDI_START)
	{	// START ! 

		start_fn();

		set_led(LED_RS);

		curr_note = REST_NOTE;

		curr_chain_index = 0;			// index into current chain
		note_counter = 0;
		pattern_play_index = 0;			// index into current pattern in chain
		curPatternRun = 0;		

		clearPendingDinPulses();


		if( playingChange == 1)
		{
			// if we start on a note in midi sync the note is usually just behind the clock. So we would be one clock late
			cli();
			run_tempo = 2; // simulate one Midi clock 
			sei();
		}
		else
		{
			cli();
			run_tempo = 0;
			sei();
		}

		swing_it = 0;
		playing = TRUE;

#ifdef SYNC_OUT
		midi_putchar(MIDI_START);
		dinsync_start();
#endif
	}	//start
}

/*
*	Set the proper sync mode on "mode"-function startup 
*/
void set_syncmode(void)
{
	if(sync == INTERNAL_SYNC)
		turn_on_tempo();

#ifdef DIN_SYNC_IN
	if(sync == DIN_SYNC)
		dinsync_set_in();
	else
		dinsync_set_out();
#endif
}


void do_patterntrack_play(void)
{
	uint8_t i = 0, curr_function;
	uint8_t start_point = 0;
	uint8_t end_point = 0;

	uint8_t no_loop = FALSE;

	curr_function = function;

	set_syncmode(); 

	if( IS_SET0(SETTINGS0_KEEP_PATT) )
		next_chain[0] = curr_chain[0]=patt_location;
	else
		next_chain[0] = curr_chain[0] = 0;
	
	next_chain[1] = curr_chain[1] = 0xFF;

	curr_chain_index = 0;

	live_edit = FALSE;
	loop = FALSE;
	loop_countdown = FALSE;
	loop_start = 1;
	loop_end = patt_length;
	countdown = 0;

	next_bank = curr_bank = bank;

	set_bank_leds(bank);


	start_fn=play_start_fn; 
	stop_fn=play_stop_fn; 

	// set up the local sub functions for do_midi()
	if (sync == MIDI_SYNC)
	{
		noteOnFunc=pattern_note_on;
		noteOffFunc=pattern_note_off;
		controllerFunc=pattern_controller;
		progChangeFunc=pattern_progChange; 
	}

	while(1)
	{
		read_switches();
		playingChange = 0;	// to store a required change in playing status

		if( function != curr_function)
		{
			tempoKnobMode = 0;

			if( function == EDIT_PATTERN_FUNC)
			{	// "hot" change to edit pattern mode during run  
				live_edit = TRUE;
				clear_note_leds(); // reset indication of currently played pattern. 
			}

			if( !playing)
			{	// end pattern play 
				patt_location=curr_chain[curr_chain_index];
				live_edit = FALSE;
				midi_stop();
				all_accent = all_rest = all_slide = swing_it = 0;
				return;
			}
			curr_function = function;
		}

		if(live_edit)
		{
			if( function == EDIT_PATTERN_FUNC)
			{
				patt_location = curr_chain[curr_chain_index];	// edit what you just hear 
				edit_live();
				if( IS_SET0(SETTINGS0_LIVE_SETCHAIN) )  
					curr_chain[curr_chain_index]=patt_location; // replace if stored in different location. 
					// if not stored - or in same location - this simply does nothing. 
			}
			else   // Back from live edit (mode knob turned) 
			{
				live_edit = FALSE;
				edit_mode= EDIT_NONE;
				clear_all_leds();
				set_led(LED_RS);
			}
		}
		else	//PATTERN PLAY!
		{
			uint8_t enteredMode = 0;

			do_midi();		// evaluate midi (notes, CC, PB all non real-time ) 
			start_stop();	// check for start stop command (R/S button, midi real time) 

			if(!tempoKnobMode)
				if(just_pressed(KEY_TEMPO) && !is_pressed(KEY_DONE))
					enteredMode = 7; // start with gate len 

			if(tempoKnobMode || enteredMode) // either tempo knob mode active - or it should be started 
			{
				if(bank != curr_bank)	// moving bank knob ends tempo knob mode 
                    tempoKnobMode = 0;
				
                if(just_pressed(KEY_TEMPO) && !enteredMode)	// toggle tempo knob mode to off if button pressed *again*  (not still!) 
				{
					clear_all_leds();
					if(playing)
                        set_led(LED_RS);
					tempoKnobMode = 0;
				}

				set_led(LED_TEMPO);	// Indicating Tempo Knob mode 
				
                if(just_pressed(KEY_C2))	// reset to default values
				{
					for(i=1;i<NUM_KNOB_PARAMS-1;++i)
						*tk_paras[i]=0;		// mostly harmless ... err.. zero  
                    *tk_paras[i]=15; // <-- variLastStep = 15;
					gateLen = 18;
					enteredMode=tempoKnobMode; // dirty  hack saves us some bytes:
					//							=> the following for(NUM_KNOB_PARAMS) will display the just changed value!
				}

				// Load 'n store: presets for tempo knob valuesbank
				
				set_keypressed_led(KEY_DONE);		// give visual feedback if key pressed
				for(i=0;i< 7 /*sizeof(numkey_tab)*/;++i)
				{
					set_keypressed_led( numkey_tab[i]);	// give visual feedback
					
					if( just_pressed( numkey_tab[i] ) ) // newly pressed "preset place" key
					{
						if (is_pressed(KEY_DONE))		// together with CHAIN key
							write_param_preset( i );	// then store
						else							// else load
						{
							uint8_t gt = gateLen;
							uint8_t sw = swingPercent;
							read_param_preset( i );
							if (IS_SET1(SETTINGS1_PRESERVE_TIMING))
							{
								gateLen = gt;
								swingPercent = sw;
							}
							enteredMode=tempoKnobMode; // same trick as explained above.. 
						}
					}
				}
                
				for(i=1;i<NUM_KNOB_PARAMS;++i)
				{
					if(just_pressed(tk_fnkey[i]) || i==enteredMode )
					{
						clear_key_led(tk_fnkey[tempoKnobMode]);	// clear previous LED, or 0=LED_DONE on entering:  wrong, but does no harm.
						set_key_led(tk_fnkey[i]);
						tempoKnobMode=i;
						tempoval_led();
					}
				}
				
				// 'tap tempo' requests by timing between KEY_CHAIN strikes
				if(just_pressed(KEY_CHAIN) )
				{	
					uint16_t tt; 
					cli();
						tt=tapTempoTimer;
						tapTempoTimer=0;
					sei(); 
					
					if(tt) // no div by zero
					{
						tt = 60000UL / tt;	// convert time in ms to BPM
						if(tt>MIN_TEMPO && tt<MAX_TEMPO)  // check for range 
							change_tempo(tt);			  // and only set if it is in range
					}
				}	
			}

			if(!tempoKnobMode) 
			{
				if( is_pressed(KEY_CHAIN))
				{
					// start a new chain if just pressed
					if(just_pressed(KEY_CHAIN))
					{
						clear_notekey_leds();
						set_led(LED_CHAIN);
						buff_chain_len = 0; // 'start' to write a new chain
					}
					
					if( just_pressed(KEY_PREV))
					{
						pattern_play_index = loop_start - 1;
					}


					// ok lets add patterns/tracks to the buffer chain!
					i = get_lowest_numkey_just_pressed();
					if((i != 0) && (buff_chain_len < MAX_CHAIN))
					{
						buff_chain[buff_chain_len++] = i - 1;
						buff_chain[buff_chain_len] = 0xFF;
					}
					
					// display the current chain
					for(i = 0; i < buff_chain_len; i++)
					{
						if(buff_chain[i] >= 8)
							break;
						set_numkey_led(buff_chain[i] + 1);
					}
				}	
				else 
				{
					// releasing the chain key 'finalizes' the chain buffer
					if(just_released(KEY_CHAIN))
					{
						cpy_chain((uint8_t*)next_chain, buff_chain);

						// if we're not playing something right now, curr = next
						if(!playing)
						{
							cpy_curr_chain();

							curr_pitch_shift = next_pitch_shift;
							clear_led(LED_UP);
							clear_led(LED_DOWN);
						}

						clear_led(LED_CHAIN);
					}

					if( (i=is_pressed(KEY_UP))|| is_pressed(KEY_DOWN))
					{	// if they press U or D, show the current pitch shift and allow pitch shift adjust
						int8_t	notekey = get_lowest_notekey_pressed();

						// clear any pattern indicator leds
						if(just_pressed(KEY_UP) || just_pressed(KEY_DOWN))
						{
							clear_notekey_leds();
							clear_led(LED_CHAIN);
						}

						// check if they are changing the shift
						if( i)
						{
							clear_led(LED_DOWN);
							set_led(LED_UP);

							if(notekey != -1)
								next_pitch_shift = notekey;

							if(curr_pitch_shift >= 0)
							{
								if(!is_notekey_led_blink(curr_pitch_shift))
								{
									clear_notekey_leds();
									set_notekey_led_blink(curr_pitch_shift);
								}
							}

							if(next_pitch_shift != curr_pitch_shift)
								set_notekey_led(next_pitch_shift);
						}
						else // if(is_pressed(KEY_DOWN)) // must be true! 
						{
							clear_led(LED_UP);
							set_led(LED_DOWN);

							if(notekey != -1)
								next_pitch_shift = notekey - OCTAVE;	// invert direction
							if(curr_pitch_shift <= 0)
							{
								if(!is_notekey_led_blink(OCTAVE + curr_pitch_shift))
								{
									clear_notekey_leds();
									set_notekey_led_blink(OCTAVE + curr_pitch_shift);
								}
							}

							if(next_pitch_shift != curr_pitch_shift)
								set_notekey_led(OCTAVE + next_pitch_shift);
						}
						// if not playing something right now,
						// make the pitch shift effective immediately
						if(!playing)
							curr_pitch_shift = next_pitch_shift;
					}
					else	
					{	// not up or down pressed:
						
						if(just_released(KEY_UP) || just_released(KEY_DOWN))
						{
							// clear any pitch shift indicators
							clear_note_leds();
						}
						
						if(curr_pitch_shift>0)
							set_led(LED_UP);
						else 	
							clear_led(LED_UP);
	
						if(curr_pitch_shift<0)
							set_led(LED_DOWN);
						else
							clear_led(LED_DOWN);


						// if they just pressed a numkey, make a chain thats
						// one pattern long
						i = get_lowest_numkey_pressed();
						if(!is_pressed(KEY_DONE) && ((i != 0) || has_bank_knob_changed()))
						{
							if(i != 0) // i = pressed numkey 
							{
								clear_notekey_leds(); 
								buff_chain[0] = next_chain[0] = i - 1;
								buff_chain[1] = next_chain[1] = 0xFF;

								if(!playing)
									cpy_curr_chain();
							}
							else
							{
								next_bank = bank;
								if(!playing)
									curr_bank = next_bank;
							}

							if(!playing)
							{
								set_bank_leds(next_bank);
								curr_pitch_shift = next_pitch_shift;
							}
						}
						// indicate current pattern & next pattern & shift
						if(!chains_equiv(next_chain, curr_chain))
						{
							if(next_chain[1] == END_OF_CHAIN && curr_chain[1] == END_OF_CHAIN)
							{
								// basically single patterns. current blinks
								set_numkey_led_blink(curr_chain[0] + 1);
							}
							// otherwise, always just show the next chain in all solid lights
							for(i = 0; i < MAX_CHAIN; i++)
							{
								if(next_chain[i] > 8)
									break;
								set_numkey_led(next_chain[i] + 1);
							}
						}
						else
						{
							// clear old/blinking tracks/patterns
							clear_notekey_leds(); 
							for(i = 0; i < MAX_CHAIN; i++)
							{
								if(curr_chain[i] > 8)
									break;
								if(playing && (curr_chain[i] == curr_chain[curr_chain_index]))
								{
									set_numkey_led_blink(curr_chain[i] + 1);
								}
								else
								{
									// all other patterns in chain solid
									set_numkey_led(curr_chain[i] + 1);
								}
							}
						}
					
						if(is_pressed(KEY_DONE))
						{
							if(just_pressed(KEY_TEMPO))	/* 8th note stuff */
							{
								eighths = !eighths;
								no_loop = TRUE;
							}

							i = get_lowest_loopkey_just_pressed();
							if(start_point == 0)
							{
								start_point = i;
							}
							else if(end_point == 0)
							{
								end_point = i;
							}
						}
						else
						{
							all_ras(KEY_SLIDE,&all_slide);	
							all_ras(KEY_ACCENT,&all_accent);
							all_ras(KEY_REST,&all_rest);
						}
				
						if(just_pressed(KEY_NEXT) && is_pressed(KEY_PREV))
						{
							pattern_play_index = get_next_patt_idx();
						}
						if(just_pressed(KEY_PREV) && is_pressed(KEY_NEXT))
						{
							pattern_play_index = prev_pattern_index;
							if(loop_countdown)
							{
								if(countdown < 0)
									countdown--;
								else
									countdown++;
							}
						}
					}
				}
				if(just_released(KEY_DONE))
				{
					if(!no_loop)
					{
						loop_countdown = TRUE;
						countdown = loop_end - pattern_play_index - 1;
						if(start_point == 0 && end_point == 0)
						{
							loop_start = 1;
							loop_end = patt_length;
							loop = FALSE;
						}
						else
						{
							if
							(
								end_point != 0
							&&	pattern_buff[start_point - 1] != 0xFF
							&&	pattern_buff[end_point - 1] != 0xFF
							)
							{
								loop = TRUE;
								loop_start = start_point;
								loop_end = end_point;
							}

							start_point = end_point = 0;
						}
					}
					else
						no_loop = FALSE;
				}
			}	//tempoKnob
		}	//pattern play
	}	//while
}


uint8_t chains_equiv(volatile uint8_t *chain1, volatile uint8_t *chain2)
{
	uint8_t i;

	for(i = 0; i < MAX_CHAIN; i++)
	{
		if(chain1[i] != chain2[i])
			return FALSE;
		if(chain1[i] == 0xFF)
			return TRUE;
	}
	return TRUE;
}

uint8_t get_next_patt_idx()
{
	prev_pattern_index = pattern_play_index;

	if(loop && !loop_countdown)
	{
		if(pattern_play_index == loop_end - 1)
			return loop_start - 1;
		if(loop_start > loop_end)
			return pattern_play_index - 1;
	}
	else if(loop_countdown)
	{
		if(countdown < 0)
		{
			countdown++;
			return pattern_play_index - 1;
		}
		else if(countdown > 0)
			countdown--;
		else
		{
			loop_countdown = FALSE;
			if(loop)
				return loop_start - 1;
			else
				return loop_end;
		}
	}
	return pattern_play_index + 1;
}
/*
uint8_t isTriggerNote(uint8_t note)
{
	if(note > 35 && note < 97 && note != 37 && note != 39 && note != 42)
		return 1;

	//if ( note==36 || note==38 || note==40|| note==41|| note==43|| note==45|| note==47 || (note>47 && note<97) )
	//	return 1;
	return 0;
}
*/