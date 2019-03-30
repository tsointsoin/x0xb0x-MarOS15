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
#include "main.h"
#include "synth.h"
#include "delay.h"
#include "led.h"
#include "midi.h"
#include "switch.h"
#include "pattern.h"
#include "eeprom.h"


void do_keyboard_mode(void)
{
	signed int shift = 0;
	uint8_t slide = 0; 
    uint8_t accent = 0; 
	uint8_t i; 

	has_bank_knob_changed();			// ignore startup change
	while(1)
	{
		read_switches();

		if(function != KEYBOARD_MODE_FUNC)
		{
			midi_notesoff();			// turn all notes off
			return;
		}

		// show the current MIDI address
		set_bank_leds(midi_out_addr);

		if(has_bank_knob_changed())
		{
			// bank knob was changed, which means they want a different
			// midi addr... OK then!
			midi_out_addr = bank;

			// set the new midi address (burn to EEPROM)
			internal_eeprom_write8(MIDIOUT_ADDR_EEADDR, midi_out_addr);
		}

		// show the octave
		display_octave_shift(shift);
		for(i = 0; i < 13; i++)
		{
			// check if any notes were just pressed
			if(just_pressed(loopkey_tab[i]))
			{
				note_on(((C2 + i) + shift*OCTAVE) | slide | accent);
				midi_send_note_on( ( (C2 + i) + shift*OCTAVE) | accent );
				slide = SLIDE;

				// turn on that LED
				set_notekey_led(i);	
			}

			// check if any notes were released
			if(just_released(loopkey_tab[i]))
			{
				midi_send_note_off( ( (C2 + i)  + shift*OCTAVE) /* | accent */ );

				// turn off that LED
				 clear_notekey_led(i);
			}
		}

        if (just_pressed(KEY_UP))
        {
          if (shift < 2)
                shift++;
        } 
        else if (just_pressed(KEY_DOWN))
        {
          if (shift > -1)
            shift--;
        } 

		// check if they turned accent on
		if (just_pressed(KEY_ACCENT)) 
		{
			accent ^= ACCENT;
          
			if (accent)
				set_led(LED_ACCENT);
			else
				clear_led(LED_ACCENT);
		}
	
		// if no keys are held down and there was a note just playing
		// turn off the note.
		if((NOTE_PIN & 0x3F) && no_keys_pressed())
		{
			note_off(0);
			slide = 0;
			clear_note_leds();
		}
	}
}
