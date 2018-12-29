#define F_CPU 8000000ul
#include <avr/io.h>
#include <stdlib.h>
#include <math.h>
#include <util/delay.h>
#include <avr/interrupt.h>

#include "waveform.h"
#include "uart.h"

#define _CMD_START_CNT 1
#define _CMD_END_CNT   1
#define _CMD_WAVE_CNT  1
#define _CMD_AMP_CNT   3
#define _CMD_FRQ_CNT   3

#define FULL_CMD_CNT (_CMD_START_CNT +  _CMD_WAVE_CNT + _CMD_AMP_CNT + _CMD_FRQ_CNT + _CMD_END_CNT)
#define WAVE_OFFSET  (_CMD_START_CNT)
#define AMP_OFFSET   (_CMD_START_CNT +  _CMD_WAVE_CNT)
#define FREQ_OFFSET  (_CMD_START_CNT +  _CMD_WAVE_CNT + _CMD_AMP_CNT)
#define MARKER_END   (_CMD_START_CNT +  _CMD_WAVE_CNT + _CMD_AMP_CNT + _CMD_FRQ_CNT)
#define MARKER_START (0)

#define WAVEFORM_NUM 4

#define DAC_DDR  DDRB
#define DAC_PORT PORTB

#define ARR_SIZE(a) (sizeof(a) / sizeof(*a))

#define PRESCALAR_BITS 0b010
#define PRESCALAR 8 

#define DC_OFF 127 
#define AMP_FACTOR 5 

typedef enum {GENERATE_WAVE, UPDATE_WAVE} states_t;

static uint8_t cmd_buffer[FULL_CMD_CNT];
static void (*waveform[WAVEFORM_NUM])(uint8_t amp, uint8_t freq);
static states_t currentState = GENERATE_WAVE;
static uint8_t amp_value = 0;
static uint8_t freq_value = 0;
static uint8_t waveform_index = WAVEFORM_NUM;
int sine[] = {0, 3, 6, 8, 9, 10, 9, 8, 6, 3, 0, -3, -6, -8, -9, -10, -9, -8, -6, -3};

void set_amplitude_centred(uint8_t a)
{
	OCR2 = (DC_OFF + a); // DC shift by peak to get it centred around DC_OFF
}

ISR(TIMER0_COMP_vect)
{
	static int i = 0;
	set_amplitude_centred(AMP_FACTOR * sine[i]);
	if (++i == ARR_SIZE(sine))
		i = 0;
}

void init_pwm(void)
{
	DDRD |= 0b10000000;

	OCR2 = 0; 


	TCCR2 = (1 << WGM21) | (1 << WGM20); 
	TCCR2 |= (2 << COM20); 
	TCCR2 |= (1 << CS20); 
}
void init_timer(void) 
{
	sei(); 
	TIMSK |= (1 << OCIE0); 
	OCR0 = (F_CPU / PRESCALAR) / 20000; 
	TCCR0 = (1 << WGM01) | (0 << WGM00) | PRESCALAR_BITS;
}




void sineWave(uint8_t amp, uint8_t freq)
{
	init_pwm();
	init_timer();
	_delay_ms(500);
	
}

void squareWave(uint8_t amp, uint8_t freq)
{
	// TODO: Place ur code here
	DAC_DDR = 255;
	_delay_us(500);
	DAC_PORT = 0x00;
	_delay_us(8000);
	DAC_PORT = 0xFF;
	_delay_us(8000);
}

void staircaseWave(uint8_t amp, uint8_t freq)
{
	// Refresh DAC DDR to be output.
	DAC_DDR = 255;
	_delay_us(500);
	// Generate waveform.
	DAC_PORT = 0x00;
	_delay_us(1500);
	DAC_PORT = 0x33;
	_delay_us(1500);
	DAC_PORT = 0x66;
	_delay_us(1500);
	DAC_PORT = 0x99;
	_delay_us(1500);
	DAC_PORT = 0xCC;
	_delay_us(1500);
	DAC_PORT = 0xFF;
	_delay_us(1500);
}

void triangleWave(uint8_t amp, uint8_t freq)
{
	// TODO: Place ur code here
	for(int i = 0; i < 255; i++){
		DAC_PORT = i;
		_delay_us(800);
	}
	_delay_ms(1);
	for(int i = 255; i > 0; i--){
		DAC_PORT = i;
		_delay_us(800);
	}
	/*for(int i = 255; i > 0; i--){
		DAC_PORT -= 1;
		_delay_us(40);
	}*/
	// Generate waveform.
	/*
	DAC_PORT = 0x00;
	_delay_us(8000);
	DAC_PORT = 0xFF;
	_delay_us(8000);
*/

	
}


void WAVE_Init(void)
{
	uint8_t i;
	
	/* Init UART driver. */
	UART_cfg my_uart_cfg;
	
	/* Set USART mode. */
	my_uart_cfg.UBRRL_cfg = (BAUD_RATE_VALUE)&0x00FF;
	my_uart_cfg.UBRRH_cfg = (((BAUD_RATE_VALUE)&0xFF00)>>8);
	
	my_uart_cfg.UCSRA_cfg = 0;
	my_uart_cfg.UCSRB_cfg = (1<<RXEN)  | (1<<TXEN) | (1<<TXCIE) | (1<<RXCIE);
	my_uart_cfg.UCSRC_cfg = (1<<URSEL) | (3<<UCSZ0);
	
	UART_Init(&my_uart_cfg);
	
	
	/* Clear cmd_buffer. */
	for(i = 0; i < FULL_CMD_CNT; i += 1)
	{
		cmd_buffer[i] = 0;
	}
	
	/* Initialize waveform array. */
	waveform[0] = squareWave;
	waveform[1] = staircaseWave;
	waveform[2] = triangleWave;
	waveform[3] = sineWave;

	/* Start with getting which wave to generate. */
	currentState = UPDATE_WAVE;
}

void WAVE_MainFunction(void)
{
	// Main function must have two states,
	// First state is command parsing and waveform selection.
	// second state is waveform executing.
	switch(currentState)
	{
		case UPDATE_WAVE:
		{
			UART_SendPayload((uint8_t *)">", 1);
			while (0 == UART_IsTxComplete());

			/* Receive the full buffer command. */
			UART_ReceivePayload(cmd_buffer, FULL_CMD_CNT);
			
			/* Pull unitl reception is complete. */
			while(0 == UART_IsRxComplete());

			/* Check if the cmd is valid. */
			if((cmd_buffer[MARKER_START] == '@') && (cmd_buffer[MARKER_END] == ';'))
			{
				// Extract amplitude and freq values before sending them to the waveform generator.
				/* Compute amplitude. */
				{
					char _buffer[_CMD_AMP_CNT];
					for(uint8_t i = 0; i < _CMD_AMP_CNT; ++i) { _buffer[i] = cmd_buffer[AMP_OFFSET+i]; }
					amp_value = atoi(_buffer);
				}

				/* Compute frequency. */
				{
					char _buffer[_CMD_FRQ_CNT];
					for(uint8_t i = 0; i < _CMD_FRQ_CNT; ++i) { _buffer[i] = cmd_buffer[FREQ_OFFSET+i]; }
					freq_value = atoi(_buffer);
				}

				/* Compute waveform. */
				{
					waveform_index = cmd_buffer[WAVE_OFFSET] - '0';
				}
			}
			else
			{
				/* Clear cmd_buffer. */
				for(uint8_t i = 0; i < FULL_CMD_CNT; i += 1)
				{
					cmd_buffer[i] = 0;
				}
			}

			// Trigger a new reception.
			UART_ReceivePayload(cmd_buffer, FULL_CMD_CNT);

			UART_SendPayload((uint8_t *)"\r>", 2);
			while (0 == UART_IsTxComplete());

			DDRA = 255; PORTA = 4;
		}
		case GENERATE_WAVE:
		{
			DDRA = 255; PORTA = 8;
			// Execute waveform..
			if(waveform_index < WAVEFORM_NUM)
			{
				waveform[waveform_index](amp_value, freq_value);
				
				DDRA = 255;
				PORTA = 1;
			}
			else
			{
				DDRA = 255;
				PORTA = 2;
			}
			// Keep in generate wave if no command it received.
			currentState = (1 == UART_IsRxComplete()) ? UPDATE_WAVE : GENERATE_WAVE;
			break;
		}
		default: {/* Do nothing.*/}
	}
}

