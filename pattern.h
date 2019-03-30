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


/*
*
*	Constants.
*
*/

#define NUM_BANKS		16
#define NUM_LOCS		8
#define BANK_SIZE		(NUM_LOCS * PATT_SIZE)
#define PATT_SIZE		16	// bytes
#define PATTERN_MEM		0x0

#define MAX_CHAIN		32	//
#define END_OF_CHAIN	0xFF

#define PRESET_MEM 0x800
#define PRESET_SIZE 16
#define PRESET_SIZE_USED 12


#define END_OF_PATTERN	0xFF

#define NOTE_MASK  (0x3f)   // Note Part of the Pattern 
#define EOP_NOTE   (0x3f)   // Pseudo note valaue for end of pattern
#define REST_NOTE  (0x00)   // Pseudo note valaue for rest 

// edit_mode values:
#define EDIT_NONE			0   // => Pattern select 
#define EDIT_STEP_STOPPED	1
#define EDIT_RUNNING		2
#define EDIT_STEP_RUNNING	4
#define EDIT_ALLMODES   (EDIT_STEP_STOPPED|EDIT_RUNNING|EDIT_STEP_RUNNING)
// if (!(mode&EDIT_ALLMODES)) used for Pattern select - working when none of the submodes is acitve 
/*
*
*	Functions 
*
*/

void	do_pattern_edit(void);
void	do_patterntrack_play(void);

void	load_pattern(uint8_t bank, uint8_t patt_location);
void	write_pattern(uint8_t bank, uint8_t patt_location);

void    write_param_preset( uint8_t idx );
void    read_param_preset( uint8_t idx );

void	edit_pattern(void);
void	edit_live(void);


void	stop_editStepStopped(void);
void	stop_editStepRunning(void);
void    start_editStep(uint8_t next); 

uint8_t chains_equiv(volatile uint8_t *chain1, volatile uint8_t *chain2);

uint8_t get_next_patt_idx(void);
uint8_t isTriggerNote(uint8_t note);
void	cpy_curr_chain(void);	// copies next_chain to current chains and helps to save some bytes on the "frequently" used function 
void	set_syncmode(void);
void	start_stop(void); 

void set_current_index_led(void);


/*
*
*	Pattern related global variables 
*
*/
extern uint8_t	edit_mode;  // which edit input (sub) mode is currently active? 

extern uint8_t	tempoKnobMode;

extern uint8_t	pattern_play_index;
extern uint8_t	pattern_edit_index;

extern uint8_t	curr_chain[MAX_CHAIN];
extern uint8_t	next_chain[MAX_CHAIN];
extern uint8_t	curr_chain_index;
extern uint8_t	curr_bank, next_bank;
extern uint8_t	all_accent, all_slide, all_rest;	// all the time
extern int8_t	curr_pitch_shift;
extern int8_t	next_pitch_shift;
extern uint8_t	gateLen;
extern uint8_t	swingPercent;
extern uint8_t	variationOn;
extern uint8_t	loop;
extern uint8_t	loop_end;
extern uint8_t	loop_start;
extern uint8_t	scaleCorrType;
extern uint8_t	scaleCorrRoot;
extern uint8_t	patt_length;
extern uint8_t	patt_location;
extern uint8_t	store_mode;
extern uint8_t	eighths;




// function pointers, so pattern_play and pattern_edit  can hook their individual parts in "START" and "STOP" 
extern void (*start_fn) (void) ;
extern void (*stop_fn) (void) ;
