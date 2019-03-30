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
#ifndef _MIDI_H_
#define _MIDI_H_

#define MIDI_IGNORE			0x0 // special running status (this is not received via Midi in, but generated/used internally)
                                // for "not adressed" and other "not my data" purposes  
// midi channel messages
#define MIDI_NOTE_OFF		0x80
#define MIDI_NOTE_ON		0x90
#define MIDI_CONTROLLER		0xB0
#define MIDI_POLYPRESSURE	0xA0
#define MIDI_PROGCHANGE		0xC0
#define MIDI_CHPRESSURE		0xD0
#define MIDI_PITCHBEND		0xE0


// ... just a fraction is used, but I like to have them all defined ... 
// System Common Messages 
// 0xF0 ...0xF7 
#define MIDI_SYXSTART	0xF0 
#define MIDI_TCQFRAME	0xF1 
#define MIDI_SPP		0xF2
#define MIDI_SONGSELECT 0xF3
#define MIDI_RES1		0xF4
#define MIDI_RES2		0xF5
#define MIDI_TUNEREQ    0xF6
#define MIDI_SYXEND     0xF7 
// Realtime Messages 
#define MIDI_CLOCK		0xF8
#define MIDI_RES_3		0xF9
#define MIDI_START		0xFA
#define MIDI_CONTINUE	0xFB
#define MIDI_STOP		0xFC
#define MIDI_RES4		0xFD
#define MIDI_ACTSENSE   0xFE
#define MIDI_RESET      0xFF 

// special / pre-defined controllers: 
#define MIDI_ALL_NOTES_OFF	123
#define MIDI_ALL_SOUND_OFF	120

#define MIDISYNC_PPQ	24


void	midi_putchar(uint8_t c);
void	do_midi_mode(void);

void	init_midi(void);
void	midi_clr_input(void); 


uint8_t get_midi_addr(uint8_t eeaddr);

void	midi_note_off(uint8_t note);
void	midi_note_on(uint8_t note, uint8_t velocity);

void	midi_send_note_on(uint8_t note);
void	midi_send_note_off(uint8_t note);

uint8_t midi_recv_cmd(void);

void	midi_stop(void);
void	midi_notesoff(void);



void	do_midi(void);	// Midi data evaluation function. Called from the mode functions, calls the mode specific note_on (...) by the function pointers below 

extern volatile uint8_t  midi_realtime_cmd;  // distributes Midi-Start-Stop-Continue to the mode functions
extern volatile int8_t	 sync_startstop;
extern volatile uint8_t	 midi_run;			// Run/Stop status from midi. (Bar counting is only done/valid during midi run. 
extern volatile uint8_t	bar_pos; // bar position counter, in 1/96 steps
 
// function pointers to evalute midi messages - so each mode can do that different
extern void (*noteOnFunc) (uint8_t,uint8_t) ;
extern void (*noteOffFunc)(uint8_t) ;
extern void (*controllerFunc)(uint8_t,uint8_t) ;
extern void (*progChangeFunc)(uint8_t) ;

extern uint8_t	midi_out_addr;	// store this in EEPROM
extern uint8_t	midi_in_addr;	// store this in EEPROM, too!

//extern uint8_t bar_pos; // Position inside a (assumed 4/4) bar 

#endif
