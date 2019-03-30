#include <inttypes.h>

#include "main.h"
#include "randomizer.h"
#include "synth.h"
#include "pattern.h"

 static const uint8_t __flash randSetups[13][12] = { 
//		scaled 	upDown	down	rest	multi	3step	4step	acc		glide,	repeat,	halfScale,"waup"	
 { 		20,		20,		20,		0,		0,		0,		0,		25,		0,      35,     40,		10		},   // all on, no glides
 { 		20,		20,		20,		0,		0,		0,		0,		25,		25,	 	30,     30,		20		},   // all on
 { 		50,		40,		20,		0,		0,		0,		0,		25,		35,	 	25,     30,		15		},   // all on complex
 { 		30,		20,		20,		20,		10,		0,		0,		25,		25,		25,     30,		10		},   // breaks 
 { 		50,		40,		0,		30,		30,		0,		0,		35,		35,		25,     30,		10		},   // breaks complex
 { 		35,		25,		20,		15,		30,		30,		30,		25,		25,		25,     30,		10		},   // a bit of all
 { 		70,		50,		30,		5,		5,		5,		5,		40,		40,		0,      0,		0		},   // battery taken off
 { 		35,		25,		20,		20,		70,		40,		40,		25,		25,		0,      40,		10		},   // long notes
 { 		35,		25,		20,		30,		70,		40,		40,		25,		25,		0,      40,		10		},   // long notes, more rest
 { 		35,		0,		0,		20,		0,		0,		0,		0,		0,		0,      50,		50		},   // new "1"  
 { 		10,		0,		0,		0,		0,		0,		0,		0,		0,		0,      50,		50		},   // new "2"  
 { 		50,		0,		0,		40,		0,		0,		0,		0,		0,		0,      10,		50		},   // new "3"  
 { 		0,		0,		0,		80,		0,		0,		0,		0,		0,		0,      20,		60		}    // new "4"  
}; 



/* static const uint8_t __flash randSetups[1][12] = {
	 //		scaled 	upDown	down	rest		multi		3step	4step	acc		glide,	repeat,	halfScale,	"waup"
	 { 		20,		20,		20,		0,		0,		0,		0,		25,		0,      35,     40,		10		}   // all on, no glides
};*/

// sometimes use these to get more structured sequences
static const uint16_t structuredRests[5] = {
0xDDDD, //bit wise encoded version 
0x7777,
0x5555,
0x6DB6,
0xCCCC
};

uint16_t scaledNotes[4] = { //bit wise encoded version 
  /*111111111111*/    0x0fff, 
  /*010010101001*/    0x04a9,
  /*010110101101*/    0x05ad,
  /*101010110101*/    0x0ab5
  /*010110101011*/    // 0x05ab  phrygian
  /*100110101101*/    // 0x09ad  harmonic minor
  /*100110101011*/    // 0x09ab  both of the above (no real use case)
  /*101010110011*/    // 0x0ab3  "arabic major"
  // harm min setting not to affect major scales
}; 

uint8_t getScaledNote(uint8_t scale, uint8_t halfScale);


void randomize(uint8_t scale, uint8_t p_len)
{
	uint8_t setup = random_x(13); 
	
	if(scale == 4)
	{
		setup = settings[FAV_RAND_SETUP]; 
		scale = settings[FAV_RAND_SCALE]; 
		internal_eeprom_write8(FAV_RAND_SETUP + USERSETTINGS_EEADDR, setup);
		internal_eeprom_write8(FAV_RAND_SCALE + USERSETTINGS_EEADDR, scale);
	}
	if(setup>=13)
		setup=0; 
	if(scale>=4)
		scale=0; 

	settings[FAV_RAND_SCALE] = scale;
	settings[FAV_RAND_SETUP] = setup;

	uint8_t scaledProp = randSetups[setup][0];		// probability of step not root key
	uint8_t upDownProp = randSetups[setup][1];		// probability of step up or down..
	uint8_t downProp = randSetups[setup][2];		// ..in that case, probability of step is down
	uint8_t restProp = randSetups[setup][3];		// probability of step is a rest
	uint8_t multiProp = randSetups[setup][4];		// probability of step is joined with next(s) to build a longer note/rest..
	uint8_t _3StepProp = randSetups[setup][5];		// ..in that case, prob. of whether thats 3 steps
	uint8_t _4StepProp = randSetups[setup][6];		// ..or 4 steps
	uint8_t accProp = randSetups[setup][7];			// probability of step is accented
	uint8_t glideProp = randSetups[setup][8];		// probability of step is glide
	uint8_t repeatProp = randSetups[setup][9];		// probability pattern gets looped
	uint8_t halfScaleProp = randSetups[setup][10];	// probability of pattern uses only note 0..6
	uint8_t waupProp = randSetups[setup][11];	    // probability step is start of a "waup-sequence"
    
	uint8_t lastKey = 0;

	uint8_t useStructuredRests = randomIf(restProp / 3) ? 1 : 0;
	uint8_t structRestType = random_x(5);

	uint8_t scaleEnd = randomIf(halfScaleProp) ? 6 : 100;

    uint8_t waupCount = 0; // inactive
    uint8_t waupNote;
	for(uint8_t i = 0; i < p_len; /*noop*/ )
	{
        if (waupCount==2)
        {
            pattern_buff[i] = waupNote-OCTAVE;
            waupCount = 0;
            i++;
            continue;
        }
        
		uint8_t len = 1;
        
		if(!waupCount && randomIf(waupProp) && i<p_len-1)
        {
          waupCount = 1; // first step of waup
        }
		else if(randomIf(multiProp))
		{
			len = 2;
			if(randomIf(_3StepProp))
				len = 3;
			else if(randomIf(_4StepProp))
				len = 4;
		}

		uint16_t	structRestBit = structuredRests[structRestType] & (0x8000 >> i);
		uint8_t		isRest = useStructuredRests ? structRestBit : (randomIf(restProp) ? 1 : 0);
		if(useStructuredRests && ((randomIf(15)) || (i > 11 && randomIf(30))))	//sometimes break the rules
			isRest = randomIf(restProp) ? 1 : 0;

        if (waupCount)
            isRest = 0;
        
		if(isRest)
		{
			for(uint8_t s = 0; s < len && i+s < p_len; s++)
				pattern_buff[i + s] = REST_NOTE;
			lastKey = 0;
		}
		else
		{
			uint8_t key = C2;
			key += randomIf(scaledProp) ? getScaledNote(scale, scaleEnd) : 0;
			if(randomIf(upDownProp))
				key += randomIf(downProp) ? -OCTAVE : OCTAVE;
    
            if ( key>=C2 && key%12>6 && randomIf(50))
				key -= OCTAVE; // shift upper notes down in 1/2 the cases

            for(uint8_t s = 0; s < len  && i+s < p_len; s++)
			{
				pattern_buff[i + s] = key;
				if(s > 0)
					pattern_buff[i + s] |= SLIDE;	//glide to bind notes
			}

			if(randomIf(accProp) || waupCount==1)
				pattern_buff[i] |= ACCENT;

			if(!waupCount && key != lastKey && lastKey > 0)	// only for changed pitch, bindings are handled elsewhere
			{
				if(randomIf(glideProp))
					pattern_buff[i] |= SLIDE;
			}

			lastKey = key;
		}

       
        if (waupCount==1)
        {
            waupNote = lastKey;
            if (waupNote <= C5-OCTAVE)
            {
                pattern_buff[i] += OCTAVE;
                waupNote += OCTAVE;
            }
            pattern_buff[i] |= SLIDE; // glide!
            waupCount = 2;
        }
		i += len;
 	}

	if(randomIf(repeatProp))
	{
		simplifyByLoop( p_len);
	}
};

// note from 0...11, picked from "scale" 
uint8_t getScaledNote(uint8_t scale, uint8_t halfScale)
{
	uint8_t res = 0;
	while(res < 1 || res > halfScale)
	{
		res = random_x(12);
		if( ! ( (scaledNotes[scale]>>res) & 1) )
			res = 0;	//again!
	}
	return res;
};

void randomizeNotes(uint8_t p_len)
{
	uint8_t scale = random_x(4);
	uint8_t setup = random_x(9);

	uint8_t scaledProp = randSetups[setup][0];	// probability of step not root key
	uint8_t halfScaleProp = randSetups[setup][10];	// probability of step is glide
	uint8_t scaleEnd = randomIf(halfScaleProp) ? 6 : 100;

	for(uint8_t i = 0; i <  p_len; i++)
	{
		uint8_t is = pattern_buff[i];
		if( (is & NOTE_MASK)!=REST_NOTE)	
		{
			is &=(ACCENT|SLIDE); 
			is += C2;
			is +=  randomIf(scaledProp) ? getScaledNote(scale, scaleEnd) : 0;
			pattern_buff[i] = is;
		}
	}
};

void simplifyByLoop( uint8_t p_len)
{
	uint8_t type = random_x(3);
	if(type < 2)
	{
		for(uint8_t i = 1; i < 3 + type  && (4+4*i) < p_len ; i++)	// saving bytes:)
			for(uint8_t k = 0; k < 4; k++)
				pattern_buff[4 * i + k] = pattern_buff[k];
	}
	else
	{
		for(uint8_t i = 0; i < 8 && i+8 < p_len; i++)
			pattern_buff[i + 8] = pattern_buff[i];
	}
};
