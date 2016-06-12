/*************************************************************/
/*    The following firmware is to initialize a MAX9860      */
/*so that it works with a NUCLEO-F429Z ARM development board */
/*It is crrently set up to send audio signals to the MAX9860 */
/*          But cannot yet receive microphone signals        */
/*                                                           */
/*The I2C protocal is used to communicate between the devices*/
/*           This was written by Jacob Law, 2016             */
/*************************************************************/
/*

                            Vcc (1.7-1.9V)
                            ^
                            | 
                            |
                            \ 1K
---------------------       /                               -------------------
|                    |      \              1K              |                  |
|             9(SDA) |--------------------/\/\/\-----------| PF0(SDA)         |
|            10(SCL) |--------------------/\/\/\-----------| PF1(SCL)         |
|                    |      |               1K             |                  |
|                    |      \                              |                  |
|     MAX9860        |      / 1K                           |       ARM        |
|                    |      \                              |                  |
|                    |      |                              |                  |
|       14(MCLK)     |      |                              |    PC9(MCO2)     |
---------------------       Vcc(1.7-1.9V)                  --------------------
             |                                                      |
             -------------------------------------------------------

*/
              

#define CLOCK_FREQUENCY 16000000 //assuming use of internal 16MHz oscillator in ARM chip
#define I2C_CLOCK_FREQUENCY 100000	//Setting for 100KHz

#define I2C2_CLOCK PF1			//this is the pin from the datasheet
#define I2C2_DATA PF0			//this is the pin from the datasheet

#define MAX9860 0				//unsure of address

#define N (65536*96*16000)/16000000	// N = (65536*96*F_lrclk)/F_pclk
#define NHI N>>8					//high 7 bits of N
#define NLO N&0b000000011111111		//NLO is lower 8 bits of N

void ARM_Initialization_I2C(void)
{
	//This series of initialization steps was ofund on page 847 of the reference manual RM0090 for master mode
	
	I2C_CR2 &= 0X11C0; //clear the frequency bits
	I2C_CR2 |= 0X0010; //frequency is 16 MHz, so this is put into the register. other bits not yet set, reserved not effected
	
	I2C_CCR &= 0X0300;	//clear all bits that aren't reserved
	I2C_CCR |= 0X37FF;	//slow mode, high time and low time equal (nice square wave) for SCL
	
	I2C_CR1 &= 0X4004;	//clear all bits, keep reserved
	I2C_CR1 |= 0X40F9; //SWRST = 0, ALERT = 0, PEC = 0, POs = 0, ACK = 0, START = 0, STOP = 0, NOSTRETCH = 1, ENGC = 1, ENPEC = 1, ENARP = 1, SMBTYPE = 1, SMBUS = 0, PE = 0
	
	return;
}

void sendTwoBytes(int address, int reg, int data)
{
	//set start bit
	I2C_CR1 |= 0X0100;
	
	//move the address of the MAX9860 to the data register
	I2C_DR |= (0X1100 + (address<<1));	// RRRR RRRR AAAA AAAX where X choses transmit or receive, we want transmit (0)
	
	//wait for the transmit buffer to empty
	while(!TxE);
	
	//move the desired register address (data for register on MAX9860) to data register
	I2C_DR |= (0X1100 + reg);
	
	//wait for transmit buffer to empty
	while(!TxE);
	
	//load data for MAX9860 into data register, shift register not yet empty
	I2C_DR |= (0x1100 + data);
	
	//wait for transmission to end
	while(!TxE && !BTF);
	
	//send stop condition by setting stop bit
	I2C_CR1 ^= 0b0000001000000000;	//only change the stop bit to a 1
	
	
}

void main(void)
{
	//set SYSCLK at output from MCO2 (PIN PC9), this will be the MCLK for MAX9860
	RCC_CFGR &= 0x07ff	//SYSCLK output & no prescaller for MCO2, all other bits unchanged
	
	PE = 0;	//disable I2C for initialization
	
	//start both high, preference
	I2C_CLOCK = 1;
	I2C_DATA = 1;
	
	//this sets all the registers thatwe need to have the ARM chip working as a master transmitter
	ARM_Initialization_I2C();
	
	PE = 1;	//enable I2C
	
	/*Initialize the MAX9860 by setting registers*/
	
	// registers at 0x03, 0x04, and 0x05 are clock control registers
	sendTwoBytes(MAX9860, 0X03, 0X11);	//PSCLK = 01, FREQ = 00, 16KHz = 1 (LRCLK = 16KHz)
	sendTwoBytes(MAX9860, 0X04, NHI);	//PLL =0,
	sendTwoBytes(MAX9860, 0X05, NLO);
	
	// registers at 0x06 and 0x07 are digital audio interface registers
	sendTwoBytes(MAX9860, 0X06, 0x00);	//MAS = 0, WCI = 0 (really not sure), DBCI = 0, DDLY = 0, HIZ = 0, TDM = 0, 
	sendTwoBytes(MAX9860, 0X07, 0x00);	//ABCI = 0, ADLY = 0, ST = 0, BSEL = 000
	
	//Digital filters
	sendTwoBytes(MAX9860, 0x08, 0x00);	//AVFLT = 0, DVFLT = 0 (not sure which filter would be best for this, choosing no filter)
	
	//Digital level control registers
	sendTwoBytes(MAX9860, 0x09, 0x06);	//0 DAC adjustment, this would require testing and/or a better understanding of the overall system
	sendTwoBytes(MAX9860, 0x0A, 0x33);	//both ADC's set to 0 adjustment, same reason as above AND mic not supported yet
	sendTwoBytes(MAX9860, 0x0B, 0x00);	//no gain on DAC, unsure of DVST bits safer to disable
	
	//microphone input register
	sendTwoBytes(MAX9860, 0x0c, 0x00);	//I haven't set this up to allow microphones (make MAX9860 master, ARM slave)
	
	//AGC and Noise gate registers
	sendTwoBytes(MAX9860, 0x0E, 0x80);	//sum of left & right noise gates for AGC & noise gate, AGCRLS shortest time, but AGC disabled since I am unsure of its need
	sendTwoBytes(MAX9860, 0x0f, 0x00);	//Noise gate threshhold disabled, AGC signal threshhold -3bBFS
	
	//power management register
	sendTwoBytes(MAX9860, 0x10, 0x88);	//powered on, DAC on, both ADC's off (since this is not set up for microphone input)
	
	
	//add any functionality, codec is now initialized to receive digital auio signals. 
	while(1);
	
	return;
}