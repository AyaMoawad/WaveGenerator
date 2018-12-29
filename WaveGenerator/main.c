#define F_CPU 8000000ul

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include "waveform.h"


int main(void)
{
	DDRB = 255;
	DDRD = 255;
	WAVE_Init();
	sei();
    while (1) 
    {
		WAVE_MainFunction();
    }
	
	
}




