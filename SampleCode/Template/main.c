/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include <string.h>
#include "NuMicro.h"

#include "project_config.h"


/*_____ D E C L A R A T I O N S ____________________________________________*/
// #define ENABLE_ADC_INTERRUPT

#define ADC_12BIT
// #define ADC_10BIT
/*_____ D E F I N I T I O N S ______________________________________________*/
volatile uint32_t BitFlag = 0;
volatile uint32_t counter_tick = 0;

int32_t  i32BuiltInData = 0;
uint32_t CalculateAVDD = 0;
const uint8_t getBandGapLoop = 4;

uint8_t adcConvoterCount = 0;
uint32_t usCurrent_ADC = 0;
uint32_t upCurrent_ADC = 0;
uint32_t temp_ADC = 0;
uint32_t avdd_ADC = 0;

const uint8_t avg_count = 8;
const uint8_t shiftBits = 2; 

#define ADC_DIGITAL_SCALE(void) 							(0xFFFU >> ((0) >> (3U - 1U)))		//0: 12 BIT 
#define ADC_CALC_DATA_TO_VOLTAGE(DATA,VREF) 				((DATA) * (VREF) / ADC_DIGITAL_SCALE())

#define ADC_DIGITAL_SCALE_10bit(void) 						(0x3FFU >> ((0) >> (3U - 1U)))
#define ADC_CALC_DATA_TO_VOLTAGE_10bit(DATA,VREF) 			((DATA) * (VREF) / ADC_DIGITAL_SCALE_10bit())


/*_____ M A C R O S ________________________________________________________*/

/*_____ F U N C T I O N S __________________________________________________*/

void tick_counter(void)
{
	counter_tick++;
}

uint32_t get_tick(void)
{
	return (counter_tick);
}

void set_tick(uint32_t t)
{
	counter_tick = t;
}

void compare_buffer(uint8_t *src, uint8_t *des, int nBytes)
{
    uint16_t i = 0;	
	
    for (i = 0; i < nBytes; i++)
    {
        if (src[i] != des[i])
        {
            printf("error idx : %4d : 0x%2X , 0x%2X\r\n", i , src[i],des[i]);
			set_flag(flag_error , ENABLE);
        }
    }

	if (!is_flag_set(flag_error))
	{
    	printf("%s finish \r\n" , __FUNCTION__);	
		set_flag(flag_error , DISABLE);
	}

}

void reset_buffer(void *dest, unsigned int val, unsigned int size)
{
    uint8_t *pu8Dest;
//    unsigned int i;
    
    pu8Dest = (uint8_t *)dest;

	#if 1
	while (size-- > 0)
		*pu8Dest++ = val;
	#else
	memset(pu8Dest, val, size * (sizeof(pu8Dest[0]) ));
	#endif
	
}

void copy_buffer(void *dest, void *src, unsigned int size)
{
    uint8_t *pu8Src, *pu8Dest;
    unsigned int i;
    
    pu8Dest = (uint8_t *)dest;
    pu8Src  = (uint8_t *)src;


	#if 0
	  while (size--)
	    *pu8Dest++ = *pu8Src++;
	#else
    for (i = 0; i < size; i++)
        pu8Dest[i] = pu8Src[i];
	#endif
}

void dump_buffer(uint8_t *pucBuff, int nBytes)
{
    uint16_t i = 0;
    
    printf("dump_buffer : %2d\r\n" , nBytes);    
    for (i = 0 ; i < nBytes ; i++)
    {
        printf("0x%2X," , pucBuff[i]);
        if ((i+1)%8 ==0)
        {
            printf("\r\n");
        }            
    }
    printf("\r\n\r\n");
}

void  dump_buffer_hex(uint8_t *pucBuff, int nBytes)
{
    int     nIdx, i;

    nIdx = 0;
    while (nBytes > 0)
    {
        printf("0x%04X  ", nIdx);
        for (i = 0; i < 16; i++)
            printf("%02X ", pucBuff[nIdx + i]);
        printf("  ");
        for (i = 0; i < 16; i++)
        {
            if ((pucBuff[nIdx + i] >= 0x20) && (pucBuff[nIdx + i] < 127))
                printf("%c", pucBuff[nIdx + i]);
            else
                printf(".");
            nBytes--;
        }
        nIdx += 16;
        printf("\n");
    }
    printf("\n");
}



void delay_ms(uint16_t ms)
{
	TIMER_Delay(TIMER0, 1000*ms);
}

__STATIC_INLINE uint32_t FMC_ReadBandGap(void)
{
    FMC->ISPCMD = FMC_ISPCMD_READ_UID;            /* Set ISP Command Code */
    FMC->ISPADDR = 0x70u;                         /* Must keep 0x70 when read Band-Gap */
    FMC->ISPTRG = FMC_ISPTRG_ISPGO_Msk;           /* Trigger to start ISP procedure */
#if ISBEN
    __ISB();
#endif                                            /* To make sure ISP/CPU be Synchronized */
    while(FMC->ISPTRG & FMC_ISPTRG_ISPGO_Msk) {}  /* Waiting for ISP Done */

    return FMC->ISPDAT & 0xFFF;
}


int32_t getBuiltInBandGap(void)
{
    int32_t res = 0;
    SYS_UnlockReg();
    FMC_Open();
    res = FMC_ReadBandGap();

    return res;
}

void ADC_IRQHandler(void)
{
    set_flag(flag_ADC_Convert , ENABLE);
    ADC_CLR_INT_FLAG(ADC, ADC_ADF_INT); /* Clear the A/D interrupt flag */
}


int32_t bandGapCalculateAVDD(void)
{
    int32_t  i32ConversionData;
    int32_t  i = 0;

    CalculateAVDD = 0;
    set_flag(flag_ADC_Convert , DISABLE);

    /* Enable ADC converter */
    ADC_POWER_ON(ADC);

    /* Set input mode as single-end, Single mode, and select channel 29 (band-gap voltage) */
    ADC_Open(ADC, ADC_ADCR_DIFFEN_SINGLE_END, ADC_ADCR_ADMD_SINGLE_CYCLE, BIT0|BIT1|BIT11|BIT29);

    /* To sample band-gap precisely, the ADC capacitor must be charged at least 3 us for charging the ADC capacitor ( Cin )*/
    /* Sampling time = extended sampling time + 1 */
    /* 1/24000000 * (Sampling time) = 3 us */
    ADC_SetExtendSampleTime(ADC, 0, 71);

    /* Clear the A/D interrupt flag for safe */
    ADC_CLR_INT_FLAG(ADC, ADC_ADF_INT);

    #if defined (ENABLE_ADC_INTERRUPT)
    ADC_ENABLE_INT(ADC, ADC_ADF_INT);  /* Enable sample module A/D interrupt. */
    NVIC_EnableIRQ(ADC_IRQn);
    #endif

    for (i=0 ;i!=getBandGapLoop;i++)   
    {
        #if defined (ENABLE_ADC_INTERRUPT)
        set_flag(flag_ADC_Convert , DISABLE);
        ADC_START_CONV(ADC);
        while (is_flag_set(flag_ADC_Convert) == DISABLE);
        #else
        ADC_START_CONV(ADC);
        while (ADC_GET_INT_FLAG(ADC, ADC_ADF_INT)==0);
        ADC_CLR_INT_FLAG(ADC, ADC_ADF_INT);
        #endif

        #if 1
        while(ADC_IS_DATA_VALID(ADC, 0) == 0);
        while(ADC_IS_DATA_VALID(ADC, 1) == 0);
        while(ADC_IS_DATA_VALID(ADC, 11) == 0);
        while(ADC_IS_DATA_VALID(ADC, 29) == 0);
        #endif

        ADC_GET_CONVERSION_DATA(ADC, 0);
        ADC_GET_CONVERSION_DATA(ADC, 1);
        ADC_GET_CONVERSION_DATA(ADC, 11);
        i32ConversionData += ADC_GET_CONVERSION_DATA(ADC, 29);
    }

    i32BuiltInData = getBuiltInBandGap();

    i32ConversionData = i32ConversionData / getBandGapLoop; 
    CalculateAVDD = 3072*i32BuiltInData/i32ConversionData;
    // i32BuiltInData_MUL_3p072 = 3.072*(i32BuiltInData>>shiftBits);    
    printf("AVdd = 3072 * %d / %d = %d mV \r\n", i32BuiltInData, i32ConversionData, CalculateAVDD);    


    return CalculateAVDD;
}

int32_t adc_process (void)
{
    #if defined (ENABLE_ADC_INTERRUPT)
    set_flag(flag_ADC_Convert , DISABLE);    
    ADC_START_CONV(ADC);
    while (is_flag_set(flag_ADC_Convert) == DISABLE);

    #else
    ADC_START_CONV(ADC);    
    while (ADC_GET_INT_FLAG(ADC, ADC_ADF_INT)==0);
        
    ADC_CLR_INT_FLAG(ADC, ADC_ADF_INT); 
    #endif

    #if 1 
    while(ADC_IS_DATA_VALID(ADC, 0) == 0);
    while(ADC_IS_DATA_VALID(ADC, 1) == 0);
    while(ADC_IS_DATA_VALID(ADC, 11) == 0);
    while(ADC_IS_DATA_VALID(ADC, 29) == 0);
    #endif    
        
    adcConvoterCount++; 

    #if defined (ADC_12BIT)
    usCurrent_ADC += ADC_GET_CONVERSION_DATA(ADC, 0);
    upCurrent_ADC += ADC_GET_CONVERSION_DATA(ADC, 1);
    temp_ADC += ADC_GET_CONVERSION_DATA(ADC, 11);
    #elif defined (ADC_10BIT)
    usCurrent_ADC += ADC_GET_CONVERSION_DATA(ADC, 0)>>shiftBits;
    upCurrent_ADC += ADC_GET_CONVERSION_DATA(ADC, 1)>>shiftBits;
    temp_ADC += ADC_GET_CONVERSION_DATA(ADC, 11)>>shiftBits;
    #endif

     avdd_ADC += ADC_GET_CONVERSION_DATA(ADC, 29);   

    if (adcConvoterCount == (avg_count))
    {
        adcConvoterCount = 0;        
        usCurrent_ADC  /= avg_count;
        upCurrent_ADC /= avg_count;
        temp_ADC   /= avg_count;

        avdd_ADC /= avg_count;
        CalculateAVDD = 3072*i32BuiltInData/avdd_ADC;

        #if defined (ADC_12BIT)
        printf("usCurrent_ADC:%4dmv,upCurrent_ADC:%4dmv,temp_ADC:%4dmv,avdd_ADC:0x%4X,%4d\r\n",
                ADC_CALC_DATA_TO_VOLTAGE(usCurrent_ADC,CalculateAVDD) , 
                ADC_CALC_DATA_TO_VOLTAGE(upCurrent_ADC,CalculateAVDD) , 
                ADC_CALC_DATA_TO_VOLTAGE(temp_ADC,CalculateAVDD) , 
                avdd_ADC , CalculateAVDD);
        #elif defined (ADC_10BIT)
        printf("usCurrent_ADC:%4dmv,upCurrent_ADC:%4dmv,temp_ADC:%4dmv,avdd_ADC:0x%4X,%4d\r\n",
                ADC_CALC_DATA_TO_VOLTAGE_10bit(usCurrent_ADC,CalculateAVDD) , 
                ADC_CALC_DATA_TO_VOLTAGE_10bit(upCurrent_ADC,CalculateAVDD) , 
                ADC_CALC_DATA_TO_VOLTAGE_10bit(temp_ADC,CalculateAVDD) , 
                avdd_ADC , CalculateAVDD);        

        #endif

        usCurrent_ADC = 0;
        upCurrent_ADC = 0;
        temp_ADC = 0;
        avdd_ADC = 0;

    }


    return 0; 
}

void process (void)
{
    adc_process();

}

void GPIO_Init (void)
{
    SYS->GPB_MFPH = (SYS->GPB_MFPH & ~(SYS_GPB_MFPH_PB14MFP_Msk)) | (SYS_GPB_MFPH_PB14MFP_GPIO);
    SYS->GPB_MFPH = (SYS->GPB_MFPH & ~(SYS_GPB_MFPH_PB15MFP_Msk)) | (SYS_GPB_MFPH_PB15MFP_GPIO);

    GPIO_SetMode(PB, BIT14, GPIO_MODE_OUTPUT);
    GPIO_SetMode(PB, BIT15, GPIO_MODE_OUTPUT);	

}


void TMR1_IRQHandler(void)
{
//	static uint32_t LOG = 0;

	
    if(TIMER_GetIntFlag(TIMER1) == 1)
    {
        TIMER_ClearIntFlag(TIMER1);
		tick_counter();

		if ((get_tick() % 1000) == 0)
		{
//        	printf("%s : %4d\r\n",__FUNCTION__,LOG++);

			PB14 ^= 1;
		}

		if ((get_tick() % 50) == 0)
		{

		}	
    }
}


void TIMER1_Init(void)
{
    TIMER_Open(TIMER1, TIMER_PERIODIC_MODE, 1000);
    TIMER_EnableInt(TIMER1);
    NVIC_EnableIRQ(TMR1_IRQn);	
    TIMER_Start(TIMER1);
}

void UARTx_Process(void)
{
	uint8_t res = 0;
	res = UART_READ(UART0);

	if (res == 'x' || res == 'X')
	{
		NVIC_SystemReset();
	}

	if (res > 0x7F)
	{
		printf("invalid command\r\n");
	}
	else
	{
		switch(res)
		{
			case '1':
				break;

			case 'X':
			case 'x':
			case 'Z':
			case 'z':
				NVIC_SystemReset();		
				break;
		}
	}
}

void UART02_IRQHandler(void)
{
    if(UART_GET_INT_FLAG(UART0, UART_INTSTS_RDAINT_Msk | UART_INTSTS_RXTOINT_Msk))     /* UART receive data available flag */
    {
        while(UART_GET_RX_EMPTY(UART0) == 0)
        {
			UARTx_Process();
        }
    }

    if(UART0->FIFOSTS & (UART_FIFOSTS_BIF_Msk | UART_FIFOSTS_FEF_Msk | UART_FIFOSTS_PEF_Msk | UART_FIFOSTS_RXOVIF_Msk))
    {
        UART_ClearIntFlag(UART0, (UART_INTSTS_RLSINT_Msk| UART_INTSTS_BUFERRINT_Msk));
    }	
}

void UART0_Init(void)
{
    SYS_ResetModule(UART0_RST);

    /* Configure UART0 and set UART0 baud rate */
    UART_Open(UART0, 115200);
    UART_EnableInt(UART0, UART_INTEN_RDAIEN_Msk | UART_INTEN_RXTOIEN_Msk);
    NVIC_EnableIRQ(UART02_IRQn);
	
	#if (_debug_log_UART_ == 1)	//debug
	printf("\r\nCLK_GetCPUFreq : %8d\r\n",CLK_GetCPUFreq());
	printf("CLK_GetHXTFreq : %8d\r\n",CLK_GetHXTFreq());
	printf("CLK_GetLXTFreq : %8d\r\n",CLK_GetLXTFreq());	
	printf("CLK_GetPCLK0Freq : %8d\r\n",CLK_GetPCLK0Freq());
	printf("CLK_GetPCLK1Freq : %8d\r\n",CLK_GetPCLK1Freq());	
	#endif	

}

void SYS_Init(void)
{
    /* Unlock protected registers */
    SYS_UnlockReg();

    CLK_EnableXtalRC(CLK_PWRCTL_HIRCEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk);

//    CLK_EnableXtalRC(CLK_PWRCTL_HXTEN_Msk);
//    CLK_WaitClockReady(CLK_STATUS_HXTSTB_Msk);

//    CLK_EnableXtalRC(CLK_PWRCTL_LIRCEN_Msk);
//    CLK_WaitClockReady(CLK_STATUS_LIRCSTB_Msk);	

//    CLK_EnableXtalRC(CLK_PWRCTL_LXTEN_Msk);
//    CLK_WaitClockReady(CLK_STATUS_LXTSTB_Msk);	

    /* Select HCLK clock source as HIRC and HCLK source divider as 1 */
    CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_HIRC, CLK_CLKDIV0_HCLK(1));

    CLK_EnableModuleClock(UART0_MODULE);
    CLK_SetModuleClock(UART0_MODULE, CLK_CLKSEL1_UART0SEL_HIRC, CLK_CLKDIV0_UART0(1));

    CLK_EnableModuleClock(TMR0_MODULE);
  	CLK_SetModuleClock(TMR0_MODULE, CLK_CLKSEL1_TMR0SEL_HIRC, 0);

    CLK_EnableModuleClock(TMR1_MODULE);
  	CLK_SetModuleClock(TMR1_MODULE, CLK_CLKSEL1_TMR1SEL_HIRC, 0);

    CLK_EnableModuleClock(ADC_MODULE);
    CLK_SetModuleClock(ADC_MODULE, CLK_CLKSEL2_ADCSEL_PCLK1, CLK_CLKDIV0_ADC(2));

    /* Set PB multi-function pins for UART0 RXD=PB.12 and TXD=PB.13 */
    SYS->GPB_MFPH = (SYS->GPB_MFPH & ~(SYS_GPB_MFPH_PB12MFP_Msk | SYS_GPB_MFPH_PB13MFP_Msk)) |
                    (SYS_GPB_MFPH_PB12MFP_UART0_RXD | SYS_GPB_MFPH_PB13MFP_UART0_TXD);


    GPIO_SetMode(PB, BIT0|BIT1, GPIO_MODE_INPUT);
    /* Configure the PB.0 - PB.3 ADC analog input pins.  */
    SYS->GPB_MFPL = (SYS->GPB_MFPL & ~(SYS_GPB_MFPL_PB0MFP_Msk | SYS_GPB_MFPL_PB1MFP_Msk )) |
                    (SYS_GPB_MFPL_PB0MFP_ADC0_CH0 | SYS_GPB_MFPL_PB1MFP_ADC0_CH1);
    /* Disable the PB.0 - PB.3 digital input path to avoid the leakage current. */
    GPIO_DISABLE_DIGITAL_PATH(PB, BIT0|BIT1);

    GPIO_SetMode(PB, BIT11, GPIO_MODE_INPUT);
    /* Configure the PB.0 - PB.3 ADC analog input pins.  */
    SYS->GPB_MFPH = (SYS->GPB_MFPH & ~(SYS_GPB_MFPH_PB11MFP_Msk )) |
                    (SYS_GPB_MFPH_PB11MFP_ADC0_CH11);
    /* Disable the PB.0 - PB.3 digital input path to avoid the leakage current. */
    GPIO_DISABLE_DIGITAL_PATH(PB, BIT11);

   /* Update System Core Clock */
    SystemCoreClockUpdate();

    /* Lock protected registers */
    SYS_LockReg();
}

/*
 * This is a template project for M031 series MCU. Users could based on this project to create their
 * own application without worry about the IAR/Keil project settings.
 *
 * This template application uses external crystal as HCLK source and configures UART0 to print out
 * "Hello World", users may need to do extra system configuration based on their system design.
 */

int main()
{
    SYS_Init();

	GPIO_Init();
	UART0_Init();
	TIMER1_Init();

    bandGapCalculateAVDD();

    /* Got no where to go, just loop forever */
    while(1)
    {
        process();

    }
}

/*** (C) COPYRIGHT 2017 Nuvoton Technology Corp. ***/
