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
#include <stdio.h>
#include "main.h"
#include "led.h"
#include "switch.h"
#include "delay.h"
#include "pattern.h"
#include "compcontrol.h"
#include "keyboard.h"
#include "midi.h"
#include "eeprom.h"
#include "synth.h"
#include "dinsync.h"
#include "randomizer.h" 


/*
*
*	Check if on of the both supported CPU types is defined
*	#define some helpers, so the code is less cluttered with #ifdef __WHICH_CPU_BLAH___ 
*/
#if defined( __AVR_ATmega162__ )
	#define ENA_LED 0xBB
	#define CURRENT_CPU_TIMSK3 ETIMSK	
	#define CURRENT_CPU_EE_WRITE_EN EEWE
	#define CURRENT_CPU_EE_WRITE_PREPARE EEMWE
#elif defined( __AVR_ATmega2561__ )
	#define ENA_LED 0xF7
	#define CURRENT_CPU_TIMSK3 TIMSK3
	#define CURRENT_CPU_EE_WRITE_EN EEPE
	#define CURRENT_CPU_EE_WRITE_PREPARE EEMPE
#else 
	#error "Selected CPU not supported"
#endif


#define UART_BAUDRATE	19200UL
#define MIDI_BAUDRATE	31250UL			// the MIDI spec baudrate

#define TEMPOISRUNNING	(CURRENT_CPU_TIMSK3&(1<<OCIE3A))



volatile int8_t		tk_value;	// tempo knob moved
#ifdef HASDOUBLECLICKF
volatile uint8_t	doublePressTimeOut;	// counted down by 1ms interrupt
#endif
volatile uint8_t	debounce_timer;	// counted up by 1ms interrupt, 
volatile uint8_t	run_tempo;		// to start do_tempo if (Tempo) Timer is elapsed or Din-Sync or Midi-Sync received. 
volatile uint16_t   tapTempoTimer;  // for measuring time between two taps. 


uint8_t				settings[NUM_SETTINGS_TOTAL] ={  // "USER C" + other permanent user settings configuration bytes from/to internal EEPROM
					SETTINGS0_LIVE_SETCHAIN | SETTINGS0_KEEP_PATT | SETTINGS0_AUTO_INC,  // default settings 0
					SETTINGS1_STARTMODE | SETTINGS1_HELPLED,								 // default settings 1
					0,0,0,SETTINGS_VERSION_MAGIC,			// settings 2,3,4, Version-Magic 
					0,0,0,0,0,0,0,0,0,0					// unused bytes for future expansion 
					};
					

uint8_t				curPatternRun = 0;
uint8_t				playing;		// are we playing?
uint8_t				tempo;	// current internal tempo 
uint16_t			delay_clock9ms; // the value timer 3 must be changed to get a proper delay for start to clock 
uint8_t				sync = INTERNAL_SYNC;
uint8_t				note_counter; // counts 1/32 notes (as the 303 gate length was 1/32 note) 
uint8_t				curr_note;
uint8_t				prev_note;
uint8_t				swing_it = 0;

// The randomizer control values which are editable with the knob mode
uint8_t				accentRand;
uint8_t				slideRand;
uint8_t				octRand;
uint8_t				restRand;
uint8_t				octDownRand;
uint8_t				variFirstStep ;
uint8_t				variBarModulo;
uint8_t				variLastStep=15;

//  Tempo knob function parameter info:
uint8_t * const		tk_paras[NUM_KNOB_PARAMS]	 = { NULL, &octRand, &octDownRand, &slideRand, &accentRand,&restRand, &swingPercent, &gateLen, &variBarModulo,&variFirstStep, &variLastStep };
const uint8_t		tk_maxval[NUM_KNOB_PARAMS]	 = { 0,    61,       61,           61,         61,          61,		   100,			  38,       15,             15,             15            };
const uint8_t		tk_fnkey[NUM_KNOB_PARAMS]	 = { 0,    KEY_UP,   KEY_DOWN,     KEY_SLIDE,  KEY_ACCENT,  KEY_REST,  KEY_PREV,      KEY_NEXT, KEY_CS,         KEY_DS,         KEY_FS        };


#ifdef SYNC_OUT
// when doing midi sync to dinsync conversion, this is the timeout
// to dropping the clock after a MIDICLOCK message
static volatile uint8_t	dinsync_clock_timeout = 0;
static uint16_t			pendingDinPulse[12];
static uint8_t			note_counterFullTempo;
#endif

volatile uint8_t	tempoMeasure;
static uint8_t			tmpo = 120;
static uint8_t			midi_tempo = 120;


static uint8_t			prevSlide = 0;
static uint8_t			isSlide = 0;

static uint8_t			swingNoteOff = 0;
static uint8_t			swingNoteOn = 0;
static uint16_t			swingNoteOff_timeout = 0;
static uint16_t			swingNoteOn_timeout = 0;
static volatile uint8_t	dispatch_swing_note;
static uint8_t          wasAcc = 0;

// store pitch shifted note values here, to be able to sent matching noteOff after pitchshift changed
static uint8_t			prevNoteSent = 0;
static uint8_t			noteSent = 0;

/* 8th note stuff */
static uint8_t			skipit = TRUE;
static uint8_t			runhalf = FALSE;
static uint8_t			onemore = FALSE;



static int8_t			scaleCorrTab[2][12] =
{
	{ 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 },   // major
	{ 0, 1, 0, 0, -1, 0, 1, 0, 0, -1, 0, -1 }, // minor
};


static const uint16_t scaleModifications[2][2][2] =  // main type / dim2nd / harm min
{
  { {   0x05ad,
        0x09ad },  
  {     0x05ab,    
        0x09ab } },
        
{ {     0x0ab5,
        0x0ab5 },  
  {     0x0ab3,    
        0x0ab3 } } 
};





//void	dispatch_note_off(int8_t pitch_shift);
void	dispatch_note_off();
void	dispatch_note_on(int8_t pitch_shift);
void	load_next_chain(uint8_t reset);
void	setup_scales(void);
void dumb_functions(uint8_t func);


/*
*
*	Set up pins and UARTs on start up
*
*/
void ioinit()
{
#if defined( __AVR_ATmega162__ )

	PORTA = 0x3C;	// pullups on rotary1,2,4,8
	DDRA = 0xC0;	// led latch (o), rotary com (o), rot1, rot2, rot4, ro8, tempoa, tempob

	PORTB = 0x04;	// Uart Tx high
	DDRB = 0xB9;	// spi_clk, spi_in, spi_out, NC, USB-UART TX, RX, LED_ENABLE (was NC), switch latch (o)
	DDRC = 0xFF;	// accent, slide, note[0-5]
	PORTD = 0x02;	// Midi TX high
	DDRD = 0xFF;	// dinsync1, 2, 3, 4 (outputs), NC, NC, MIDI TX & RX
	DDRE = 0xFF;	// note latch, gate, NC

#elif defined( __AVR_ATmega2561__ )

	PORTA = 0x3C;	// pullups on rotary1,2,4,8
	DDRA = 0xC0;	// led latch (o), rotary com (o), rot1, rot2, rot4, ro8, tempoa, tempob

	PORTB = 0x0;	// Bootloader messed that up => back to zero
	DDRB = 0xB7;	// NC,LED_ENABLE (was NC) ,gate,note latch,miso,mosi,sck,switch latch

	DDRC = 0xFF;	// accent, slide, note[0-5]
	DDRD = 0xF8;	// dinsync1,2,3,4(o),usb tx(o),usb rx,i2c(not used at the moment)
	PORTD = 0x0b;	// pullups on for unsed pins, usb tx set high

	PORTE = 0xff;	//  pullups on for unused pins, midi tx set high
	DDRE = 0x02;	//  nc,nc,eeprom,nc,nc,nc,nc,nc,midi tx(o), midi rx

	PORTF = 0xff;	// pullups on for unused pins
	DDRF = 0x00;	// all pins unused

	PORTG = 0xff;	// pullups on for unused pins
	DDRG = 0x00;	// all pins unused
#endif

	// SPI set up:
#if CFG_SPI_CLOCK_FREQ_MHZ==1
	SPCR = (1 << SPE) | (1 << MSTR) | (1<<SPR0);	// master spi, clk=fosc/16 = 1mhz
#elif CFG_SPI_CLOCK_FREQ_MHZ==2
	SPSR = (1<<SPI2X);
	SPCR = (1 << SPE) | (1 << MSTR) | (1<<SPR0);	// master spi, clk=fosc/8 = 2mhz
#elif  CFG_SPI_CLOCK_FREQ_MHZ==4
	SPCR = (1 << SPE) | (1 << MSTR);				// master spi, clk=fosc/4 = 4mhz
#else
	SPSR = (1<<SPI2X);
	SPCR = (1 << SPE) | (1 << MSTR);				// master spi, clk=fosc/2 = 8mhz
#endif

#ifdef FLASHCRCCHECK
#if ! DEBUG
	if( checkFlash() !=0 )
	{	// CRC error .. turn some LEDs on ... this isn't the time for fancy code...
#if FLASHCRCCHECK >=2
		static const __flash uint8_t ledm[]={ BITBYTE(LED_DONE)| BITBYTE(LED_DOWN),0, 0, 0, 0 }; // just DOWN & DONE lit
#endif
		cbi(LED_LATCH_PORT, LED_LATCH_PIN);
		for(uint8_t i = 0; i < 5; i++)
		{
#if FLASHCRCCHECK >=2
			SPDR = ledm[i]; 
#else 
			SPDR = 0x88;
#endif 
			while(!(SPSR & (1 << SPIF)))
				;
		}
		sbi(LED_LATCH_PORT, LED_LATCH_PIN);
		DDRB = ENA_LED;	//  enable LEDs. (=> for Hardware mod to *NOT* have the anoying random start up state of the HC594 shown on the LEDs. )
		while(1)
			;
	}
#endif
#endif

	/* setup the USB-UART */
	uint16_t	baud = (F_CPU / (16 * UART_BAUDRATE)) - 1;
	UCSR1B |= (1 << RXEN1) | (1 << TXEN1);	// read and write & intr
	UBRR1L = (uint8_t) baud;	// set baudrate
	UBRR1H = (uint8_t) (baud >> 8);

	UCSR1B |= (1 << RXCIE1);	// now turn on RX interrupts

	/* setup the MIDI UART */
	baud = (F_CPU / (16 * MIDI_BAUDRATE)) - 1;
	UCSR0B |= (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);	// read and write, interrupt on recv.
	UBRR0L = (uint8_t) baud;	// set baudrate
	UBRR0H = (uint8_t) (baud >> 8);
}

////////////////////////////////// main()
int main(void)
{
	uint8_t i;

	ioinit();		// set up IO ports and the UART
	init_tempo(); 	// start the tempo timer
	init_timer0();	// start the 1ms 'rtc' timer0
#ifdef DIN_SYNC_IN
	dinsync_set_out();	// output DINSYNC
#endif
	init_midi();
	clock_leds();	// set shift register outputs to zero - this enables the interrupts also!
	//	sei();	// enable interrupts aready done by clock_leds()
	DDRB = ENA_LED;	//  enable LEDs. (=> for Hardware mod to *NOT* have the anoying random start up state of the HC594 shown on the LEDs. )

#if !ALL_FIXED_SETTING
	uint8_t version = internal_eeprom_read8(SETTINGS_VERSION+USERSETTINGS_EEADDR);
	if (version != SETTINGS_VERSION_MAGIC)
	{
		for(i=0;i<NUM_SETTINGS_TOTAL;++i)
		internal_eeprom_write8(USERSETTINGS_EEADDR + i , settings[i]);
	}
	for(i=0;i<NUM_SETTINGS_TOTAL;++i)
		settings[i] = internal_eeprom_read8(USERSETTINGS_EEADDR+i);	//read settings from EEPROM
#endif




	i=4;
	while(read_switches() || i--) ;	// four "real" reads required to get all switches in current positions.
	
	// the main loop!
	while(1)
	{
		tempoKnobMode=0;	// end parameter edit
		edit_mode=EDIT_NONE; 
		playing=0;			// mode change stops all playing
		curr_pitch_shift=0;	// start up with no shift, so it hast not be cleared on every mode function startup and return.
		next_pitch_shift=0;
		setup_scales();
		dinsync_stop();		// just in case it was on during last mode ..
		clear_all_leds();	// start up every mode with no LEDs, so not every mode has to clear them on start up!
		turn_off_tempo();	//  start up every function with tempo of, so it has just to be switched on if needed
		midi_clr_input();	// not all modes read midi in, so clean up the buffer to remove anything old accumulated in another mode
		note_off(0);		// shut up after mode change.
		sync = INTERNAL_SYNC;	// just the most often as default ... saves a few bytes.
		
		switch(function)
		{
			case EDIT_PATTERN_FUNC:
				do_pattern_edit();
				break;
			case PLAY_PATTERN_FUNC:
				do_patterntrack_play();
				break;
			case PLAY_PATTERN_DINSYNC_FUNC:
				sync = DIN_SYNC;
				do_patterntrack_play();
				break;
			case PLAY_PATTERN_MIDISYNC_FUNC:
				sync = MIDI_SYNC;
				do_patterntrack_play();
				break;
			case MIDI_CONTROL_FUNC:
				do_midi_mode();
				break;
			case KEYBOARD_MODE_FUNC:
				do_keyboard_mode();
				break;
			case A_FUNC:	// =  edit pattern with midi sync on
				sync = MIDI_SYNC;
				do_pattern_edit();
				break;
			case B_FUNC:
				sync = DIN_SYNC;
				do_pattern_edit();
				break; 
			case RANDOM_MODE_FUNC:
#ifdef HASRANDOM
				set_syncmode();
#endif
			case COMPUTER_CONTROL_FUNC:
			case EDIT_TRACK_FUNC: 
			default:
				dumb_functions(function);
				break;
		}
	}
}



void clearPendingDinPulses()
{
#ifdef SYNC_OUT
	for(uint8_t i = 0; i < 12; i++)
		pendingDinPulse[i] = 0;
	note_counterFullTempo = 0;
#endif
	skipit = 1;
	
}

/*void clearScheduledNotes()
{
	swingNoteOn = 0;
	swingNoteOn_timeout = 0;
	swingNoteOff = 0;
	swingNoteOff_timeout = 0;
}*/

uint8_t findEOP()
{
	uint8_t index=0; 
	while((index < patt_length) && (pattern_buff[index] != END_OF_PATTERN))
		(index)++;
	return index; 
}

uint8_t variationAllowed()
{
	if(	   variationOn 
		&& pattern_play_index >= variFirstStep 
		&& pattern_play_index <= variLastStep
		&& curPatternRun % (variBarModulo + 1) == variBarModulo )
	{
		return 1;
	}
	else
	{
		return 0; 
	}
}

/* */

// the 'tempo' interrupt! (on timer 3)
// gets called 2*4*DINSYNC_PPQ times per beat (192 calls per beat @ sync24)

// fastest is 255PM -> 1.225ms
ISR(TIMER3_COMPA_vect)
{
	run_tempo++; 
}

void clock_ticks()
{
	if(run_tempo)
	{
		cli();
		run_tempo--;
		sei();
		do_tempo();
	}
	if(dispatch_swing_note==1)
	{
		dispatch_note_on(swingNoteOn);
	}
	if(dispatch_swing_note==2)
	{
		//dispatch_note_off(swingNoteOff);
		dispatch_note_off();
	}
	dispatch_swing_note=0;


}

void do_tempo(void)
{
#ifdef HASRANDOM
	static uint8_t next_random_note;
#endif 
	uint8_t curr_function = function;
	uint8_t division_factor = patt_length / 4;

	cli();

	int8_t	GL = gateLen - 18;

#ifdef SYNC_OUT
	if(!note_counter && !dinsync_counter)
	{
		note_counterFullTempo = 0;
		swing_counter = 0;
	}

	if(swing_counter > 5)
		swing_counter = 0;

	if(!swing_counter)
	{
		tmpo = tempo;
		if(sync == MIDI_SYNC || sync == DIN_SYNC)
			tmpo = midi_tempo;

		if(note_counterFullTempo > 7)
			note_counterFullTempo = 0;

		if(!note_counterFullTempo)
		{
			clearPendingDinPulses();
		}

		if(!(note_counterFullTempo % 2))
		{
			uint8_t swung = (note_counterFullTempo % 4) > 0;
			uint16_t *pend = pendingDinPulse;
			if(swung)
				pend += 6;

			uint16_t	straightPulseDist = 10000 / tmpo;
			for(uint16_t i = 0; i < 6; i++)
			{
				int16_t factor;
				if(swung)	//swung, go out
					factor = 50 - (i * 8);
				else
					//unswung go in
					factor = i * 8;

				*pend++ = ((i * straightPulseDist+2)/4) + (factor * swingPercent) / tmpo + 1;
			}
		}

		note_counterFullTempo++;
	}
	swing_counter++;
#else
	tmpo = tempo;
	if(sync == MIDI_SYNC || sync == DIN_SYNC)
		tmpo = midi_tempo;
#endif

	// if the sync is internal or whatever, we have to generate dinsync/midisync msgs
	if(dinsync_counter >= DINSYNC_PPQ / division_factor)	// 6 or 8 (triplet->8)
	{
		dinsync_counter = 0;
		if(!(note_counter & 0x1))
			swing_it = !swing_it;
	}

	uint16_t swingShift = (50 * swingPercent) / tmpo;		//ms
	if(swingPercent && !swingShift)
		swingShift=1; // avoid 0 if swingPercent != 0, as that does not work proper! 
	int16_t fac = 7500;
	if(patt_length == 15)
		fac = 10000;
	if(runhalf)
		fac *= 2;

	int16_t gateLenNorm = fac / tmpo;	//ms of a 1/32 note
	uint16_t maxNoteoffTime = 0;

	//TODO check all this for triplets and/or eight-mode
	maxNoteoffTime = gateLenNorm * 2 - 5;

	//    if (!swing_it)
	//        maxNoteoffTime += swingShift; // cant handle noteOff for longer notes!!
	if(!swing_it)
		swingShift = 0;

	// 24 pulses per quarter, increment

	// make sure that all notes actually start on the zero count
	// so that tempo and SYNC are aligned.
	if(dinsync_counter != 0)
	{
		dinsync_counter++;
		sei();
		return;
	}
	dinsync_counter++;
	sei(); 

	if(!(note_counter & 0x1))
	{
		//change the grid only for done "rounds" of clocks!
		if(pattern_buff[PATT_SIZE - 1] == 0x01)
			patt_length = 15;
		else
			patt_length = 16;
	}


	skipit = !skipit;	/* 8th note stuff */

	if((!runhalf && !onemore) || !skipit)
	{
		int8_t schedule_note=-128; 

		// reset note counter
		if(note_counter >= 8)
			note_counter = 0;

		if(note_counter & 0x1)
		{	// sixteenth notes
#ifdef HASRANDOM
			schedule_note=0; 
#endif 
			if(playing)
			{
				schedule_note=curr_pitch_shift;

				if(loop && pattern_play_index == loop_start - 1)
				{
					curr_pitch_shift = next_pitch_shift;

					/* 8th note stuff */
					if(runhalf != eighths)
					{
						runhalf = eighths;
						skipit = runhalf;
						onemore = !skipit;
					}

					if(!chains_equiv(next_chain, curr_chain))
					{
						pattern_play_index = patt_length;
						loop_start = 1;
						loop_end = patt_length;
						loop = FALSE;
					}
				}

				// last note of this pattern?
				if((pattern_play_index >= patt_length) || (pattern_buff[pattern_play_index] == END_OF_PATTERN))
				{

					/* 8th note stuff */
					if(runhalf != eighths)
					{
						runhalf = eighths;
						skipit = runhalf;
						onemore = !skipit;
					}

					pattern_play_index = 0; // start next pattern in chain
					curPatternRun++;
						
					if(curr_function==PLAY_PATTERN_FUNC ||curr_function==PLAY_PATTERN_DINSYNC_FUNC ||curr_function==PLAY_PATTERN_MIDISYNC_FUNC   )
					{	// one of the play modes, these switch patterns. Edit modes should not do that. 
							
						curr_chain_index++;		// go to next patt in chain

						// last pattern in this chain?
						if((curr_chain_index >= MAX_CHAIN) || (curr_chain[curr_chain_index] == 0xFF))
						{
							curr_chain_index = 0;
						}

						load_next_chain(TRUE);

						uint8_t len = patt_length;
						load_pattern(curr_bank, curr_chain[curr_chain_index]);

						patt_length = len;
					}
				}
			}	//playing

			if(schedule_note!=-128)
			{
				if(GL == 0 && swing_it && swingPercent > 0)
				{
					swingNoteOff_timeout = swingShift;	//(50*swingPercent) / tempo; //ms
					swingNoteOff = curr_pitch_shift;
				}
				else if(GL == 0)
				{
					//dispatch_note_off(curr_pitch_shift);
					dispatch_note_off();
				}
			}
		}
		else
		{
			/* break out */
			prev_note = curr_note;

#ifdef HASRANDOM
			if(curr_function==RANDOM_MODE_FUNC)
			{
				curr_note = next_random_note;
				next_random_note = random();
				set_note_led(curr_note);
				schedule_note=0;
			}
#endif
			if(playing)
			{
				if(!tempoKnobMode)
					set_current_index_led();
				// load up the next note
				curr_note = pattern_buff[pattern_play_index];

				if(curr_note != 0xFF)
				{
					if( (edit_mode&EDIT_RUNNING) && !store_mode ) // running edit, DONE not pressed: display notes
							set_note_led(curr_note);

					schedule_note=curr_pitch_shift; //+ get_pitchshift_from_patt(curr_patt) );
				}
				pattern_play_index = get_next_patt_idx();
			}
			if(schedule_note !=-128)
			{
				if(swing_it && swingPercent > 0)
				{
					swingNoteOn_timeout = swingShift;	//(50*swingPercent) / tempo; //ms
					swingNoteOn = schedule_note;
				}
				else
				{
					dispatch_note_on(schedule_note);
				}

				if(GL != 0)
				{
					swingNoteOff_timeout = gateLenNorm + GL * gateLenNorm / 20 + swingShift;
					if(swingNoteOff_timeout > maxNoteoffTime)
					swingNoteOff_timeout = maxNoteoffTime;
					swingNoteOff = schedule_note;
				}
			}
		}

		// blink the tempo led & any other LEDs!
		if(!tempoKnobMode)
		{
			if(note_counter < 2)
			{
				set_led(LED_TEMPO);
			}
			else // if(note_counter < 8)
			{
				clear_led(LED_TEMPO);
			}
		}
		blink_led_on = !(note_counter&2);
		dimblink_led_on = !(note_counter%8);
		note_counter++;
	}
	else if(onemore)
		onemore = FALSE;
}



//   1ms "RTC" timer
#ifdef __AVR_ATmega162__
ISR(TIMER0_COMP_vect)
#endif 
#ifdef __AVR_ATmega2561__
ISR(TIMER0_COMPA_vect)
#endif
{
	// Count all the "stop watches" needed here and there... 

	if(tempoMeasure!=0xff)
		tempoMeasure++;

	if(debounce_timer != 0xFF)
		debounce_timer++;

	if(uart_timeout != 0xFFFF)
		uart_timeout++;

	if(tapTempoTimer!=0xffff)
		tapTempoTimer++; 

#ifdef HASDOUBLECLICKF
	if(doublePressTimeOut > 0)
		doublePressTimeOut--;
#endif 
	
	dimblink_led_ctr++;

	// Timers for the swinging notes: 
	if(swingNoteOff_timeout != 0)
	{
		swingNoteOff_timeout--;
		if(swingNoteOff_timeout == 0)
		dispatch_swing_note=2; // just generate a "do it now" "event" for the main loop. 
	}

	if(swingNoteOn_timeout != 0)
	{
		swingNoteOn_timeout--;
		if(swingNoteOn_timeout == 0)
		dispatch_swing_note=1;
	}


#ifdef __AVR_ATmega2561__
	// Tempo knob polling, the MEGA162 does that with  its port change interrupt, so not here 
	static uint8_t last_tk=3;
	static int8_t cnt; 
	uint8_t curr_tk;

	curr_tk = TEMPO_PIN & 0x3;	// pins A0 and A1
	// clicks are on "3" 
	// CW (+) :  3 -> 1 -> 0 -> 2 -> 3 
	// CCW (-): 3 -> 2 -> 0 -> 1 -> 3 
	if(last_tk!=curr_tk)
	{	// tempo knob change!
		if(   (last_tk==3 && curr_tk==1 )
		   || (last_tk==1 && curr_tk==0 )
		   || (last_tk==0 && curr_tk==2 ) 
		   || (last_tk==2 && curr_tk==3 ) 
		)
		{	// valid CW 
			cnt++; 
			if(cnt==4)
			{
				cnt=0;
				tk_value++;
			}
		}
		else if(    
			   ( last_tk==3 && curr_tk==2 ) 
			|| ( last_tk==2 && curr_tk==0 )
			|| ( last_tk==0 && curr_tk==1 ) 
			|| ( last_tk==1 && curr_tk==3 ) 
		)
		{	// valid CCW
			cnt--;
			if(cnt==-4)
			{
				cnt=0;
				tk_value--;
			}
		}
		else // quad err
			cnt=0;

		last_tk = curr_tk;
	}
#endif


	// any (!) creation of din/MIDI clock now here!!!
	// direct creation in main/1ms timer for DINin-MIDIout is cut!
	// direct creation in midi.c for MIDIin-DINout is cut!
	// any setting of the pending pulses happens in do_tempo for any 12ths do_tempo (1/16 note)
#ifdef SYNC_OUT
	uint16_t	*p = pendingDinPulse;

	if((sync != DIN_SYNC) && (dinsync_clock_timeout != 0))
	{
		dinsync_clock_timeout--;
		if(dinsync_clock_timeout == 0)
			cbi(DINSYNC_DATA, DINSYNC_CLK);		// lower the clock
	}

	for(uint8_t i = 0; i < 12; i++)
	{
		if(*p > 1)
			*p = *p - 1;

		if(*p == 1)
		{
			if(sync != DIN_SYNC)
			{
				sbi(DINSYNC_DATA, DINSYNC_CLK); // rising edge on note start
				dinsync_clock_timeout = 6;		// schedule pulse to end
			}
			midi_putchar(MIDI_CLOCK);
			*p = 0;
		}
		p++;
	}
#endif
#ifdef DIN_SYNC_IN
	if(sync == DIN_SYNC)
	{
		static uint8_t	last_dinsync_c;
		uint8_t curr_dinsync_c;
		
		curr_dinsync_c = DINSYNC_PINS & (1<<DINSYNC_CLK); 

		if(last_dinsync_c==0 && curr_dinsync_c)
		{
			if(DINSYNC_PINS & (1<< DINSYNC_START))
			{
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
			}
			run_tempo +=2 ;		// notify a clock was recv'd

			// "Clock out" is not done on "clock in" but by the swing timing! 
			measureTempo();
		}
		last_dinsync_c = curr_dinsync_c;
	}
#endif

	// Blink by timer if there is no clock source
	//   internal stopped                            or    external time out
	if( (  !TEMPOISRUNNING && sync==INTERNAL_SYNC ) || (sync!=INTERNAL_SYNC && tempoMeasure==255) )
	{
		if(dimblink_led_ctr == 0)
		{
			// turn off
			blink_leds_off();
		}
		else if(dimblink_led_ctr == 128)
		{
			// turn on
			blink_leds_on();
		}
	}
}


#ifdef __AVR_ATmega162__
// Tempo Knob (port pin changed)  interupt 
ISR(PCINT0_vect)
{
	static uint8_t last_tk;
	uint8_t curr_tk;

	// tempo knob change!
	curr_tk = TEMPO_PIN & 0x3;	// pins A0 and A1

//	if (IS_SET1( SETTINGS1_ENC_Q1 ) )
	{	
		if((last_tk == 3) && (curr_tk == 2) )
			tk_value--; 

		if((last_tk == 2) && (curr_tk == 3) )
			tk_value++;
	}

	if (IS_SET2( SETTINGS2_ENC_Q2 ) )
	{
		if((last_tk == 1) && (curr_tk == 3) )
			tk_value--;

		if((last_tk == 3) && (curr_tk == 1) )
			tk_value++;
	}

/*
	if (IS_SET1( SETTINGS1_ENC_Q3 ) )
	{
		if((last_tk == 1) && (curr_tk == 0) )
			tk_value++;

		if((last_tk == 0) && (curr_tk == 1) )
			tk_value--;
	}

	if (IS_SET1( SETTINGS1_ENC_Q4 ) )
	{
		if((last_tk == 0) && (curr_tk == 2) )
			tk_value++;

		if((last_tk == 2) && (curr_tk == 4) )
			tk_value--;
	}
*/

	last_tk = curr_tk;

}
#endif 



// the loop for the simple modes - just "all in one" to save some bytes 
void dumb_functions(uint8_t func)
{
#if !ALL_FIXED_SETTING
	int8_t which_setting=0;
#endif 
#ifdef DACBITS_TEST
	uint8_t notePort=NOTE_PORT; 
//	uint8_t gatePort=0; 
#endif 
	// set_syncmode();
	while (func==function)
	{
		read_switches();
#ifdef DACBITS_TEST		
		if(function == EDIT_TRACK_FUNC)
		{
			static const uint8_t notePortKeys[]={ KEY_C, KEY_D,KEY_E,KEY_F, KEY_G, KEY_A, KEY_SLIDE, KEY_ACCENT }; 
			uint8_t i; 
				
			/*
				a) Port C0..5 = 6Bit Dac for note CV (pitch) (Keys C=0, D=1; E=2; F=3, G=3, A=4)
				b) Port C6 = Slide, Key=Slide 
				c) Port C7 = Accent, Key Accent (inverted, so LED OFF=High=NO ACCENT / LED-ON=Low=ACCENT ) 
				d) Port E1 = GATE, Key RUN/STOP LED_On=High=GateON
				e) Port E2 = NoteLatch, Key NEXT, Latches on Rising edge 
				
			*/
//			if(just_pressed(KEY_RS))
			{
	//			gatePort=!gatePort; 
	//			if(gatePort)
				if(is_pressed(KEY_RS))
				{
					sbi(GATE_PORT, GATE_PIN);
					set_led(LED_RS);
				}
				else 
				{
					cbi(GATE_PORT, GATE_PIN);
					clear_led(LED_RS);
				}
			}
			if(is_pressed(KEY_NEXT))
			{
				sbi(NOTELATCH_PORT, NOTELATCH_PIN);
				set_led(LED_NEXT);
			}else
			{
				cbi(NOTELATCH_PORT, NOTELATCH_PIN);
				clear_led(LED_NEXT);
			}
			
			for(i=0;i<8;++i)
			{
				if(just_pressed(notePortKeys[i]))
				{
					notePort^=(1<<i);
				}
				if(notePort &(1<<i))
					set_key_led(notePortKeys[i]);
				else
					clear_key_led(notePortKeys[i]);	 
			}
			NOTE_PORT = notePort ^ ACCENTPIN;
		}
#endif 
#ifdef KEYLED_TEST
		if(function == COMPUTER_CONTROL_FUNC)
		{	
			int8_t i;
			for(i=0;i<24;++i)
			{
				if(switches[i>>3]&(1<<(i&7)))
					set_key_led(i);
				else
					clear_key_led(i);
			}	
			set_bank_leds(bank); 
		}
#endif 
#if !ALL_FIXED_SETTING
		if(function == C_FUNC)
		{
			int8_t		i,m;
			uint8_t new_settings;

			m=get_lowest_blackkey_just_pressed(); 
			m--; 
			if(m>=0 && m<NUM_C_SETTINGS)
				which_setting=m; 

			for(i = 0; i < NUM_C_SETTINGS; ++i)
			{
				if(i!=which_setting)
					clear_key_led(blackkey_tab[i]); 
				else 
					set_key_led(blackkey_tab[i]);
			}

			new_settings = settings[which_setting];

			m=1;
			for(i = 0; i < 8; ++i)
			{
				if(new_settings & m)
					set_numkey_led(i + 1);
				else
					clear_numkey_led(i + 1);
				if(i == get_lowest_numkey_just_pressed() - 1)
					new_settings ^= m;
				m<<=1;
			}
			if(settings[which_setting] != new_settings)
			{
				internal_eeprom_write8(USERSETTINGS_EEADDR+which_setting, new_settings);
				settings[which_setting] = new_settings;
			}
		}
#endif 
	}
}


void setup_scales()
{
/*
    uint8_t of;

    if(IS_SET1(SETTINGS1_DIM2nd))
    {
	    scaleCorrTab[0][1] = scaleCorrTab[1][1] = 0 ;
	    scaleCorrTab[0][2] = scaleCorrTab[1][2] = -1;
	    of=2;
    }
    else
		of=0;

    if(IS_SET1(SETTINGS1_HARM_MIN))
    {
	    scaleCorrTab[1][10] = 1;
	    scaleCorrTab[1][11] = 0;
	    of+=1;
    }
    
    scaledNotes[2] = *(&scaleModifications[0][0][0]+of);
	of+=4; 
    scaledNotes[3] = *(&scaleModifications[0][0][0]+of);
*/

    uint8_t dm2;
    uint8_t hrm;

	if(IS_SET1(SETTINGS1_DIM2nd))
	{
		scaleCorrTab[0][1] = scaleCorrTab[1][1] = 0 ;
		scaleCorrTab[0][2] = scaleCorrTab[1][2] = -1;
		dm2=1;
	}
	else
		dm2=0; 

	if(IS_SET1(SETTINGS1_HARM_MIN))
    {
	    scaleCorrTab[1][10] = 1;
		scaleCorrTab[1][11] = 0;
		hrm=1; 
	}
	else
		hrm=0; 
	
    scaledNotes[2] = scaleModifications[0][dm2][hrm];
    scaledNotes[3] = scaleModifications[1][dm2][hrm];
 

 /*   
    scaleCorrTab[0][1] = scaleCorrTab[1][1] = dm2 ? 0 : +1;
    scaleCorrTab[0][2] = scaleCorrTab[1][2] = dm2 ? -1 : 0;


    scaleCorrTab[1][10] = hrm ? +1 : 0;
    scaleCorrTab[1][11] = hrm ? 0 : -1;
*/
}


/*
*
*	Timer 0, Interrupt each 1ms. CTC mode 
*
*/
void init_timer0(void)
{
#ifdef __AVR_ATmega162__
	sbi(TIMSK, OCIE0);	// timer0 overflow interrupt enable
	TCCR0 = (1 << WGM01) | 0x3; // compare mode, clk/64
	OCR0 = 249; // 1KHz, divider value = count-1!
#endif 
#ifdef __AVR_ATmega2561__
	sbi(TIMSK0, OCIE0A);	// timer0 overflow interrupt enable
	TCCR0A = (1 << WGM01);	// compare mode (CTC ), no outputs
    TCCR0B = (1<<CS01) | (1<<CS00);	// clk/64
	OCR0A = 249; // 1KHz, divider value = count-1!
#endif
}

void init_tempo(void)
{
	// sets up all the timer things for us: 
	change_tempo(internal_eeprom_read8(TEMPO_EEADDR_L));	

	// Port pin change interrupt for tempo knob
	// On MEGA2561 the tempo knob is polled - so no code needed here
#ifdef __AVR_ATmega162__
	sbi(PCMSK0, PCINT0);	// detect change on pin A0
	sbi(PCMSK0, PCINT1);	// detect change on pin A1
	sbi(GICR, PCIE0);		// enable pin change interrupt for tempo knob detect
#endif 

	// don't turn timer on here, thats done by set_sync() on each mode startup if required
}


void turn_on_tempo()
{
	sbi(CURRENT_CPU_TIMSK3, OCIE3A);	
}

void turn_off_tempo()
{
	clear_led(LED_TEMPO);
	cbi(CURRENT_CPU_TIMSK3, OCIE3A);	
}

// calculate the setting for timer 3
// limited to real used range from 20 to 255 BPM 
// which is checked & clipped 
// newtempo is 16 bit, as we have 16 bit data sources, so they don't have to care. 
void change_tempo(uint16_t newtempo)	 
{
	uint32_t	divider;

	if(newtempo < MIN_TEMPO)
		newtempo = MIN_TEMPO;
	if(newtempo > MAX_TEMPO)
		newtempo = MAX_TEMPO;

	tempo = newtempo;

	send_tempo(newtempo);

   if ( IS_SET0(SETTINGS0_NOSTORETEMPO) ==0 )
	   internal_eeprom_write8(TEMPO_EEADDR_L, tempo);

/*
	calculate settings for timer 3 as clock for current tempo
	  divider =  60 * CPU_FREQUENCY; 
	  divider /= set_tempo;
	  divider /= 4;         // sixteenth notes!
	  divider /= 2;         // call twice per quarter
	  divider /= DINSYNC_PPQ/4;  // do dinsync on same interrupt
*/
	divider = F_CPU * 60;
	divider /= newtempo * 2 * DINSYNC_PPQ;
	divider >>=3; // div@ max.Tempo 255 is 78431, so prescaler /8 (next to 1) is always needed 

#if  F_CPU*60/8/MIN_TEMPO/2/DINSYNC_PPQ > 65535  // larger divider needed for lowest tempo ? 
	if(divider>65535)
	{
	#if defined ( __AVR_ATmega162__ ) 
		divider >>=1; // prescaler  /16 already fits min. Tempo 20, so no more checks needed   
		TCCR3B= (1<<CS32) | (1<<CS31) | (1<<WGM32);	// Prescaler 16, CTC Mode (using OCR3A)
		delay_clock9ms=F_CPU/16/111;  // 9ms=111 Hz 
	#elif defined ( __AVR_ATmega2561__ )
		divider >>=3; // next available prescaler  /64 fits min. Tempo 20, so no more checks needed   
		TCCR3B= (1<<CS31) | (1<<CS30) | (1<<WGM32);	// Prescaler 64, CTC Mode (using OCR3A)
		delay_clock9ms=F_CPU/64/111;  // 9ms=111 Hz 
	#endif  
	}
	else
#endif // 2nd divider needed 
	{
		TCCR3B=(1<<CS31) | (1<<WGM32); // use prescaler /8 , CTC Mode 
		delay_clock9ms=F_CPU/8/111;  // 9ms=111 Hz
	}
	OCR3A = --divider;	// set OCR (output compare register) to clock divider;
	if(TCNT3>divider)	// avoid a lengthy wrap around cycle ...
		TCNT3=0;		// ... if we just moved the OCR below the current count value 
}

// set bank led to current value of currently edited tempo knob parameter 
void tempoval_led(void)
{
	uint8_t val;

	if(!tempoKnobMode)					// not tempo knob mode 
		val=255;						// does not lit any LED 
	else
	{
		val = *tk_paras[tempoKnobMode]; // get value from parameter pointer list 
		if(tempoKnobMode < 6)			// scale it 
			val= val / 4;
		else if(tempoKnobMode == 6)
			val= val * 15 / 100;
		else if(tempoKnobMode == 7)
			val= val * 4 / 10 ;
	}
	set_bank_leds(val);					// set bank LEDs accordingly 
}

// tempo knob mode was active and tempo knob was moved (+/- ticks) 
// so set the parameter accordingly. 
void changeTempoKnobValue(int8_t moved_ticks)
{
	uint8_t *param;
	int16_t max;
	int16_t val; 

	val=moved_ticks; 
	param=tk_paras[tempoKnobMode];
	max=tk_maxval[tempoKnobMode];
	if(tempoKnobMode==6)
		val*=2; 
	val+=*param; 

	if(val<0)
		val=0;
	if(val>max)
		val=max; 
	*param=val; 
	tempoval_led();
}


//void dispatch_note_off(int8_t pitch_shift)
void dispatch_note_off()
{
	isSlide = (curr_note | all_slide ) & SLIDE ;
	
    uint8_t modifiedSlideRand = slideRand;
    
    if (wasAcc && !isSlide && slideRand<40)
        modifiedSlideRand *= 2;
    
    if(randomIf(modifiedSlideRand) && variationAllowed())
		isSlide ^= SLIDE;
        
	if(curr_note != 0xFF)
	{
		if(isSlide)
		{	// if the note had slide on it
			note_off(1);	// slide
			// DONT send a midi note off
		}
		else
		{
			note_off(0);	// no slide
			if((curr_note & NOTE_MASK) != REST_NOTE) // not rest
			{
				midi_send_note_off(noteSent);
			}
			else
				midi_send_note_off(curr_note);
		}
	}

	// turning off old notes that were glided
	if( prev_note != 0xFF && prevSlide 
		&& (prev_note&NOTE_MASK)!=(curr_note&NOTE_MASK))
	{
		if((prev_note & NOTE_MASK) != REST_NOTE)		// not rest
		{
			midi_send_note_off(prevNoteSent);
		}
		else
			midi_send_note_off(prev_note);
	}
}


void dispatch_note_on(int8_t pitch_shift)
{
	if(all_rest)
		curr_note &= 0xC0;
    
	uint8_t note = curr_note & NOTE_MASK;

	uint8_t doVariantion = variationAllowed();

	if(doVariantion && randomIf(restRand) )
		note = 0;

	int8_t	ps;

	if(!note)
		ps = 0;
	else
	{
		ps = note + pitch_shift;

		if(doVariantion)
		{
			if(randomIf(octRand))
				ps += 12;
			else if(randomIf(octDownRand))
				ps -= 12;
		}
	}

	// process transposition and scale correction!
	if(scaleCorrType > 0 && note)	//not a rest
	{
		int8_t	r = (ps + 1 - scaleCorrRoot + 144) % 12;
		ps += scaleCorrTab[scaleCorrType - 1][r];
	}

	// place final note in valid range
	while(ps > HIGHESTNOTE)			//highest
		ps -= OCTAVE;
	while(note && ps < LOWESTNOTE)	//lowest C!
		ps += OCTAVE;


	prevNoteSent = noteSent;
	noteSent = ps;

	uint8_t isAcc = (curr_note | all_accent)  & ACCENT;

	if(doVariantion && randomIf(accentRand) )
		isAcc  ^= ACCENT;
        
    wasAcc = isAcc;

	prevSlide = isSlide;// isSlide was set in last notes noteOff!

	note_on((uint8_t) ps | prevSlide  // slide is from prev note!
					     | isAcc);	  // accent

	if (prevSlide && ps == prevNoteSent)
		return;

	midi_send_note_on((uint8_t) ps | isAcc);
}

void load_next_chain(uint8_t reset)
{
	if(!chains_equiv(next_chain, curr_chain) || (curr_bank != next_bank))
	{
		cpy_curr_chain();	// copy next pattern chain into current pattern chain

		if(reset)
			curr_chain_index = 0;	// reset to beginning

		// reset the pitch
		next_pitch_shift = curr_pitch_shift = 0;

		clear_note_leds();
	}
	curr_bank = next_bank;
	curr_pitch_shift = next_pitch_shift;
}

void measureTempo()
{
	static uint8_t lastMeasure = 5;//whatever
	static uint8_t sameTempoCount = 0;//whatever
	
	// this really does the trick now
	// it takes some seconds until it finds a reliable set of measures,
	// but then its correct, and fixes the old problem
	// of missing notes / hung notes when MIDI Sync + high Gate + high swing 
	if (tempoMeasure == lastMeasure) 
	{
		sameTempoCount++;
	} else
	{
		sameTempoCount=0;
		lastMeasure = tempoMeasure;
	}
	
	uint8_t cond =  IS_SET1(SETTINGS1_FAST_TEMPOMEAS)?1:2;
	if(sameTempoCount>cond) // 2 was too less (1.4.1), with 4 it takes ages, but 3 works, lucky us. 
		midi_tempo = 2500 / lastMeasure;

	tempoMeasure = 0;
}



/*
 miniature "pseudo random number generator".
 Better call these pseudo random sequence counters. Counts from 1 to 65535 in a funny way. 
 As one step to the next has several "just shifted" bits, it is counted up 8 times each call, 
 so the lower 8 bits have the "not too bad" random behavior of a LFSR.
*/
uint8_t random_x(uint8_t x)
{
	static int16_t	randState=1;
	uint8_t i; 

	// Galois LFSR ( http://en.wikipedia.org/wiki/Linear_feedback_shift_register )
	// the polynomial is mirrored (x^15 is bit 0) and shifting thus to the left. 
	//  bits 0, 2,3,5 (x^15 + x^13 + x^12 + x^10)
	// 1011 0100 0000 0000 = b400  => mirror 2d
	// you might think of "A55A" ... but that is 4 bytes more code! 
	// (... see polynomials: http://users.ece.cmu.edu/~koopman/lfsr/index.html )
	for(i=0;i<8;++i) // save some time - as we really need only 8 fresh bits each time 
	{
		if(randState < 0)
		{
			randState <<= 1;
			// 76543210
			// 00101101=0x2d
			randState ^= 0x2d;
		}
		else
			randState <<= 1;
	}
	if(x)
		return (uint8_t)(((uint16_t)randState)%x); // not really good ... but distribution is good enough for our purpose 
	else
		return (uint8_t) randState;

}

uint8_t random100(void)
{
	return random_x(100);
}

#ifndef randomIf
uint8_t randomIf(uint8_t percent)
{
	uint16_t p; 
	p=percent;
	p*=256;
	p/=100;
	if(random_x(0) < (uint8_t)p)
		return 1; 
	else
		return 0;
}
#endif 

//**************************************************
//         Internal EEPROM
//**************************************************
uint8_t internal_eeprom_read8(uint16_t addr)
{
	loop_until_bit_is_clear(EECR, CURRENT_CPU_EE_WRITE_EN);	// wait for last write to finish
	EEAR = addr;
	sbi(EECR, EERE);	// start EEPROM read
	return EEDR;		// takes only 1 cycle
}

void internal_eeprom_write8(uint16_t addr, uint8_t data)
{
	loop_until_bit_is_clear(EECR, CURRENT_CPU_EE_WRITE_EN);	// wait for last write to finish

	EEAR = addr;
	EEDR = data;
	cli();	// turn off interrupts
	sbi(EECR, CURRENT_CPU_EE_WRITE_PREPARE);	// these instructions must happen within 4 cycles
	sbi(EECR, CURRENT_CPU_EE_WRITE_EN);
	sei();	// turn on interrupts again
}




