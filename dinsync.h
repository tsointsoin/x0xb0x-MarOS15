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
#define DINSYNC_PPQ			24	// roland's dinsync standard pulses per quarternote
#define DINSYNC_DISABLED	0
#define DINSYNC_OUT			1
#define DINSYNC_IN			2

#define DINSYNC_START		7
#define DINSYNC_4			6
#define DINSYNC_5			5
#define DINSYNC_CLK			4
#define DINSYNC_DATA		PORTD
#define DINSYNC_PINS		PIND
#define DINSYNC_DDR			DDRD

#define DINSYNC_IS_START  (DINSYNC_PINS & (1<<DINSYNC_START))

#ifdef DIN_SYNC_IN // should not be known if not there - so the compiler warns you with line number, not just link errors w/o 
void	dinsync_set_out(void);
void	dinsync_set_in(void);
uint8_t dinsync_stopped(void);
uint8_t dinsync_started(void);
uint8_t dinsync_rs(void);
#endif
void	dinsync_stop(void);
void	dinsync_start(void);


extern uint8_t dinsync_counter;
extern uint8_t swing_counter;
