#include <stdio.h>
#include <system.h>
#include <math.h>
#include <sys/alt_irq.h>
#include <altera_avalon_pio_regs.h>
#include <altera_avalon_timer_regs.h>
#include "altera_avalon_timer.h"
#include <altera_avalon_lcd_16207.h>
#include <altera_avalon_lcd_16207_regs.h>
#include "altera_up_avalon_video_pixel_buffer_dma.h"
#include <altera_up_sd_card_avalon_interface.h>

// Interrupt Pointers
volatile int edge_capture;
volatile const *rxd = (void*) 0x2440;
volatile const *txd = (void*) 0x2450;

// LUTs
static int SevenSegment_values[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
static int dispnums[10] =  {0b01000000, 0b01111001, 0b00100100, 0b00110000, 0b00011001, 0b00010010, 0b00000010, 0b01111000, 0b10000000, 0b00010000};
static int pong[5] = {0b00001100, 0b01000000, 0b01001000, 0b00010000, 0b01001111};// p o n g i

static int LUT[10] = {0b01000000, 0b01111001, 0b00100100, 0b00110000, 0b00011001, 0b00010010,0b00000010, 0b01111000, 0b00000000, 0b00011000};

// Prototype Functions
static void keys_isr(void* context);
static void timer_isr(void* context);
static void timer2_isr(void* conteExt);
static void uart_isr(void* context);
static void uart_isr1(void* context);
static void uart_isr2(void* context);
static void uart_isr3(void* context);
void VGA_box(int, int, int, int, int);

// ------------------------------------GLOBAL VARIABLES----------------------------------

// Misc. Variables
alt_u16 myside;
alt_u32 ledr;
static alt_u8 increm = 1;
static int clearlcd = 1;
static int afterscoredon = 0;

// PIO Variables
int switchVal;		// Check current switch value
int LEDR_val;		// Check current status of LEDR
int ballrec = 0;	// Reset LEDR based on position (Ball received)
int ballinplay = 0;		// ball in play (back and forth). out of play when score occur
static int hostscored = 0;
static int gamestarted = 0;

// Game Setting Variables
int position; 				// If 0 (left) or 1 (right)
int invalidHG;
int pause = 1;				// Pause if 0, unpause if 1
int hostORguest = 0;		// If 0 (!host) or 1 (host)
static int hostguest = 5;	// status for LCD. 1 means Host - 0 means Guest
static int playerready = 5; // player waiting 0 - ready 1
static int ready = 5;		// lcd - 0 waiting - 1 ready
static alt_8 gameready = 0; // game is not ready 0 - game is ready 1
static int bothready = 0;
int enablerec;
int time = 0;

// Scores
int LeftScore = 0;
int RightScore = 0;
int hits = 0;

alt_u32 displayL = 0;
alt_u32 displayR = 0;

// Win Conditions
int guestwon = 0;
int hostwon = 0;

/////////////////////////////------SETUPS-------///////////////////////////////////////
//--------------------------------KEY SETUP--------------------------------------
static void init_button_pio(){
	void* edge_capture_ptr = (void*)&edge_capture;
	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(KEYS_BASE, 0xF);
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(KEYS_BASE, 0x0);
    alt_irq_register(KEYS_IRQ, edge_capture_ptr, keys_isr);
	printf("Keys Interrupts Registered: \n");
}

//-------------------------------UART INIT--------------------------------------------
static void init_uart(){
		void* edge_capture_ptr = (void*)&edge_capture;
		IOWR_ALTERA_AVALON_PIO_IRQ_MASK(UART_R_BASE, 0xF);	// Might need to be 0b1
	    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(UART_R_BASE, 0x0);
	    alt_irq_register(UART_R_IRQ, edge_capture_ptr, uart_isr);
	    printf("UART Set: \n");
}

//-------------------------------UART1111111 INIT--------------------------------------------
static void init_uart1(){
		void* edge_capture_ptr = (void*)&edge_capture;
		IOWR_ALTERA_AVALON_PIO_IRQ_MASK(UART_R1_BASE, 0xF);	// Might need to be 0b1
	    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(UART_R1_BASE, 0x0);
	    alt_irq_register(UART_R1_IRQ, edge_capture_ptr, uart_isr1);
	    printf("UART Set: \n");
}

//-------------------------------UART22222 INIT--------------------------------------------
static void init_uart2(){
		void* edge_capture_ptr = (void*)&edge_capture;
		IOWR_ALTERA_AVALON_PIO_IRQ_MASK(UART_R2_BASE, 0xF);	// Might need to be 0b1
	    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(UART_R2_BASE, 0x0);
	    alt_irq_register(UART_R2_IRQ, edge_capture_ptr, uart_isr2);
	    printf("UART Set: \n");
}

//-------------------------------UART333333 INIT--------------------------------------------
static void init_uart3(){
		void* edge_capture_ptr = (void*)&edge_capture;
		IOWR_ALTERA_AVALON_PIO_IRQ_MASK(UART_R3_BASE, 0xF);	// Might need to be 0b1
	    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(UART_R3_BASE, 0x0);
	    alt_irq_register(UART_R3_IRQ, edge_capture_ptr, uart_isr3);
	    printf("UART Set: \n");
}

//------------------------------Initialize Timer------------------------------------
static void init_timer(){
	alt_u32 period = (SYSTEM_TIMER_LOAD_VALUE * time);	// 150ms timer
	alt_u16 periodl = period & 0xFFFF;
	alt_u16 periodh = period >> 0x10;

	IOWR_ALTERA_AVALON_TIMER_STATUS(SYSTEM_TIMER_BASE, 0);
	IOWR_ALTERA_AVALON_TIMER_PERIODL(SYSTEM_TIMER_BASE, periodl);
	IOWR_ALTERA_AVALON_TIMER_PERIODH(SYSTEM_TIMER_BASE, periodh);
	IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0x0B);
	alt_irq_register( SYSTEM_TIMER_IRQ, 0, timer_isr);

	printf("System Timer Set: \n");
}

//--------------------------Initialize High Res Timer-------------------------------
static void init_timer2(){
	alt_u32 period = HIGH_RES_TIMER_LOAD_VALUE * 500;	// 1ms timer
	alt_u16 periodl = period & 0xFFFF;
	alt_u16 periodh = period >> 0x10;

	IOWR_ALTERA_AVALON_TIMER_STATUS(HIGH_RES_TIMER_BASE, 0);
	IOWR_ALTERA_AVALON_TIMER_PERIODL(HIGH_RES_TIMER_BASE, periodl);
	IOWR_ALTERA_AVALON_TIMER_PERIODH(HIGH_RES_TIMER_BASE, periodh);
	IOWR_ALTERA_AVALON_TIMER_CONTROL(HIGH_RES_TIMER_BASE, 0x0B);
	alt_irq_register(HIGH_RES_TIMER_IRQ, 0, timer2_isr);

	printf("High Res Set: \n");
}

/////////////////////////////------ISRS-------///////////////////////////////////////
//---------------------- System Timer ISR----------------------------------------
static void timer_isr(void* context){
	printf("System Timer ISR: \n");

	// Board is placed on LEFT position-----------------------------------------------
	if (position == 0){

		printf("LEFT position entered. \n");
		// If RESET is 1 && Position is LEFT, LEDR is set to LEDR0
		if (ballrec == 1){
			ledr = 1;
			IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, ledr);
			ballrec = 0;
		}

		// Check if ball is on my side
		if (myside == 1){
			printf("Myside HIGH\n");
			if (increm == 1){
				ledr = ledr << 1; 	// shift LEDR left by one
				IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, ledr);

				// if LEDR reaches LEDR17 shift right
				if (ledr >= 0b100000000000000000){
					printf("left got scored on\n");
					IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0xB);

					if ((hostguest == 0)){
						IOWR_ALTERA_AVALON_PIO_DATA(UART_T3_BASE, 0b1);
						delaytx();
						IOWR_ALTERA_AVALON_PIO_DATA(UART_T3_BASE, 0b0);
						IOWR_ALTERA_AVALON_TIMER_CONTROL(HIGH_RES_TIMER_BASE, 0x7);
					}
					else if (hostguest == 1){
						RightScore++;

					if(RightScore < 3){
						IOWR_ALTERA_AVALON_TIMER_CONTROL(HIGH_RES_TIMER_BASE, 0x7);
					}
					else if (RightScore == 3){
						printf("Guest Won!\n");
						IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0xB);
						delaytx();
						IOWR_ALTERA_AVALON_PIO_DATA(UART_T1_BASE, 0b1);
						delaytx();
						IOWR_ALTERA_AVALON_PIO_DATA(UART_T1_BASE, 0b0);


						sdc_upload(4); // Host loses
						othercelebrate();
					}
					}
				}

			} else if (increm == 0){
				ledr = ledr >> 1;	// shift LEDR right by one
				IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, ledr);

				printf("Shifting right out\n");
				if (ledr < 1){
					increm = 1;
					myside = 0;

					IOWR_ALTERA_AVALON_PIO_DATA(UART_T2_BASE, 0b1);
					delaytx();
					IOWR_ALTERA_AVALON_PIO_DATA(UART_T2_BASE, 0b0);
					IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0xB);
				}
			}

		} else if (myside == 0){
			printf("Not on my side. \n");
		}

/////////////////////////////////////////////////////////////////////////////////////////
	// Board is placed on RIGHT position
	} else if (position == 1){

		// If RESET is 1 && Position is RIGHT, LEDR is set to LEDR17
		if (ballrec == 1){
			ledr = 131072;
			IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, ledr);
			ballrec = 0;
		}

		//IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, ledr);
		if (myside == 1){

			if (increm == 1){
				ledr = ledr >> 1; 	// shift LEDR right by one
				IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, ledr);

				// If LEDR reaches LEDR0 shift left
				if (ledr <= 1){
					printf("right got scored on\n");
					IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0xB);

					if ((hostguest == 0)){
						IOWR_ALTERA_AVALON_PIO_DATA(UART_T3_BASE, 0b1);
						delaytx();
						IOWR_ALTERA_AVALON_PIO_DATA(UART_T3_BASE, 0b0);
						IOWR_ALTERA_AVALON_TIMER_CONTROL(HIGH_RES_TIMER_BASE, 0x7);
					}
					else if (hostguest == 1){
					LeftScore++;

					if(LeftScore < 3){
						IOWR_ALTERA_AVALON_TIMER_CONTROL(HIGH_RES_TIMER_BASE, 0x7);
					}
					else if (LeftScore == 3){
						printf("Guest Won!\n");
						IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0xB);
						delaytx();
						IOWR_ALTERA_AVALON_PIO_DATA(UART_T1_BASE, 0b1);
						delaytx();
						IOWR_ALTERA_AVALON_PIO_DATA(UART_T1_BASE, 0b0);

						// Host loses
						sdc_upload(4);
						othercelebrate();
					}
					}
				}

			} else if (increm == 0){
				ledr = ledr << 1;	// shift LEDR left by one
				IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, ledr);

				printf("Shifting Left out\n");
				if (ledr > 0b100000000000000000){
					increm = 1;
					myside = 0;

					IOWR_ALTERA_AVALON_PIO_DATA(UART_T2_BASE, 0b1);
					delaytx();
					IOWR_ALTERA_AVALON_PIO_DATA(UART_T2_BASE, 0b0);
					IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0xB);
				}
			}

		} else if (myside == 0){
			printf("Not on my side. \n");
		}

	} // end of position check
	IOWR_ALTERA_AVALON_TIMER_STATUS(SYSTEM_TIMER_BASE, 0);
	IORD_ALTERA_AVALON_TIMER_STATUS(SYSTEM_TIMER_BASE);
}

 //-----------------------------High Res Timer ISR------------------------------------
static void timer2_isr(void* context){
	printf("High Res Timer ISR:  \n");
	afterscoredon = 1;
	if (afterscoredon == 1){
		if (position == 0) {
			ledr = 32768;
			if (hostguest == 1){
				alt_u32 mask = 0xFFFFFF00;
				displayR = LUT[RightScore];
				displayR = mask + displayR;
				//IOWR_ALTERA_AVALON_PIO_DATA(SEVEN_SEGMENT1_BASE, displayR);
			}

		}
		else if (position == 1) {
			ledr = 4;
			if (hostguest == 1){
				alt_u32 mask = 0xFFFFFF00;
				displayL = LUT[LeftScore];
				displayL = mask + displayL;
				//IOWR_ALTERA_AVALON_PIO_DATA(SEVEN_SEGMENT2_BASE, displayL);
			}

		}
		printf("Score Graphic!\n");
		sdc_upload(2);

		IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, ledr);
		increm = 0;
		IOWR_ALTERA_AVALON_TIMER_CONTROL(HIGH_RES_TIMER_BASE, 0xB);
		afterscoredon = 1;
	}
	IOWR_ALTERA_AVALON_TIMER_STATUS(HIGH_RES_TIMER_BASE, 0);
	IORD_ALTERA_AVALON_TIMER_STATUS(HIGH_RES_TIMER_BASE);
}

//--------------------------------UART ISR --------------------------------------
static void uart_isr(void* context){
	volatile int* edge_capture_ptr = (volatile int*) context;
	*edge_capture_ptr = IORD_ALTERA_AVALON_PIO_EDGE_CAP(UART_R_BASE);
	printf("Setting Host/Guest\n");

	if(gamestarted == 0){
// DETERMINING WHO IS HOST AND GUEST
	if ( hostORguest == 0){
		printf("Other is Host\n");	// other board is Guest
		hostORguest = 1;
	}
	else if (hostORguest == 1){
		printf("Other is Guest\n");	// other board is Host
		hostORguest = 0;
	}

	uart_isr_self();
	}
	else if ((gamestarted == 1) && (hostguest == 0)){// host winner
		printf("Host Won!\n");
		IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0xB);
		IOWR_ALTERA_AVALON_TIMER_CONTROL(HIGH_RES_TIMER_BASE, 0xB);
		hostcelebrate();
	}

	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(UART_R_BASE, 0);
	IORD_ALTERA_AVALON_PIO_EDGE_CAP(UART_R_BASE);
}

void uart_isr_self(){
		if ((hostguest == 0) && (hostORguest == 0)){	//cant start. both guest
			printf("Both are guest\n");
			invalidHG = 1;
		}
		else if ( ((hostguest == 5)||(hostguest == 1)) && (hostORguest == 1)){	// cant start. both host
			printf("Both are Host\n");
			invalidHG = 1;
		}
		else if ((hostguest == 1) && (hostORguest == 0)){	// im guest, theyre host
			printf("I am Host. They are Guest\n");
			invalidHG = 0;
		}
		else if ( (hostguest == 0) && (hostORguest == 1)){	// im host, theyre guest
			printf("I am Guest. They are Host\n");
			invalidHG = 0;
		}
}

//--------------------------------UART1111 ISR --------------------------------------
static void uart_isr1(void* context){
	volatile int* edge_capture_ptr = (volatile int*) context;
	*edge_capture_ptr = IORD_ALTERA_AVALON_PIO_EDGE_CAP(UART_R_BASE);

	if(gamestarted == 0){
	//	ENSURING IF BOTH PLAYERS ARE READY
	printf("Checking Ready State\n");
	if (( playerready == 1) || (playerready == 5)){
		printf("Waiting for Other\n");	// other board is waiting to ready up
		playerready = 0;
	}
	else if (playerready == 0){
		printf("Other is now Ready\n");	// other board is ready
		playerready = 1;
	}

	uart_isr1_selfready();
	}

	else if ((gamestarted == 1) && (hostguest == 0)){//GUEST WON
		printf("You Won!\n");
		IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0xB);
		IOWR_ALTERA_AVALON_TIMER_CONTROL(HIGH_RES_TIMER_BASE, 0xB);

		// Host wins
		sdc_upload(1);
		delayp();
		othercelebrate();
	}

	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(UART_R1_BASE, 0);
	IORD_ALTERA_AVALON_PIO_EDGE_CAP(UART_R1_BASE);
}

void uart_isr1_selfready(){
	if ((playerready == 1) && (ready == 1)){	// both boards ready. game is ready
		printf("Both are ready\n");
		bothready = 1;
	}
	else if ((playerready == 0) && (ready == 1)){
		printf("I am ready - Waiting Other \n");
		bothready = 0;
	}
	else if ((playerready == 1) && (ready == 0)) {
		printf("Waiting for me - Other is Ready\n");
		bothready = 0;
	}
}

//--------------------------------UART22222 ISR --------------------------------------
static void uart_isr2(void* context){
	volatile int* edge_capture_ptr = (volatile int*) context;
	*edge_capture_ptr = IORD_ALTERA_AVALON_PIO_EDGE_CAP(UART_R_BASE);

	//RECEIVING THE BALL FROM OTHER PLAYER
	if (enablerec == 1){
		printf("RECEIVED\n");
		myside = 1;
		ballrec = 1;
		IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0x07);
	}
	else if ((enablerec == 0) && (hostguest == 0) && (invalidHG == 0) && (bothready == 1)){
		printf("Guest is ready to receive!\n");
		enablerec = 1;
		gamestarted = 1;
		IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, 0);
		IOWR_ALTERA_AVALON_PIO_DATA(LEDG_BASE, 0);
	}

	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(UART_R2_BASE, 0);
	IORD_ALTERA_AVALON_PIO_EDGE_CAP(UART_R2_BASE);
}

//--------------------------------UART3333 ISR --------------------------------------
static void uart_isr3(void* context){
	volatile int* edge_capture_ptr = (volatile int*) context;
	*edge_capture_ptr = IORD_ALTERA_AVALON_PIO_EDGE_CAP(UART_R_BASE);

	printf("Host Scored on Guest\n");
	alt_u32 mask = 0xFFFFFF00;
	if(hostguest == 1){
		hostscored = hostscored + 1;
		if (position == 0){
			LeftScore = hostscored;
			displayL = LUT[LeftScore];
			displayL = mask + displayL;
			//IOWR_ALTERA_AVALON_PIO_DATA(SEVEN_SEGMENT2_BASE, displayL);
		}
		else if (position == 1){
			RightScore = hostscored;
			displayR = LUT[RightScore];
			displayR = mask + displayR;
			//IOWR_ALTERA_AVALON_PIO_DATA(SEVEN_SEGMENT1_BASE, displayR);
		}

		if (hostscored < 3){
			sdc_upload(2);
		}else if(hostscored == 3){
			printf("You Won!\n");
			IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0xB);
			IOWR_ALTERA_AVALON_TIMER_CONTROL(HIGH_RES_TIMER_BASE, 0xB);

			// Host wins
			sdc_upload(1);

			delaytx();
			IOWR_ALTERA_AVALON_PIO_DATA(UART_T_BASE, 0b1);
			delaytx();
			IOWR_ALTERA_AVALON_PIO_DATA(UART_T_BASE, 0b0);
			delayshort();
			hostcelebrate();
		}
	}

	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(UART_R3_BASE, 0);
	IORD_ALTERA_AVALON_PIO_EDGE_CAP(UART_R3_BASE);
}

//-------------------------------------- KEY ISR-------------------------------------
static void keys_isr(void* context){
	volatile int* edge_capture_ptr = (volatile int*) context;
	*edge_capture_ptr = IORD_ALTERA_AVALON_PIO_EDGE_CAP(KEYS_BASE);

	switchVal = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCHES_BASE);
	if (switchVal == 0b1){
		IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0xB);
		IOWR_ALTERA_AVALON_TIMER_CONTROL(HIGH_RES_TIMER_BASE, 0xB);
		// Game settings

		// KEY3
		if (*edge_capture_ptr == 8){
			key3_isr();
		}

		// KEY2
		if (*edge_capture_ptr == 4){
			key2_isr();
		}

		// KEY1
		if (*edge_capture_ptr == 2){
			key1_isr();
		}

		// KEY0
		if (*edge_capture_ptr == 1){
			key0_isr();
		}

	} else if (switchVal == 0b0){

		// Game Controls

		if (*edge_capture_ptr == 8){
			CHECK_ISR();
		}

		if (*edge_capture_ptr == 4){
			START_ISR();
		}

		if (*edge_capture_ptr == 2){
			PAUSE_ISR();
		}

		if (*edge_capture_ptr == 1){
			HIT_ISR();
		}
	} else if (switchVal == 0b110){
		if (*edge_capture_ptr == 8){
			RESET_ISR();
		}

		if (*edge_capture_ptr == 4){
			time = 150; // easy time;
			init_timer();
		}

		if (*edge_capture_ptr == 2){
			time = 125; // med time
			init_timer();
		}

		if (*edge_capture_ptr == 1){
			time = 100; // hard time
			init_timer();
		}
	}

	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(KEYS_BASE, 0);
	IORD_ALTERA_AVALON_PIO_EDGE_CAP(KEYS_BASE);
}

//--------------------------------CHECK ISR -------------------------------------
void CHECK_ISR(){
	printf("CHECK ISR entered. \n");
	if (invalidHG == 1) printf("HostGuest Not Valid\n");
	if (hostguest == 1){
		if ((invalidHG == 0)&&(bothready == 1)){
			gameready = 1;
			printf("Game is ready\n");
			delaytx();
			IOWR_ALTERA_AVALON_PIO_DATA(UART_T2_BASE, 0b1);
			delaytx();
			IOWR_ALTERA_AVALON_PIO_DATA(UART_T2_BASE, 0b0);

			IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, 0);
			IOWR_ALTERA_AVALON_PIO_DATA(LEDG_BASE, 0);
		}
		else {
			printf("Game not ready\n");
			gameready = 0;
		}
	}
	if(hostguest == 0){
		if((invalidHG == 0) && (bothready == 1)){
			printf("Guest - Game is Ready\n");
			gameready = 1;
		}
	}

}

//--------------------------------RESET ISR -------------------------------------
void RESET_ISR(){
		IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b000001);
				char lcd[] = "LOADING...";
				char lcd2[] = "LOADING...";
				IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b10000000);
				for (int i = 0; i<sizeof(lcd)-1; i++){
					delayshort();
					IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, lcd[i]);
				}
				delayshort();
				IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b11000000);
				for (int i = 0; i<sizeof(lcd2)-1; i++){
					delayshort();
					IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, lcd2[i]);
				}
				IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, 0x2aaaa);
				IOWR_ALTERA_AVALON_PIO_DATA(LEDG_BASE, 0b10101010);
				IOWR_ALTERA_AVALON_PIO_DATA(SEVEN_SEGMENT1_BASE, 0xFFFFFFFF);
				IOWR_ALTERA_AVALON_PIO_DATA(SEVEN_SEGMENT2_BASE, 0xFFFFFFFF);

				increm = 1;
				clearlcd = 1;
				afterscoredon = 0;

				ballrec = 0;	// Reset LEDR based on position (Ball received)
				ballinplay = 0;		// ball in play (back and forth). out of play when score occur
				hostscored = 0;
				gamestarted = 0;

				pause = 1;				// Pause if 0, unpause if 1
				hostORguest = 0;		// If 0 (!host) or 1 (host)
				hostguest = 5;	// status for LCD. 1 means Host - 0 means Guest
				playerready = 5; // player waiting 0 - ready 1
				ready = 5;		// lcd - 0 waiting - 1 ready
				gameready = 0; // game is not ready 0 - game is ready 1
				bothready = 0;
				enablerec = 0;

				// Scores
				LeftScore = 0;
				RightScore = 0;
				hits = 0;

				displayL = 0;
				displayR = 0;

				// Win Conditions
				guestwon = 0;
				hostwon = 0;
				sdc_upload(3);
}
//--------------------------------START ISR -------------------------------------
void START_ISR(){
	printf("Start ISR entered. \n");

	alt_u32 period = SYSTEM_TIMER_LOAD_VALUE * time;
	alt_u16 periodl = period & 0xFFFF;
	alt_u16 periodh = period >> 0x10;

	IOWR_ALTERA_AVALON_TIMER_STATUS(SYSTEM_TIMER_BASE, 0);
	IOWR_ALTERA_AVALON_TIMER_PERIODL(SYSTEM_TIMER_BASE, periodl);
	IOWR_ALTERA_AVALON_TIMER_PERIODH(SYSTEM_TIMER_BASE, periodh);
	IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0xB);

	if (gameready == 0) {
		printf("Game is not ready to start\n");
	}
	else if ((invalidHG == 0) && (hostguest == 1)){
		if (position == 0)ledr = 32768; 	// host side left
		else if (position == 1) ledr = 4; 	//host side right
		ballrec = 0; 	// so ball starts with host
		increm = 0;
		gamestarted = 1;
		myside = 1;
		afterscoredon = 0;
		enablerec = 1;
		IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0x7);
	}
	else if ((invalidHG == 0) && (hostguest == 0)){
		printf("Guest cannot Start Game\n");
		IOWR_ALTERA_AVALON_PIO_DATA(LEDG_BASE, 0b11111111);
		delayshort();
		IOWR_ALTERA_AVALON_PIO_DATA(LEDG_BASE, 0);
		delayshort();
		IOWR_ALTERA_AVALON_PIO_DATA(LEDG_BASE, 0b11111111);
		delayshort();
		IOWR_ALTERA_AVALON_PIO_DATA(LEDG_BASE, 0);
	}
}

//--------------------------------PAUSE ISR -------------------------------------
void PAUSE_ISR(){
	printf("Pause ISR entered. \n");

	if (pause == 1){
		IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0x7); // Starts Timer
		pause = 0;

	} else if (pause == 0){
		IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0xB); // Stops Timer
		pause = 1;
	}
}

//--------------------------------HIT ISR ---------------------------------------
void HIT_ISR(){
	printf("Hit ISR entered. \n");
	LEDR_val = IORD_ALTERA_AVALON_PIO_DATA(LEDS_BASE);

if (afterscoredon == 0){
	printf("Ball in play\n");
	//-----------------------LEFT SIDE ------
	if (position == 0){
		// This board is place on LEFT position
		if (LEDR_val == 65536){
			increm = 0;
			//ledr = 32768;
			ledr = 65536;
			hits++;
			printf("Hit! \n");

		} else {
			int ledplace = IORD_ALTERA_AVALON_PIO_DATA(LEDS_BASE);
			if (  ((ledplace == 65536)||(ledplace < 1)) &&  (myside == 1) ){
				printf("Miss! \n");
			}
		}

	// RIGHT SIDE
	} else if (position == 1){
		// This board is placed on RIGHT position
		if (LEDR_val == 2){
			increm = 0;
			ledr = 2;
			hits++;
			printf("Hit! \n");

		} else {
			int ledplace = IORD_ALTERA_AVALON_PIO_DATA(LEDS_BASE);
			if ( (ledplace <= 1) &&  (myside == 1) ){
				printf("Miss! \n");
			}
		}
	}

}else if(afterscoredon == 1){
	printf("Ball Returned Next Set\n");
	IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0x7);
	afterscoredon = 0;
}

}

//--------------------------------KEY3 ISR --------------------------------------
void key3_isr(){
	if (clearlcd == 1){
		IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b000001);
		clearlcd = 0;
	}

	static int player =0;
		if (player == 0){
			 char lcd[8] = "Player 1";
			 delayp();
			 IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b10000000);
			 for (int i = 0; i<sizeof(lcd); i++){
			 		delayp();
			 		IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, 0);
			 }
			 delayp();
			 IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b10000000);
			 for (int i = 0; i<sizeof(lcd); i++){
			 	delayp();
			 	IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, lcd[i]);
			 }

			player = 1;
		}
		else if (player == 1) {
			 char lcd2[8] = "Player 2";
			 delayp();
			 IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b10000000);
			 for (int i = 0; i<sizeof(lcd2); i++){
			 	delayp();
			 	IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, 0);
			 }

			 delayp();
			 IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b10000000);
			 for (int i = 0; i<sizeof(lcd2); i++){
			 		delayp();
			 		IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, lcd2[i]);
			 }

			player = 0;
		}
}

//--------------------------------KEY2 ISR --------------------------------------
void key2_isr(){
	IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b10001001);
	delayp();
	IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, 0x2d);

	delayp();
	static int side =0;
	if (side == 0){
		 char lcd[5] = "Left ";
		 delayp();
		 IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b10001011);
		 for (int i = 0; i<sizeof(lcd); i++){
		 	delayp();
		 	IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, 0);
		 }

		 delayp();
		 IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b10001011);
		 for (int i = 0; i<sizeof(lcd); i++){
		 		delayp();
		 		IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, lcd[i]);
		 	}
		side = 1;
		position = 0;	// GLOBAL POSITION VARIABLE (LEFT)
		printf("Position set to LEFT \n");
	}
	else if (side == 1) {
		 char lcd2[5] = "Right";
		 delayp();
		 IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b10001011);
		 for (int i = 0; i<sizeof(lcd2); i++){
		 	delayp();
		 	IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, 0);
		 }

		 delayp();
		 IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b10001011);
		 for (int i = 0; i<sizeof(lcd2); i++){
		 		delayp();
		 		IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, lcd2[i]);
		 	}
		side = 0;
		position = 1;	// GLOBAL POSITION VARIABLE (RIGHT)
		printf("Position set to RIGHT \n");
	}
}

//--------------------------------KEY1 ISR --------------------------------------
void key1_isr(){
		if ((hostguest == 0) || (hostguest == 5)){
			 char lcd[5] = "Host ";
			 delayp();
			 IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b11000000);
			 for (int i = 0; i<sizeof(lcd); i++){
			 	delayp();
			 	IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, 0);
			 }

			 delayp();
			 IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b11000000);
			 for (int i = 0; i<sizeof(lcd); i++){
			 		delayp();
			 		IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, lcd[i]);
			 	}
			hostguest = 1;
			uart_isr_self();
			printf("I am Host\n");

			delaytx();
			IOWR_ALTERA_AVALON_PIO_DATA(UART_T_BASE, 0b1);
			delaytx();
			IOWR_ALTERA_AVALON_PIO_DATA(UART_T_BASE, 0b0);
		}
		else if (hostguest == 1) {
			 char lcd2[5] = "Guest";
			 delayp();
			 IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b11000000);
			 for (int i = 0; i<sizeof(lcd2); i++){
			 	delayp();
			 	IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, 0);
			 }

			 delayp();
			 IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b11000000);
			 for (int i = 0; i<sizeof(lcd2); i++){
			 		delayp();
			 		IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, lcd2[i]);
			 	}
			hostguest = 0;
			uart_isr_self();
			printf("I am Guest\n");

			delaytx();
			IOWR_ALTERA_AVALON_PIO_DATA(UART_T_BASE, 0b1);
			delaytx();
			IOWR_ALTERA_AVALON_PIO_DATA(UART_T_BASE, 0b0);
		}
}
//--------------------------------KEY0 ISR --------------------------------------
void key0_isr(){
	IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b11000110);
	delayp();
	IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, 0x2d);

	delayp();
	if ((ready == 1) || (ready == 5)){
		char lcd[7] = "Waiting";
		 delayp();
		 IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b11001001);
		 for (int i = 0; i<sizeof(lcd); i++){
		 	delayp();
		 	IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, 0);
		 }

		delayp();
		IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b11001001);
		for (int i = 0; i<sizeof(lcd); i++){
			delayp();
			IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, lcd[i]);
				 	}
			ready = 0;
			printf("Waiting\n");
			uart_isr1_selfready();

			delaytx();
			IOWR_ALTERA_AVALON_PIO_DATA(UART_T1_BASE, 0b1);
			delaytx();
			IOWR_ALTERA_AVALON_PIO_DATA(UART_T1_BASE, 0b0);
		}
		else if (ready == 0) {
			char lcd2[7] = "Ready  ";
			 delayp();
			 IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b11001001);
			 for (int i = 0; i<sizeof(lcd2); i++){
			 	delayp();
			 	IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, 0);
			 }

			delayp();
			IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b11001001);
			for (int i = 0; i<sizeof(lcd2); i++){
				delayp();
				IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, lcd2[i]);
			}
			ready = 1;
			printf("Ready\n");
			uart_isr1_selfready();

			delaytx();
			IOWR_ALTERA_AVALON_PIO_DATA(UART_T1_BASE, 0b1);
			delaytx();
			IOWR_ALTERA_AVALON_PIO_DATA(UART_T1_BASE, 0b0);
		}
}

void sdc_upload(int c){

	volatile int i, j;
	alt_up_sd_card_dev *device_reference = NULL;

	int connected = 0;

	short int handler;
	short att1=0,att2=0,att3=0,att;
	int pixel;

	device_reference = alt_up_sd_card_open_dev("/dev/SD");

	if(device_reference!=NULL)
	{
		if((connected == 0) && (alt_up_sd_card_is_Present()))
		{
			printf("\ncard connected\n");

			if(alt_up_sd_card_is_FAT16())
			{
			printf("FAT16 file system detected. \n");

			switch (c){

			case 0:
				handler = alt_up_sd_card_fopen("intel.bmp", false);
				break;

			case 1:
				handler = alt_up_sd_card_fopen("smile.bmp", false);
				VGA_box(0,0,639,479,0xFFFF);
				break;

			case 2:
				handler = alt_up_sd_card_fopen("score.bmp", false);
				VGA_box(0,0,639,479,0x0000);
				break;

			case 3:
				handler = alt_up_sd_card_fopen("loading.bmp", false);
				VGA_box(0,0,639,479,0x0000);
				break;

			case 4:
				handler = alt_up_sd_card_fopen("gameover.bmp", false);
				VGA_box(0,0,639,479,0x0000);
				break;
			} // */

			//handler = alt_up_sd_card_fopen("gameover.bmp", false);
			att = alt_up_sd_card_get_attributes(handler);

			//VGA_box(0,0,639,479,0xFFFF);


			for (j=0;j<54;j++)
			{
				att1 = alt_up_sd_card_read(handler);
			}
			i=0,j=0;
			for (i=240;i>=0;i=i-1)
			{
				for (j=0;j<320;j=j+1)
				{
					att1 = (unsigned char)alt_up_sd_card_read(handler);
					att2 = (unsigned char)alt_up_sd_card_read(handler);
					att3 = (unsigned char)alt_up_sd_card_read(handler);

					pixel = ((att3>>3)<<11 | ((att2>>2)<<5) | (att1>>3)); // 24 to 16 bit conversion
					VGA_box(j, i, j, i, pixel);
				}
			}
			alt_up_sd_card_fclose(handler);
			}
			else
			{
				printf("Unknown File System.\n");
			}
			connected = 1;
			}
		else if ((connected == 1) && (alt_up_sd_card_is_Present() == false))
		{
			printf("Card Disconnected");
			connected = 0;
		}
	}

	delayceleb();
	delayceleb();
	delayceleb();
	VGA_box(0, 0, 640, 480, 0xFFFF);

	//----------------BOARDER--------------------
	// Center Line
	VGA_box(145, 0, 175, 240, 0x187F);

	// Bottom Line
	VGA_box(0, 225, 320, 240, 0x187F);

	// Top Line
	VGA_box(0, 0, 320, 15, 0x187F);

	// Left Line
	VGA_box(0, 0, 12, 240, 0x187F);

	// Right Line
	VGA_box(308, 0, 320, 240, 0x187F);

	switch (LeftScore){

	case 0:
		// Clear Screen
		VGA_box (30, 40, 120, 200, 0xFFFF);

		// Left Score Board
		VGA_box (60, 50, 90, 59, 0xF800);		// SEG0
		VGA_box (104, 68, 113, 107, 0xF800);	// SEG1
		VGA_box (104, 137, 113, 177, 0xF800);	// SEG2
		VGA_box (60, 180, 90, 189, 0xF800);		// SEG3
		VGA_box (36, 137, 45, 175, 0xF800);		// SEG4
		VGA_box (36, 68, 45, 107, 0xF800);		// SEG5
		//VGA_box (60, 114, 90, 125, 0xF800);		// SEG6

		break;

	case 1:
		// Clear Screen
		VGA_box (30, 40, 120, 200, 0xFFFF);

		// Left Score Board
		//VGA_box (60, 50, 90, 59, 0xF800);		// SEG0
		VGA_box (104, 68, 113, 107, 0xF800);	// SEG1
		VGA_box (104, 137, 113, 177, 0xF800);	// SEG2
		//VGA_box (60, 180, 90, 189, 0xF800);		// SEG3
		//VGA_box (36, 137, 45, 175, 0xF800);		// SEG4
		//VGA_box (36, 68, 45, 107, 0xF800);		// SEG5
		//VGA_box (60, 114, 90, 125, 0xF800);		// SEG6
		break;

	case 2:
		// Clear Screen
		VGA_box (30, 40, 120, 200, 0xFFFF);

		// Left Score Board
		VGA_box (60, 50, 90, 59, 0xF800);		// SEG0
		VGA_box (104, 68, 113, 107, 0xF800);	// SEG1
		//VGA_box (104, 137, 113, 177, 0xF800);	// SEG2
		VGA_box (60, 180, 90, 189, 0xF800);		// SEG3
		VGA_box (36, 137, 45, 175, 0xF800);		// SEG4
		//VGA_box (36, 68, 45, 107, 0xF800);		// SEG5
		VGA_box (60, 114, 90, 125, 0xF800);		// SEG6
		break;

	case 3:
		// Clear Screen
		VGA_box (30, 40, 120, 200, 0xFFFF);

		// Left Score Board
		VGA_box (60, 50, 90, 59, 0xF800);		// SEG0
		VGA_box (104, 68, 113, 107, 0xF800);	// SEG1
		VGA_box (104, 137, 113, 177, 0xF800);	// SEG2
		VGA_box (60, 180, 90, 189, 0xF800);		// SEG3
		//VGA_box (36, 137, 45, 175, 0xF800);		// SEG4
		//VGA_box (36, 68, 45, 107, 0xF800);		// SEG5
		VGA_box (60, 114, 90, 125, 0xF800);		// SEG6
		break;

	}

	switch (RightScore){

	case 0:
		// Clear Screen
		VGA_box (200, 40, 290, 200, 0xFFFF);

		// Right Score Board
		VGA_box (225, 50, 255, 59, 0xF800);		// SEG0
		VGA_box (269, 68, 278, 107, 0xF800);	// SEG1
		VGA_box (269, 137, 278, 177, 0xF800);	// SEG2
		VGA_box (225, 180, 255, 189, 0xF800);	// SEG3
		VGA_box (201, 137, 210, 175, 0xF800);	// SEG4
		VGA_box (201, 68, 210, 107, 0xF800);	// SEG5
		//VGA_box (225, 114, 255, 125, 0xF800);	// SEG6
		break;

	case 1:
		// Clear Screen
		VGA_box (200, 40, 290, 200, 0xFFFF);

		// Right Score Board
		//VGA_box (225, 50, 255, 59, 0xF800);		// SEG0
		VGA_box (269, 68, 278, 107, 0xF800);	// SEG1
		VGA_box (269, 137, 278, 177, 0xF800);	// SEG2
		//VGA_box (225, 180, 255, 189, 0xF800);	// SEG3
		//VGA_box (201, 137, 210, 175, 0xF800);	// SEG4
		//VGA_box (201, 68, 210, 107, 0xF800);	// SEG5
		//VGA_box (225, 114, 255, 125, 0xF800);	// SEG6
		break;

	case 2:
		// Clear Screen
		VGA_box (200, 40, 290, 200, 0xFFFF);

		// Right Score Board
		VGA_box (225, 50, 255, 59, 0xF800);		// SEG0
		VGA_box (269, 68, 278, 107, 0xF800);	// SEG1
		//VGA_box (269, 137, 278, 177, 0xF800);	// SEG2
		VGA_box (225, 180, 255, 189, 0xF800);	// SEG3
		VGA_box (201, 137, 210, 175, 0xF800);	// SEG4
		//VGA_box (201, 68, 210, 107, 0xF800);	// SEG5
		VGA_box (225, 114, 255, 125, 0xF800);	// SEG6
		break;

	case 3:
		// Clear Screen
		VGA_box (200, 40, 290, 200, 0xFFFF);

		// Right Score Board
		VGA_box (225, 50, 255, 59, 0xF800);		// SEG0
		VGA_box (269, 68, 278, 107, 0xF800);	// SEG1
		VGA_box (269, 137, 278, 177, 0xF800);	// SEG2
		VGA_box (225, 180, 255, 189, 0xF800);	// SEG3
		//VGA_box (201, 137, 210, 175, 0xF800);	// SEG4
		//VGA_box (201, 68, 210, 107, 0xF800);	// SEG5
		VGA_box (225, 114, 255, 125, 0xF800);	// SEG6
		break;

	}

	delayceleb();
	delayceleb();
	delayceleb();

	if (RightScore == 3){
		// Clear Screen
		VGA_box(0, 0, 640, 480, 0xFFFF);
		// Bottom Line
		VGA_box(0, 225, 320, 240, 0x187F);
		// Top Line
		VGA_box(0, 0, 320, 15, 0x187F);
		// Left Line
		VGA_box(0, 0, 12, 240, 0x187F);
		// Right Line
		VGA_box(308, 0, 320, 240, 0x187F);

		// P
		VGA_box(70, 98, 78, 134, 0xF800);
		VGA_box(70, 98, 102, 105, 0xF800);
		VGA_box(70, 112, 102, 120, 0xF800);
		VGA_box(94, 98, 102, 120, 0xF800);

		// 2
		VGA_box(110, 98, 142, 104, 0xF800);
		VGA_box(134, 98, 142, 114, 0xF800);
		VGA_box(110, 111, 142, 117, 0xF800);
		VGA_box(110, 114, 118, 134, 0xF800);
		VGA_box(110, 128, 142, 134, 0xF800);

		// W
		VGA_box(174, 98, 182, 134, 0xF800);
		VGA_box(186, 116, 194, 134, 0xF800);
		VGA_box(174, 128, 206, 134, 0xF800);
		VGA_box(198, 98, 206, 134, 0xF800);

		// I
		VGA_box(214, 98, 222, 134, 0xF800);

		// N
		VGA_box(230, 98, 238, 134, 0xF800);
		VGA_box(230, 98, 262, 104, 0xF800);
		VGA_box(254, 98, 262, 134, 0xF800);

		// !
		VGA_box(270, 98, 278, 122, 0xF800);
		VGA_box(270, 126, 278, 134, 0xF800);
	}

	if (LeftScore == 3){
		// Clear Screen
		VGA_box(0, 0, 640, 480, 0xFFFF);
		// Bottom Line
		VGA_box(0, 225, 320, 240, 0x187F);

		// Top Line
		VGA_box(0, 0, 320, 15, 0x187F);

		// Left Line
		VGA_box(0, 0, 12, 240, 0x187F);

		// Right Line
		VGA_box(308, 0, 320, 240, 0x187F);

		// P
		VGA_box(70, 98, 78, 134, 0xF800);
		VGA_box(70, 98, 102, 105, 0xF800);
		VGA_box(70, 112, 102, 120, 0xF800);
		VGA_box(94, 98, 102, 120, 0xF800);

		// 1
		VGA_box(118, 98, 126, 102, 0xF800);
		VGA_box(122, 98, 130, 134, 0xF800);
		VGA_box(112, 128, 140, 136, 0xF800);

		// W
		VGA_box(174, 98, 182, 134, 0xF800);
		VGA_box(186, 116, 194, 134, 0xF800);
		VGA_box(174, 128, 206, 134, 0xF800);
		VGA_box(198, 98, 206, 134, 0xF800);

		// I
		VGA_box(214, 98, 222, 134, 0xF800);

		// N
		VGA_box(230, 98, 238, 134, 0xF800);
		VGA_box(230, 98, 262, 104, 0xF800);
		VGA_box(254, 98, 262, 134, 0xF800);

		// !
		VGA_box(270, 98, 278, 122, 0xF800);
		VGA_box(270, 126, 278, 134, 0xF800);
	}

}

void VGA_box(int x1, int y1, int x2, int y2, int pixel_color)
{
	int offset, row, col;
	volatile short * pixel_buffer = (short *) 0x400000;
	for (row = y1; row <= y2; row++)
	{
		col = x1;
		while (col <= x2)
		{
			offset = (row << 9) + col;
			*(pixel_buffer + offset) = pixel_color;
			++col;
		}
	}
}

void hostcelebrate(){	// celebration for host winning
	printf("Host Won!\n");
	int hostwon = 0;
	while (hostwon < 30){
		IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, 0x2aaaa);
		IOWR_ALTERA_AVALON_PIO_DATA(LEDG_BASE, 0b10101010);
		delayceleb();
		IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, 0x15555);
		IOWR_ALTERA_AVALON_PIO_DATA(LEDG_BASE, 0b01010101);
		delayceleb();
		hostwon++;
	}
}

void othercelebrate(){	//celebration for guest winning
	printf("Guest Won!\n");
	int guestwon = 0;
	while (guestwon < 30){
		IOWR_ALTERA_AVALON_PIO_DATA(LEDG_BASE, 0b10101010);
		IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, 0x2aaaa);
		delayceleb();
		IOWR_ALTERA_AVALON_PIO_DATA(LEDG_BASE, 0b01010101);
		IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, 0x15555);
		delayceleb();
		guestwon++;
	}
}

void delaytx(){
	int delay = 0;
	while(delay<49999) delay++;
}
void delayp(){
	int delay = 0;
	while(delay<49999) delay++;
}
void delayshort(){
	int delay = 0;
	while(delay<29999) delay++;
}
void delayceleb(){
	int delay = 0;
	while(delay<299999) delay++;
}

int main(){
	printf("ECE178 - Ping Pong - Final Project");
	IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, 0);
	IOWR_ALTERA_AVALON_PIO_DATA(LEDG_BASE, 0);

	init_button_pio();
	init_uart();
	init_uart1();
	init_uart2();
	init_uart3();
	init_timer();
	init_timer2();

	// Call SD upload
	sdc_upload(3);

	//VGA_box(0, 0, 320, 240, 0x0000);

			char lcd[] = "LOADING...";
			char lcd2[] = "LOADING...";
			IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b10000000);
			for (int i = 0; i<sizeof(lcd)-1; i++){
				delayshort();
				IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, lcd[i]);
			}
			delayshort();
			IOWR_ALTERA_AVALON_LCD_16207_COMMAND(LCD_BASE, 0b11000000);
			for (int i = 0; i<sizeof(lcd2)-1; i++){
				delayshort();
				IOWR_ALTERA_AVALON_LCD_16207_DATA(LCD_BASE, lcd2[i]);
			}
			IOWR_ALTERA_AVALON_PIO_DATA(LEDS_BASE, 0x2aaaa);
			IOWR_ALTERA_AVALON_PIO_DATA(LEDG_BASE, 0b10101010);
			IOWR_ALTERA_AVALON_PIO_DATA(SEVEN_SEGMENT1_BASE, 0xFFFFFFFF);
			IOWR_ALTERA_AVALON_PIO_DATA(SEVEN_SEGMENT2_BASE, 0xFFFFFFFF);

			alt_u32 p = pong[0];
			p = p << 24;
			alt_u32 i = pong[4];
			i = i << 16;
			alt_u32 n = pong[2];
			n = n << 8;
			alt_u32 g = pong[3];
			alt_u32 o = pong[1];
			o = o << 16;
			alt_u32 ping = p + i + n + g;
			IOWR_ALTERA_AVALON_PIO_DATA(SEVEN_SEGMENT2_BASE, ping);
            alt_u32 pong = p + o + n + g;
            IOWR_ALTERA_AVALON_PIO_DATA(SEVEN_SEGMENT1_BASE, pong);

	while(1){
	}
	return 0;
}
