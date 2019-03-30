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
#include <stdio.h>
#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#include "pattern.h"
#include "switch.h"
#include "led.h"
#include "main.h"
#include "compcontrol.h"
#include "eeprom.h"


#define UART_BUFF_SIZE		64	// cOnbOx expected minimum, is maximum packet size, so don't make smaller! 
static uint8_t				recv_msg_buff[UART_BUFF_SIZE];
static uint8_t				tx_msg_buff[UART_BUFF_SIZE];
static uint8_t				recv_msg_i = 0;

volatile uint16_t			uart_timeout = 0;	// timeout for messages, count up by 1ms timer 

static volatile uint8_t		uart_cmd; 

/* 
	make a proper packet "around" the data just filled in the buffer  
	kills some doubled up code and thus saves 62 bytes 
*/
void pack_n_send(uint8_t msg, uint8_t sz);


static const unsigned char fw_vers[]  =  {5,0, VERSION ,0, 'M','a', 'r','O', 's','1','5',0x61 }; // ID low, ID High, Version Low, Version High, Name,CRC 

/*
*	do_uart_cmd() is called from the main loop periodically. 
*	It evaluates the messages from the UART (=USB) interrupt. 
*	As it is *not* an interrupt, but running in main there are no problems with 
*	synchronizing date, write access to eeprom (...). Also no need for volatile. 
*	Except for the message signaling variable uart_cmd itself. 
*
*	Note: the communication is half duplex: cOnbOx sends us one 
*	command, we answer, answer is evaluated. 
*	Only than the next command should be send. 
*	I/O is buffered, but not capable of starting the next transmit when the last 
*	transmit has not been completely(!) send. 
*
*/
void do_uart_cmd(void)
{
	switch(uart_cmd)
	{
//	case OVERFLOW_MSG:
//		send_status(UART_BUFF_SIZE); //???
//		break;
	case DONE_MSG: 
	case 0:	
			return; // just nothing to do 

	// If we get to here, the message has passed the CRC and is  assumed to be valid.  Now we process the message.
	case PING_MSG:
		send_status(0x1);
		break;
	case GET_TEMPO_MSG:
		send_tempo(tempo);
		break;
	case SET_TEMPO_MSG:
		{
			if(recv_msg_buff[2] != TEMPO_MSG_LEN)
			{
				send_status(0);
				break;
			}
			change_tempo(recv_msg_buff[4]);
			break;
		}
	case RD_PATT_MSG:
		{
			uint8_t		bank, patt;
			uint16_t	addr;

			if(recv_msg_buff[2] != RD_PATT_MSG_LEN)
			{
				send_status(0);
				break;
			}
			bank = recv_msg_buff[3];
			patt = recv_msg_buff[4];
			addr = PATTERN_MEM + bank * BANK_SIZE + patt * PATT_SIZE;
			spieeprom_read(&tx_msg_buff[3],addr,PATT_SIZE);
			pack_n_send(PATT_MSG, 3 + PATT_SIZE);
			break; 
		}
	case WR_PATT_MSG:
		{
			uint8_t		bank, patt;
			uint16_t	addr;
			if(recv_msg_buff[2] != WR_PATT_MSG_LEN)
			{
				send_status(0);
				break;
			}
			bank = recv_msg_buff[3];
			patt = recv_msg_buff[4];
			addr = PATTERN_MEM + bank * BANK_SIZE + patt * PATT_SIZE;
			spieeprom_write(&recv_msg_buff[5],addr,PATT_SIZE);
			send_status(1);
			break;
		}
	case MSG_FW_VER:
		{
			memcpy(&tx_msg_buff[3],fw_vers,sizeof(fw_vers)-1 );
			pack_n_send(MSG_FW_VER,sizeof(fw_vers)-1 +3);
		}
		break;
	case INVALID_MSG:
	default:
		send_status(0);
		break;
	}
	uart_cmd=DONE_MSG; 
}

// interrupt on receive char
#ifdef __AVR_ATmega162__
ISR(USART1_RXC_vect)
#endif
#ifdef __AVR_ATmega2561__
ISR(USART1_RX_vect)
#endif
{
	uint8_t		cmd, crc;
	uint16_t	size;
	char		c = UDR1;

	if(uart_cmd==DONE_MSG)	// pseudo message - set when msg has been evaluated and we are ready for next one 
	{
		recv_msg_i=0;
		uart_cmd=0;
	}

	if(uart_timeout > 1000)
	{
		recv_msg_i = 0; // start over... but don't send status!
	}

	if(recv_msg_i < UART_BUFF_SIZE)
	{
		recv_msg_buff[recv_msg_i++] = c;	// place at end of q
	}
	else
	{
		// Receive failure.  Start over.
		uart_cmd=OVERFLOW_MSG;
	}

	uart_timeout = 0;

	// The header has been received.  Start grabbing the content and the CRC. 
	if(recv_msg_i >= 3)
	{
		cmd = recv_msg_buff[0];
		size = recv_msg_buff[1];
		size <<= 8; // size is just the body size
		size |= recv_msg_buff[2];

		if(recv_msg_i >= 4 + size)
		{	// header+foot is 4 bytes long
			crc = recv_msg_buff[3 + size];	// CRC is the last byte of the packet
			if(crc != calc_CRC8(recv_msg_buff, size + 3))
			{
				uart_cmd=INVALID_MSG; 
			}
			else 
				uart_cmd=cmd; 
		}
	}
}

volatile uint8_t serq_tx_wr;
volatile uint8_t serq_tx_rd;
ISR(USART1_UDRE_vect)
{
	uint8_t c;
	if(serq_tx_wr == serq_tx_rd)
		UCSR1B &= ~(1<<UDRIE1) ; // disable interrupt if no more chars to send 
	else
	{
		c = tx_msg_buff[serq_tx_rd++];
		//	serq_tx_rd %= UART_BUFF_SIZE;		// not needed as the buffer is *not* used circular 
		UDR1 = c;
	}
}


void pack_n_send(uint8_t msg, uint8_t len)
{
	tx_msg_buff[0] = msg;
	tx_msg_buff[1] = 0;
	tx_msg_buff[2] = len-3;
	tx_msg_buff[len] = calc_CRC8(tx_msg_buff, len);
	len++;
	cli();
	serq_tx_rd=0;
	serq_tx_wr=len;
	sei();

	UCSR1B |= (1<<UDRIE1) ; // enable tx interrupt - starts sending the buffer 
}

void send_status(uint8_t stat)
{
	tx_msg_buff[3] = stat;
	pack_n_send(STATUS_MSG, 4);
}

void send_tempo(uint8_t t)
{
	tx_msg_buff[3] = 0;
	tx_msg_buff[4] = t;
	pack_n_send(TEMPO_MSG, 5);
}
/* 
 * Adapted from: http://cell-relay.indiana.edu/mhonarc/cell-relay/1999-Jan/msg00074.html
 * 8 bit CRC Generator, MSB shifted first
 * Polynom: x^8 + x^2 + x^1 + 1
 * 
 * Calculates an 8-bit cyclic redundancy check sum.
 */
#if !defined(FLASHCRCCHECK)
uint8_t calc_CRC8(uint8_t *buff, uint16_t size)
{
	uint8_t x;
	uint8_t crc = 0;

	while(size)
	{
		--size;

#ifdef FASTCRC
		x = crc ^ *buff++;
		crc = 0;

		if(x & 0x01)
			crc ^= 0x07;
		if(x & 0x02)
			crc ^= 0x0E;
		if(x & 0x04)
			crc ^= 0x1C;
		if(x & 0x08)
			crc ^= 0x38;
		if(x & 0x10)
			crc ^= 0x70;
		if(x & 0x20)
			crc ^= 0xE0;
		if(x & 0x40)
			crc ^= 0xC7;
		if(x & 0x80)
			crc ^= 0x89;
#else

		static const uint8_t crctab[]={0x07,0x0e,0x1c,0x38,0x70,0xe0,0xc7,0x89};

		x = crc ^ *buff++;
		crc = 0;
	
		uint8_t m=1;

		for(uint8_t i=0;i<8;++i)
		{
			if(x&m)
			crc^= crctab[i];
			m<<=1;
		}
#endif 
	}
	return crc;
}
#else


uint8_t crc8(uint8_t crc, uint8_t c)
{
	uint8_t x;

#ifdef FASTCRC
	x = crc ^ c;
	crc = 0;

	if(x & 0x01)
		crc ^= 0x07;
	if(x & 0x02)
		crc ^= 0x0E;
	if(x & 0x04)
		crc ^= 0x1C;
	if(x & 0x08)
		crc ^= 0x38;
	if(x & 0x10)
		crc ^= 0x70;
	if(x & 0x20)
		crc ^= 0xE0;
	if(x & 0x40)
		crc ^= 0xC7;
	if(x & 0x80)
		crc ^= 0x89;
#else

	static const  uint8_t crctab[]={0x07,0x0e,0x1c,0x38,0x70,0xe0,0xc7,0x89};

	x = crc ^ c;
	crc = 0;
	
	uint8_t m=1; 

	for(uint8_t i=0;i<8;++i)
	{
		if(x&m)
			crc^= crctab[i];
		m<<=1; 
	}
#endif 	
	
	return crc;
}
uint8_t calc_CRC8(uint8_t *buff, uint16_t size)
{
	uint8_t crc = 0;

	while(size)
	{
		--size;
		crc = crc8(crc,*buff++);
	}
	return crc; 
}


extern uint8_t __data_load_end; // End of .data section from the linker  

uint8_t checkFlash()
{

	uint16_t i; 
	uint8_t crc=0; 
	i = (uint16_t) &__data_load_end;
	 
//15626
	do{
		--i;
		crc=crc8(crc, pgm_read_byte((void *)i) );
	}while(i); 

/*
//15632
	uint16_t i;
	uint8_t crc=0;
	const uint16_t	fsiz = (uint16_t) &__data_load_end;
	
	for(i=0;i<fsiz;++i)
		crc=crc8(crc, pgm_read_byte((void *)i) );
*/
	return crc; 
}
#endif 
