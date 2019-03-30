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
#include "eeprom.h"
#include "main.h"


/*
*
*	Using page mode saves a lot of bytes, but be careful: 
*	you can write only up to the next page boundary! 
*	That is (addr % 32) + 31. Page size = 32 
*	Currently all writes are <=16 bytes 
*	and all writes start on multiples of 16 - which fits perfect 
*
*/
void spieeprom_write(uint8_t *data, uint16_t addr, uint8_t len)
{
	uint8_t status;

	/* check if there is a write in progress, wait */
	while(1)
	{
		cli();
		cbi(SPIEE_CS_PORT, SPIEE_CS_PIN);	// pull CS low
		NOP;

		SPDR = SPI_EEPROM_RDSR;
		while(!(SPSR & (1 << SPIF)))
			;
		SPDR = 0;
		while(!(SPSR & (1 << SPIF)))
			;
		status = SPDR;
		sbi(SPIEE_CS_PORT, SPIEE_CS_PIN);	// set CS high

		if((status & 0x1) == 0)
			break;
		sei();	// leave interrupts a chance to be taken. Writes might take 5ms, that is more than interrupts are allowed to be off!
		NOP;
	}


	/* set the spi write enable latch */
	cbi(SPIEE_CS_PORT, SPIEE_CS_PIN);		// pull CS low
	NOP;
	SPDR = SPI_EEPROM_WREN; // send command
	while(!(SPSR & (1 << SPIF)))
		;
	sbi(SPIEE_CS_PORT, SPIEE_CS_PIN);	// set CS high
	NOP;
	// wait for write enable latch
	cbi(SPIEE_CS_PORT, SPIEE_CS_PIN);	// pull CS low
	NOP;
	SPDR = SPI_EEPROM_WRITE;			// send command
	while(!(SPSR & (1 << SPIF)))
		;

	SPDR = addr >> 8;	// send high addr
	while(!(SPSR & (1 << SPIF)))
		;

	SPDR = addr & 0xFF; // send low addr
	while(!(SPSR & (1 << SPIF)))
		;
	
	while(len)
	{
		SPDR = *data++;		// send data
		len--;
		while(!(SPSR & (1 << SPIF)))
			;
	}
	
	sbi(SPIEE_CS_PORT, SPIEE_CS_PIN);	//  set CS high
	sei();
}

// there is no page limit on reads - it just should not take too long 
void spieeprom_read(uint8_t *data, uint16_t addr, uint8_t len)
{
	cli();

	cbi(SPIEE_CS_PORT, SPIEE_CS_PIN);	// pull CS low
	NOP;

	SPDR = SPI_EEPROM_READ; // send command
	while(!(SPSR & (1 << SPIF)))
		;

	SPDR = addr >> 8;		// send high addr
	while(!(SPSR & (1 << SPIF)))
		;

	SPDR = addr & 0xFF;		// send low addr
	while(!(SPSR & (1 << SPIF)))
		;

	while(len)
	{
		SPDR = 0;
		len--;
		while(!(SPSR & (1 << SPIF)))
			;
		*data++ = SPDR;
	}

	sbi(SPIEE_CS_PORT, SPIEE_CS_PIN);	//   set CS high
	sei();
}
