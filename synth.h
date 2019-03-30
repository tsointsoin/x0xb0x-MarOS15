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

#include <avr/io.h>	// get our CPU type 

#ifdef __AVR_ATmega162__
	#define NOTELATCH_PORT	PORTE
	#define NOTELATCH_PIN	2
	#define NOTE_PORT		PORTC
	#define NOTE_PIN		PINC
	#define GATE_PIN		1
	#define GATE_PORT		PORTE
#endif
#ifdef __AVR_ATmega2561__
	#define NOTELATCH_PORT	PORTB
	#define NOTELATCH_PIN	4
	#define NOTE_PORT		PORTC
	#define NOTE_PIN		PINC
	#define GATE_PIN		5
	#define GATE_PORT		PORTB
#endif

// one octave is 12 notes
#define OCTAVE	12

/* between 0x0 and 0xA, the VCO voltage pins, so these notes aren't really
 * 'effective' in that they all sound the same. 
 */

// lowest octave
#define C1			0x0B	// 11
#define C1_SHARP	0x0C	// 12
#define D1			0x0D	// 13
#define D1_SHARP	0x0E	// 14
#define E1			0x0F	// 15
#define F1			0x10	// 16
#define F1_SHARP	0x11	// 17
#define G1			0x12	// 18
#define G1_SHARP	0x13	// 19
#define A2			0x14	// 20
#define A2_SHARP	0x15	// 21
#define B2			0x16	// 22
// middle octave
#define C2			0x17	// 23
#define C2_SHARP	0x18	// 24
#define D2			0x19	// 25
#define D2_SHARP	0x1A	// 26
#define E2			0x1B	// 27
#define F2			0x1C	// 28
#define F2_SHARP	0x1D	// 29
#define G2			0x1E	// 30
#define G2_SHARP	0x1F	// 31
#define A3			0x20	// 32
#define A3_SHARP	0x21	// 33
#define B3			0x22	// 34
// high octave
#define C3			0x23	// 35
#define C3_SHARP	0x24	// 36
#define D3			0x25	// 37
#define D3_SHARP	0x26	// 38
#define E3			0x27	// 39
#define F3			0x28	// 40
#define F3_SHARP	0x29	// 41
#define G3			0x2A	// 42
#define G3_SHARP	0x2B	// 43
#define A4			0x2C	// 44
#define A4_SHARP	0x2D	// 45
#define B4			0x2E	// 46
#define C4			0x2F	// 47
// highest octave
#define C4_SHARP	0x30	// 48
#define D4			0x31	// 49
#define D4_SHARP	0x32	// 50
#define E4			0x33	// 51
#define F4			0x34	// 52
#define F4_SHARP	0x35	// 53
#define G4			0x36	// 54
#define G4_SHARP	0x37	// 55
#define A5			0x38	// 56
#define A5_SHARP	0x39	// 57
#define B5			0x3A	// 58
#define C5			0x3B	// 59
// above highest octave 
#define C5_SHARP	0x3C	// 60
#define D5			0x3D	// 61
#define D5_SHARP	0x3E	// 62
 // 3F not usable / pattern end marker 

#define LOWESTNOTE  C1
#define HIGHESTNOTE D5_SHARP


#define SLIDE		0x80
#define SLIDEPIN    0x40
#define ACCENT      0x40	// used throughout the software (pattern memory and up) 
#define ACCENTPIN   0x80	// just used at the port-pin. (Schematics looks like these where swapped in error - fixed by a few bytes of software) 

// no more notes!

void	note_on(uint8_t note); // , uint8_t slide, uint8_t accent);
void	note_off(uint8_t slide);
uint8_t is_playing(uint8_t note);

