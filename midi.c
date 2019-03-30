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
#include <avr/io.h>
#include <avr/interrupt.h>
#include "midi.h"
#include "switch.h"
#include "synth.h"
#include "main.h"
#include "led.h"
#include "dinsync.h"
#include "delay.h"


#define ACCENT_THRESH	100
#define MIDI_Q_SIZE		16

/*
*	Exported module globals 
*/
uint8_t				midi_out_addr;	// store this in EEPROM
uint8_t				midi_in_addr;	// store this in EEPROM, too!

volatile uint8_t	bar_pos; // bar position pointer, from MIDI SPP, in 1/96 steps 
volatile uint8_t	midi_run;

volatile uint8_t	midi_realtime_cmd; // distributes Midi-Start-Stop-Continue to the mode functions 
volatile int8_t		sync_startstop;

// function pointers for the current midi function 
void (*noteOnFunc) (uint8_t,uint8_t) ;
void (*noteOffFunc)(uint8_t ) ;
void (*controllerFunc)(uint8_t,uint8_t) ;
void (*progChangeFunc)(uint8_t) ;


/*
*	Module variables 
*/
static uint8_t			midi_running_status = 0;

static const uint8_t	midion_accent_velocity = 127;
static const uint8_t	midioff_velocity = 32;
static const uint8_t	midion_noaccent_velocity = 100;

static volatile  uint8_t midi_q[MIDI_Q_SIZE];		// cyclic queue for midi msgs
static volatile  uint8_t head_idx = 0;
static volatile  uint8_t tail_idx = 0;

static volatile  uint8_t midi_tx_q[MIDI_Q_SIZE];	// cyclic queue for midi msgs
static volatile  uint8_t head_tx_idx = 0;
static volatile  uint8_t tail_tx_idx = 0;


/*
*	Forward function declarations 
*/

uint8_t	midi_getchar(void);

// interrupt on receive char
#ifdef __AVR_ATmega162__
ISR(USART0_RXC_vect)
#endif
#ifdef __AVR_ATmega2561__
ISR(USART0_RX_vect)
#endif
{
	unsigned char	c = UDR0;

	if(c>=MIDI_CLOCK)
	{
		if(sync==MIDI_SYNC)
		{
			if(c==MIDI_START ) 
			{
				bar_pos=0;
				midi_run=1; 
				sync_startstop=0;
				midi_realtime_cmd=c;
			}
			else if ( c==MIDI_CONTINUE )
			{
				midi_run=1;
				if(IS_SET1(SETTINGS1_CONTINUESTART))
				{
					sync_startstop=0;
					midi_realtime_cmd=MIDI_START;
				}
				else 
					sync_startstop=1; 
			}
			else if(c==MIDI_STOP)
			{
				midi_run=0;
				sync_startstop=0;
				midi_realtime_cmd=c;
			}
			else if(c == MIDI_CLOCK)
			{
				if(midi_run)
					bar_pos++;
				if(bar_pos>=96  )
				{
					bar_pos=0; 
					if(sync_startstop>0)
					{
						sync_startstop=0;
						midi_realtime_cmd=MIDI_START;
					}
					if(sync_startstop<0)
					{
						sync_startstop=0;
						midi_realtime_cmd=MIDI_STOP;
					}
				}
				measureTempo();
				run_tempo+=2;
			}
		}
		return; 
	}

	midi_q[tail_idx++] = c;		// place at end of q
	tail_idx %= MIDI_Q_SIZE;
}

// Midi UART TX interrupt 
ISR(USART0_UDRE_vect)
{
	uint8_t c;

	if(head_tx_idx == tail_tx_idx)
	{
		UCSR0B&=~ (1<<UDRIE0) ; // disable interrupt
	}
	else
	{
		c = midi_tx_q[head_tx_idx++];
		head_tx_idx %= MIDI_Q_SIZE;
		UDR0 = c;
	}
}

uint8_t get_midi_addr(uint8_t eeaddr)
{
	uint8_t midi_addr;

	midi_addr = internal_eeprom_read8(eeaddr);
	if(midi_addr > 15)
		midi_addr = 15;
	return midi_addr;
}

void init_midi(void)
{
	midi_in_addr = get_midi_addr(MIDIIN_ADDR_EEADDR);
	midi_out_addr = get_midi_addr(MIDIOUT_ADDR_EEADDR);
}


void midiFuncNop2(uint8_t a, uint8_t b)
{
	 // move some hot air!
}
void midiFuncNop1(uint8_t a)
{
	// move some hot air!
}


void midi_clr_input(void)
{
	cli(); 
	tail_idx = head_idx;
	sei();
	midi_running_status=0; 
	noteOnFunc=midiFuncNop2; 
	noteOffFunc=midiFuncNop1; 
	controllerFunc=midiFuncNop2; 
	progChangeFunc=midiFuncNop1;
}

void do_midi(void)
{
	uint8_t c; 
	static uint8_t complete, data1; 

	if(head_idx != tail_idx) //   Is there a char in the queue ? 
	{
		// if its a command & either for our address or 0xF,
		// set the midi_running_status
		c = midi_getchar();

		if(c &0x80)
		{		// if the top bit is high, this is a command
			if( c >= 0xF0)				// universal cmd, no addressing
			{	
				midi_running_status = c ; 
			}
			else if((c & 0xF) == midi_in_addr)	//  matches our addr
			{	
				midi_running_status = c &0xf0;
			}
			else
			{
				// not for us, continue!
				midi_running_status = MIDI_IGNORE;
				return; 
			}
			complete=0; 
		}
		else
		{
			if(midi_running_status==MIDI_CHPRESSURE || midi_running_status==MIDI_PROGCHANGE )
			{
				// single byte function
				complete=2; 
			}
			else
			{
				// dual byte functions. 
				if(complete==0)
				{
					data1=c; 
					complete=1; 
				}
				else if(complete==1) 
					complete=2; 
			}
			if(complete==2)
			{
				switch(midi_running_status)
				{
					case MIDI_NOTE_ON:
						if(c) 
						{
							noteOnFunc(data1,c);
							break; 
						}
						// no break! =>  with Velocity 0 it *IS* NOTE OFF 	
					case MIDI_NOTE_OFF:
						noteOffFunc(data1);
						break;
					case MIDI_CONTROLLER: 
						controllerFunc(data1,c);
						break; 
					case MIDI_PROGCHANGE:
						progChangeFunc(c); 
						break; 
					case MIDI_SPP:
						bar_pos= (data1 &0xf)*6; // data1 =1/16th notes 
						break; 
					case MIDI_PITCHBEND:
					case MIDI_POLYPRESSURE: 
					case MIDI_CHPRESSURE:
					case MIDI_IGNORE:				// somebody else's data, ignore
					default:
						break;
				}
				complete=0; 
			}
		}
	}
}


void do_midi_mode(void)
{
	// show midi addr on bank leds
	set_bank_leds(midi_in_addr);

	has_bank_knob_changed();	// ignore startup change
	prev_note = 255;			// no notes played yet

	// set proper  functions for do_midi: 
	noteOnFunc=midi_note_on; 
	noteOffFunc=midi_note_off; 

	while(1)
	{
		read_switches();
		if(function != MIDI_CONTROL_FUNC)
		{
			midi_notesoff();	// clear any stuck notes
			return;
		}

		if(has_bank_knob_changed())
		{
			// bank knob was changed, change the midi address
			midi_in_addr = bank;

			// set the new midi address (burn to EEPROM)
			internal_eeprom_write8(MIDIIN_ADDR_EEADDR, midi_in_addr);

			//clear_bank_leds();
			set_bank_leds(midi_in_addr);

		}
		do_midi(); 
	}
}


void midi_note_off(uint8_t note)
{
	if(note == prev_note)
	{
		note_off(0);
		prev_note = 255;
	}
	set_note_led( 0 );
}

void midi_note_on(uint8_t note, uint8_t velocity)
{
	uint8_t slide = 0;

	// velocity 0 -> note off is already handled by the midi stream decoder! 
	
	// Legato? => Slide! 
	if(prev_note != 255)
		slide = SLIDE;
	prev_note = note;

	// move incomming notes to playable range. 
	while (note<0x20)	// Note 0x19 and 0x20 have the same pitch as 21, as the buffer OP-Amp can't go that close to ground rail... 
		note+=OCTAVE;
	while (note>=0x3F+0x19) 
		note-=OCTAVE;

		
	note -= 0x19;
	note |=slide; 
	if(velocity > ACCENT_THRESH)
		note |=  ACCENT; 
	note_on( note);
	set_note_led( note);
}

void midi_send_note_on(uint8_t note)
{

	if((note & 0x3F) == 0)
	{
	/*	Rest 
		Midi does not know about rests
		So nothing to do.  
	*/
	}
	else
	{
		midi_putchar((MIDI_NOTE_ON) | midi_out_addr);
		midi_putchar((note & 0x3F) + 0x19); // note
		if(note & ACCENT )	// if theres an accent, give high velocity
			midi_putchar(midion_accent_velocity);
		else
			midi_putchar(midion_noaccent_velocity);
	}
}

void midi_send_note_off(uint8_t note)
{
	if((note & 0x3F) == 0)
	{
		// Note was a Rest - nothing to do; 
	}
	else
	{
		midi_putchar((MIDI_NOTE_OFF ) | midi_out_addr); // command
		midi_putchar((note & 0x3F) + 0x19);		// note
		midi_putchar(midioff_velocity);			// velocity
	}
}

void midi_putchar(uint8_t c)
{
	cli();
	midi_tx_q[tail_tx_idx++] = c;	// place at end of q
	tail_tx_idx %= MIDI_Q_SIZE;
	UCSR0B |= (1<<UDRIE0) ;			// enable tx interrupt
	sei();
}




uint8_t midi_getchar(void)
{
	char	c;

//	while(head_idx == tail_idx);  <--- that should never loop as *all* calls to this should only be made when there is a char waiting 

	cli();
	c = midi_q[head_idx++];
	head_idx %= MIDI_Q_SIZE;
	sei();

	return c;
}

// sends a midi stop and 'all notes off' message
void midi_stop(void)
{
	midi_putchar(MIDI_STOP);
	midi_notesoff();
}

void midi_notesoff(void)
{
	midi_putchar((MIDI_CONTROLLER) | midi_out_addr);
	midi_putchar(MIDI_ALL_NOTES_OFF);
	midi_putchar(0);
	midi_putchar(MIDI_ALL_SOUND_OFF);
	midi_putchar(0);

}
