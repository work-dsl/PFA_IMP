/**
  ******************************************************************************
  * @copyright: Copyright To Hangzhou Dinova EP Technology Co.,Ltd
  * @file     : main.c
  * @author   : ZJY
  * @version  : V1.0
  * @date     : 20xx-xx-xx
  * @brief    : xxx
  *                  1.xx
  *                  2.xx
  *
  * @attention: None
  ******************************************************************************
  * @history  :
  *      V1.0 : 1.xxx
  *
  *
  *
  ******************************************************************************
  */
/*------------------------------ include --------------------------------------*/

#include "board.h"
#include "Impedance.h"

#include "serial_test.h"
#include "led_test.h"
#include "spi_test.h"
#include "ad5940.h"
#include "ad5272.h"
#include "gpio.h"

#define  LOG_TAG             "main"
#define  LOG_LVL             4
#include "log.h"

/*------------------------------ Macro definition -----------------------------*/

/*------------------------------ typedef definition ---------------------------*/


/*------------------------------ variables prototypes -------------------------*/

/*------------------------------ function prototypes --------------------------*/

/**
   User could configure following parameters
**/

#define APPBUFF_SIZE 512
uint32_t AppBuff[APPBUFF_SIZE];

int32_t ImpedanceShowResult(uint32_t *pData, uint32_t DataCount)
{
  float freq;

  fImpPol_Type *pImp = (fImpPol_Type*)pData;
  AppIMPCtrl(IMPCTRL_GETFREQ, &freq);

  printf("Freq:%.2f ", freq);
  /*Process data*/
  for(int i=0;i<DataCount;i++)
  {
    printf("RzMag: %f Ohm , RzPhase: %f \n",pImp[i].Magnitude,pImp[i].Phase*180/MATH_PI);
  }
  return 0;
}

static int32_t AD5940PlatformCfg(void)
{
  CLKCfg_Type clk_cfg;
  FIFOCfg_Type fifo_cfg;
  AGPIOCfg_Type gpio_cfg;

  /* Use hardware reset */
  AD5940_HWReset();
  AD5940_Initialize();
  /* Platform configuration */
  /* Step1. Configure clock */
  clk_cfg.ADCClkDiv = ADCCLKDIV_1;
  clk_cfg.ADCCLkSrc = ADCCLKSRC_HFOSC;
  clk_cfg.SysClkDiv = SYSCLKDIV_1;
  clk_cfg.SysClkSrc = SYSCLKSRC_HFOSC;
  clk_cfg.HfOSC32MHzMode = bFALSE;
  clk_cfg.HFOSCEn = bTRUE;
  clk_cfg.HFXTALEn = bFALSE;
  clk_cfg.LFOSCEn = bTRUE;
  AD5940_CLKCfg(&clk_cfg);
  /* Step2. Configure FIFO and Sequencer*/
  fifo_cfg.FIFOEn = bFALSE;
  fifo_cfg.FIFOMode = FIFOMODE_FIFO;
  fifo_cfg.FIFOSize = FIFOSIZE_4KB;                       /* 4kB for FIFO, The reset 2kB for sequencer */
  fifo_cfg.FIFOSrc = FIFOSRC_DFT;
  fifo_cfg.FIFOThresh = 4;//AppIMPCfg.FifoThresh;        /* DFT result. One pair for RCAL, another for Rz. One DFT result have real part and imaginary part */
  AD5940_FIFOCfg(&fifo_cfg);
  fifo_cfg.FIFOEn = bTRUE;
  AD5940_FIFOCfg(&fifo_cfg);
  
  /* Step3. Interrupt controller */
  AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);   /* Enable all interrupt in INTC1, so we can check INTC flags */
  AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
  AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE); 
  AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
  /* Step4: Reconfigure GPIO */
  gpio_cfg.FuncSet = GP0_INT|GP1_SLEEP|GP2_SYNC;
  gpio_cfg.InputEnSet = 0;
  gpio_cfg.OutputEnSet = AGPIO_Pin1|AGPIO_Pin2;
  gpio_cfg.OutVal = 0;
  gpio_cfg.PullEnSet = 0;
  AD5940_AGPIOCfg(&gpio_cfg);
  AD5940_SleepKeyCtrlS(SLPKEY_UNLOCK);  /* Allow AFE to enter sleep mode. */
  return 0;
}

void AD5940ImpedanceStructInit(void)
{
  AppIMPCfg_Type *pImpedanceCfg;
  
  AppIMPGetCfg(&pImpedanceCfg);
  /* Step1: configure initialization sequence Info */
  pImpedanceCfg->SeqStartAddr = 0;
  pImpedanceCfg->MaxSeqLen = 512; /* @todo add checker in function */

  pImpedanceCfg->RcalVal = 10000.0;
  pImpedanceCfg->SinFreq = 50000.0;
  pImpedanceCfg->FifoThresh = 4;
	
	/* Set switch matrix to onboard(EVAL-AD5940ELECZ) dummy sensor. */
	/* Note the RCAL0 resistor is 10kOhm. */
	pImpedanceCfg->DswitchSel = SWD_CE0;
	pImpedanceCfg->PswitchSel = SWP_RE0;
	pImpedanceCfg->NswitchSel = SWN_SE0;
	pImpedanceCfg->TswitchSel = SWT_SE0LOAD;
	/* The dummy sensor is as low as 5kOhm. We need to make sure RTIA is small enough that HSTIA won't be saturated. */
	pImpedanceCfg->HstiaRtiaSel = HSTIARTIA_1K;
	
	/* Configure the sweep function. */
	pImpedanceCfg->SweepCfg.SweepEn = bFALSE;
	pImpedanceCfg->SweepCfg.SweepStart = 100.0f;	/* Start from 1kHz */
	pImpedanceCfg->SweepCfg.SweepStop = 100e3f;		/* Stop at 100kHz */
	pImpedanceCfg->SweepCfg.SweepPoints = 101;		/* Points is 101 */
	pImpedanceCfg->SweepCfg.SweepLog = bTRUE;
	/* Configure Power Mode. Use HP mode if frequency is higher than 80kHz. */
	pImpedanceCfg->PwrMod = AFEPWR_HP;
	/* Configure filters if necessary */
	pImpedanceCfg->ADCSinc3Osr = ADCSINC3OSR_2;		/* Sample rate is 800kSPS/2 = 400kSPS */
  pImpedanceCfg->DftNum = DFTNUM_16384;
  pImpedanceCfg->DftSrc = DFTSRC_SINC3;
}

void AD5940_Main(void)
{
  uint32_t temp;  
  AD5940PlatformCfg();
  AD5940ImpedanceStructInit();
  
  AppIMPInit(AppBuff, APPBUFF_SIZE);    /* Initialize IMP application. Provide a buffer, which is used to store sequencer commands */
  AppIMPCtrl(IMPCTRL_START, 0);          /* Control IMP measurement to start. Second parameter has no meaning with this command. */
 
  while(1)
  {
    if(AD5940_GetMCUIntFlag())
    {
      AD5940_ClrMCUIntFlag();
      temp = APPBUFF_SIZE;
      AppIMPISR(AppBuff, &temp);
      ImpedanceShowResult(AppBuff, temp);
    }
  }
}

static ad5272_dev_t ad5272_dev;  /* AD5272设备实例 */

/**
 * @brief AD5272测试初始化
 * @return 0成功，负数错误码
 */
static int ad5272_test_init(void)
{
    int ret = 0;
    
    LOG_I("=== AD5272 Test Init ===");

    /* 初始化AD5272设备 */
    ret = ad5272_init(&ad5272_dev, AD5272_DEFAULT_I2C_ADDR, AD5272_DEFAULT_ADAPTER);
    if (ret != 0) {
        LOG_E("AD5272 init failed: %d", ret);
        return ret;
    }
    
    LOG_I("AD5272 initialized successfully");
    return 0;
}

/**
 * @brief 基本功能测试
 */
static void ad5272_test_basic(void)
{
    int ret = 0;
    uint16_t position = 0U;
    uint8_t status = 0U;
    
    LOG_I("=== AD5272 Basic Test ===");
    
    /* 测试1: 读取当前阻值位置 */
    ret = ad5272_get_resistance(&ad5272_dev, &position);
    if (ret == 0) {
        LOG_I("Test 1 PASS: Read resistance position = %d", position);
    } else {
        LOG_E("Test 1 FAIL: Read resistance failed: %d", ret);
    }
    
    /* 测试2: 读取状态寄存器 */
    ret = ad5272_read_status(&ad5272_dev, &status);
    if (ret == 0) {
        LOG_I("Test 2 PASS: Read status = 0x%02X", status);
    } else {
        LOG_E("Test 2 FAIL: Read status failed: %d", ret);
    }
}

/*------------------------------ application ----------------------------------*/
/**
 * @brief  Main program
 * @param  None
 * @retval None
 */
int main(void)
{
    /* 底层驱动初始化 */
    board_init();

    /* 测试初始化 */
    led_test_init();
    serial_test_init();
    gpio_set_mode(3, PIN_OUTPUT_PP, PIN_PULL_UP);
    gpio_write(3, 0);
    spi_test_init();
    spi_test_task();
    AD5940_MCUResourceInit(0);
    ad5272_test_init();
    ad5272_test_basic();
    AD5940_Main();
    
    while (1)
    {
        led_test_task();
        serial_test_task();
    }
}

/******************************* End Of File ************************************/


