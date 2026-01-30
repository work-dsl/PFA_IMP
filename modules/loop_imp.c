/**
    ******************************************************************************
    * @file        : loop_imp.c
    * @author      : ZJY
    * @version     : V1.0
    * @date        : 2025-01-XX
    * @brief       : 回路阻抗测量模块实现
    * @attention   : None
    ******************************************************************************
    * @history     :
    *         V1.0 : 1. 回路阻抗测量序列实现
    *
    *
    ******************************************************************************
    */
/* Includes ------------------------------------------------------------------*/
#include "loop_imp.h"
#include "spi.h"
#include "port_ctrl.h"

#define  LOG_TAG             "loop_imp"
#define  LOG_LVL             4
#include "log.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/
/* Default LPDAC resolution(2.5V internal reference). */
#define DAC12BITVOLT_1LSB   (2200.0f/4095)  /* mV */
#define DAC6BITVOLT_1LSB    (DAC12BITVOLT_1LSB*64)  /* mV */

/* 物理基准 (来自 0Ω 实测) */
#define R_BASE_OFFSET        1005.1f      
#define SYSTEM_PHASE_OFFSET  (0.003456f)   /* 度 */

/* ========================================================================== */

#define APPBUFF_SIZE 512
uint32_t AppBuff[APPBUFF_SIZE];

/* 定义设备 */
static struct spi_device my_spi_device = {
    .name = "ad5940",
    .controller = NULL,
    .max_speed_hz = 10000000U,      /* 16MHz */
    .chip_select = 0U,              /* 硬件CS编号 */
    .mode = SPI_MODE_0 | SPI_MODE_MSB | SPI_MODE_SW_CS | SPI_MODE_4WIRE,
    .bits_per_word = 8U,
    .cs_pin = 4U,
    .controller_data = NULL
};

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* 最新的阻抗实部数据（幅度值） */
static float g_latest_imp_real = 0.0f;

/* 
    Application configuration structure. Specified by user from template.
    The variables are usable in this whole application.
    It includes basic configuration for sequencer generator and application related parameters
*/
loop_imp_cfg_t imp_cfg = 
{
    .bParaChanged = bFALSE,

    .SeqStartAddr = 0,
    .MaxSeqLen = 512,
    .SeqStartAddrCal = 0,
    .MaxSeqLenCal = 0,

    .ImpODR = 50,
    .NumOfData = -1,
    .SysClkFreq = 16000000.0,
    .WuptClkFreq = 32000.0,
    .AdcClkFreq = 16000000.0,
    
    .RcalVal = 1000.0,
    
    .DswitchSel = SWD_CE0,
    .PswitchSel = SWP_RE0,
    .NswitchSel = SWN_SE0,
    .TswitchSel = SWT_DE0,

    .PwrMod = AFEPWR_HP,

    .HstiaRtiaSel = HSTIARTIA_1K,
    .ExcitBufGain = EXCITBUFGAIN_2,
    .HsDacGain = HSDACGAIN_1,
    .HsDacUpdateRate = 7,
    .DacVoltPP = 600.0,
    .BiasVolt = -0.0f,
    .SinFreq = 50000.0, /* 50000Hz */

    .DftNum = DFTNUM_16384,
    .DftSrc = DFTSRC_SINC3,
    .HanWinEn = bTRUE,

    .AdcPgaGain = ADCPGA_2,
    .ADCSinc3Osr = ADCSINC3OSR_2,
    .ADCSinc2Osr = ADCSINC2OSR_22,
    
    .ADCAvgNum = ADCAVGNUM_16,
    
    .FifoThresh = 4,
    .IMPInited = bFALSE,
    .StopRequired = bFALSE,
};

/* Exported variables  -------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
static AD5940Err AppIMPSeqCfgGen(void);
static AD5940Err AppIMPSeqMeasureGen(void);
static int32_t AppIMPRegModify(int32_t * const pData, uint32_t *pDataCount);
static int32_t loop_imp_data_process(int32_t * const pData, uint32_t *pDataCount);
static AD5940Err AppADCPgaCal(void);
static AD5940Err AppHSDACCal(void);

static void convert_to_int32(int32_t *pData, uint32_t DataCount);

/* Exported functions --------------------------------------------------------*/

int32_t loop_imp_init(void)
{
    AD5940Err error = AD5940ERR_OK;  
    SEQCfg_Type seq_cfg;
    uint8_t tx_data[] = {0x20, 0x04, 0x00};
    CLKCfg_Type clk_cfg;
    FIFOCfg_Type fifo_cfg;
    AGPIOCfg_Type gpio_cfg;
    
    error = spi_device_attach(&my_spi_device, "spi1");
    if (error != 0) {
        LOG_E("spi_device_attach errno!");
        return error;
    }
    spi_write(&my_spi_device, tx_data, 3);
    
    AD5940_MCUResourceInit(0);

    /* Use hardware reset */
    AD5940_HWReset();
    AD5940_Initialize();
    
    /* 时钟配置 */
    clk_cfg.ADCClkDiv = ADCCLKDIV_1;
    clk_cfg.ADCCLkSrc = ADCCLKSRC_XTAL;
    clk_cfg.SysClkDiv = SYSCLKDIV_1;
    clk_cfg.SysClkSrc = SYSCLKSRC_XTAL;
    clk_cfg.HfOSC32MHzMode = bFALSE;
    clk_cfg.HFOSCEn = bFALSE;
    clk_cfg.HFXTALEn = bTRUE;
    clk_cfg.LFOSCEn = bTRUE;
    AD5940_CLKCfg(&clk_cfg);
    
    /* Configure FIFO and Sequencer*/
    fifo_cfg.FIFOEn = bFALSE;
    fifo_cfg.FIFOMode = FIFOMODE_FIFO;
    fifo_cfg.FIFOSize = FIFOSIZE_4KB;   /* 4kB for FIFO, The reset 2kB for sequencer */
    fifo_cfg.FIFOSrc = FIFOSRC_DFT;
    fifo_cfg.FIFOThresh = 4;    /* DFT result. One pair for RCAL, another for Rz. One DFT result have real part and imaginary part */
    AD5940_FIFOCfg(&fifo_cfg);
    fifo_cfg.FIFOEn = bTRUE;
    AD5940_FIFOCfg(&fifo_cfg);
    
    /* Interrupt controller */
    AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);   /* Enable all interrupt in INTC1, so we can check INTC flags */
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE); 
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    /* Step4: Reconfigure GPIO */
    gpio_cfg.FuncSet = GP0_INT|GP1_SLEEP|GP2_SYNC;
    gpio_cfg.InputEnSet = 0;
    gpio_cfg.OutputEnSet = AGPIO_Pin0|AGPIO_Pin1|AGPIO_Pin2;
    gpio_cfg.OutVal = 0;
    gpio_cfg.PullEnSet = 0;
    AD5940_AGPIOCfg(&gpio_cfg);
    AD5940_SleepKeyCtrlS(SLPKEY_UNLOCK);  /* Allow AFE to enter sleep mode. */
    
    if(AD5940_WakeUp(10) > 10)  /* Wakeup AFE by read register, read 10 times at most */
        return AD5940ERR_WAKEUP;  /* Wakeup Failed */
    
    /* Configure sequencer and stop it */
    seq_cfg.SeqMemSize = SEQMEMSIZE_2KB;  /* 2kB SRAM is used for sequencer, others for data FIFO */
    seq_cfg.SeqBreakEn = bFALSE;
    seq_cfg.SeqIgnoreEn = bTRUE;
    seq_cfg.SeqCntCRCClr = bTRUE;
    seq_cfg.SeqEnable = bFALSE;
    seq_cfg.SeqWrTimer = 0;
    AD5940_SEQCfg(&seq_cfg);
    
    /* Reconfigure FIFO */
    AD5940_FIFOCtrlS(FIFOSRC_DFT, bFALSE);									/* Disable FIFO firstly */
    fifo_cfg.FIFOEn = bTRUE;
    fifo_cfg.FIFOMode = FIFOMODE_FIFO;
    fifo_cfg.FIFOSize = FIFOSIZE_4KB;                       /* 4kB for FIFO, The reset 2kB for sequencer */
    fifo_cfg.FIFOSrc = FIFOSRC_DFT;
    fifo_cfg.FIFOThresh = imp_cfg.FifoThresh;              /* DFT result. One pair for RCAL, another for Rz. One DFT result have real part and imaginary part */
    AD5940_FIFOCfg(&fifo_cfg);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    
    /* 执行ADC PGA校准 */
    error = AppADCPgaCal();
    if(error != AD5940ERR_OK) {
        LOG_E("ADC PGA calibration failed: %d", error);
        return error;
    }
    
    /* 执行高速DAC校准 */
    error = AppHSDACCal();
    if(error != AD5940ERR_OK) {
        LOG_E("HSDAC calibration failed: %d", error);
        return error;
    }
    
    /* Start sequence generator */
    /* Initialize sequencer generator */
    AD5940_SEQGenInit(AppBuff, APPBUFF_SIZE);

    /* Generate initialize sequence */
    error = AppIMPSeqCfgGen(); /* Application initialization sequence using either MCU or sequencer */
    if(error != AD5940ERR_OK)
        return error;

    /* Generate measurement sequence */
    error = AppIMPSeqMeasureGen();
    if(error != AD5940ERR_OK)
        return error;

    /* Initialization sequencer  */
    imp_cfg.InitSeqInfo.WriteSRAM = bFALSE;
    AD5940_SEQInfoCfg(&imp_cfg.InitSeqInfo);
    seq_cfg.SeqEnable = bTRUE;
    AD5940_SEQCfg(&seq_cfg);  /* Enable sequencer */
    AD5940_SEQMmrTrig(imp_cfg.InitSeqInfo.SeqId);
    while(AD5940_INTCTestFlag(AFEINTC_1, AFEINTSRC_ENDSEQ) == bFALSE);

    /* Measurement sequence  */
    imp_cfg.MeasureSeqInfo.WriteSRAM = bFALSE;
    AD5940_SEQInfoCfg(&imp_cfg.MeasureSeqInfo);

    seq_cfg.SeqEnable = bTRUE;
    AD5940_SEQCfg(&seq_cfg);  /* Enable sequencer, and wait for trigger */
    AD5940_ClrMCUIntFlag();   /* Clear interrupt flag generated before */

    AD5940_AFEPwrBW(imp_cfg.PwrMod, AFEBW_250KHZ);

    imp_cfg.IMPInited = bTRUE;  /* IMP application has been initialized. */
    return AD5940ERR_OK;
}

int32_t loop_imp_ctrl(uint32_t Command, void *pPara)
{
  switch (Command)
  {
    case IMPCTRL_START:
    {
      WUPTCfg_Type wupt_cfg;

      if(AD5940_WakeUp(10) > 10)  /* Wakeup AFE by read register, read 10 times at most */
        return AD5940ERR_WAKEUP;  /* Wakeup Failed */
      if(imp_cfg.IMPInited == bFALSE)
        return AD5940ERR_APPERROR;
      /* Start it */
      wupt_cfg.WuptEn = bTRUE;
      wupt_cfg.WuptEndSeq = WUPTENDSEQ_A;
      wupt_cfg.WuptOrder[0] = SEQID_0;
      wupt_cfg.SeqxSleepTime[SEQID_0] = 4;
      wupt_cfg.SeqxWakeupTime[SEQID_0] = (uint32_t)(imp_cfg.WuptClkFreq/imp_cfg.ImpODR)-4;
      AD5940_WUPTCfg(&wupt_cfg);
      
      imp_cfg.FifoDataCount = 0;  /* restart */
      break;
    }
    case IMPCTRL_STOPNOW:
    {
      if(AD5940_WakeUp(10) > 10)  /* Wakeup AFE by read register, read 10 times at most */
        return AD5940ERR_WAKEUP;  /* Wakeup Failed */
      /* Start Wupt right now */
      AD5940_WUPTCtrl(bFALSE);
      /* There is chance this operation will fail because sequencer could put AFE back 
        to hibernate mode just after waking up. Use STOPSYNC is better. */
      AD5940_WUPTCtrl(bFALSE);
      break;
    }
    case IMPCTRL_STOPSYNC:
    {
      imp_cfg.StopRequired = bTRUE;
      break;
    }
    case IMPCTRL_GETFREQ:
      {
        if(pPara == 0)
          return AD5940ERR_PARA;
        
        *(float*)pPara = imp_cfg.SinFreq;
      }
    break;
    case IMPCTRL_SHUTDOWN:
    {
      loop_imp_ctrl(IMPCTRL_STOPNOW, 0);  /* Stop the measurement if it's running. */
      /* Turn off LPloop related blocks which are not controlled automatically by hibernate operation */
      AFERefCfg_Type aferef_cfg;
      LPLoopCfg_Type lp_loop;
      memset(&aferef_cfg, 0, sizeof(aferef_cfg));
      AD5940_REFCfgS(&aferef_cfg);
      memset(&lp_loop, 0, sizeof(lp_loop));
      AD5940_LPLoopCfgS(&lp_loop);
      AD5940_EnterSleepS();  /* Enter Hibernate */
    }
    break;
    default:
    break;
  }
  return AD5940ERR_OK;
}

/* generated code snnipet */
float AppIMPGetCurrFreq(void)
{
    return imp_cfg.SinFreq;
}

/* Application initialization */
static AD5940Err AppIMPSeqCfgGen(void)
{
    AD5940Err error = AD5940ERR_OK;
    const uint32_t *pSeqCmd;
    uint32_t SeqLen;
    AFERefCfg_Type aferef_cfg;
    HSLoopCfg_Type HsLoopCfg;
    DSPCfg_Type dsp_cfg;
    float sin_freq;

    /* Start sequence generator here */
    AD5940_SEQGenCtrl(bTRUE);

    AD5940_AFECtrlS(AFECTRL_ALL, bFALSE);  /* Init all to disable state */

    aferef_cfg.HpBandgapEn = bTRUE;
    aferef_cfg.Hp1V1BuffEn = bTRUE;
    aferef_cfg.Hp1V8BuffEn = bTRUE;
    aferef_cfg.Disc1V1Cap = bFALSE;
    aferef_cfg.Disc1V8Cap = bFALSE;
    aferef_cfg.Hp1V8ThemBuff = bFALSE;
    aferef_cfg.Hp1V8Ilimit = bFALSE;
    aferef_cfg.Lp1V1BuffEn = bFALSE;
    aferef_cfg.Lp1V8BuffEn = bFALSE;
    /* LP reference control - turn off them to save power*/
    if(imp_cfg.BiasVolt != 0.0f)    /* With bias voltage */
    {
        aferef_cfg.LpBandgapEn = bTRUE;
        aferef_cfg.LpRefBufEn = bTRUE;
    }
    else
    {
        aferef_cfg.LpBandgapEn = bFALSE;
        aferef_cfg.LpRefBufEn = bFALSE;
    }
    aferef_cfg.LpRefBoostEn = bFALSE;
    AD5940_REFCfgS(&aferef_cfg);	
    HsLoopCfg.HsDacCfg.ExcitBufGain = imp_cfg.ExcitBufGain;
    HsLoopCfg.HsDacCfg.HsDacGain = imp_cfg.HsDacGain;
    HsLoopCfg.HsDacCfg.HsDacUpdateRate = imp_cfg.HsDacUpdateRate;

    HsLoopCfg.HsTiaCfg.DiodeClose = bFALSE;
    if(imp_cfg.BiasVolt != 0.0f)    /* With bias voltage */
        HsLoopCfg.HsTiaCfg.HstiaBias = HSTIABIAS_VZERO0;
    else
        HsLoopCfg.HsTiaCfg.HstiaBias = HSTIABIAS_1P1;
    HsLoopCfg.HsTiaCfg.HstiaCtia = 31; /* 31pF + 2pF */
    HsLoopCfg.HsTiaCfg.HstiaDeRload = HSTIADERLOAD_OPEN;
    HsLoopCfg.HsTiaCfg.HstiaDeRtia = HSTIADERTIA_OPEN;
    HsLoopCfg.HsTiaCfg.HstiaRtiaSel = imp_cfg.HstiaRtiaSel;

    HsLoopCfg.SWMatCfg.Dswitch = imp_cfg.DswitchSel;
    HsLoopCfg.SWMatCfg.Pswitch = imp_cfg.PswitchSel;
    HsLoopCfg.SWMatCfg.Nswitch = imp_cfg.NswitchSel;
    HsLoopCfg.SWMatCfg.Tswitch = SWT_TRTIA|imp_cfg.TswitchSel;

    HsLoopCfg.WgCfg.WgType = WGTYPE_SIN;
    HsLoopCfg.WgCfg.GainCalEn = bTRUE;
    HsLoopCfg.WgCfg.OffsetCalEn = bTRUE;
    sin_freq = imp_cfg.SinFreq;
    imp_cfg.FreqofData = sin_freq;
    HsLoopCfg.WgCfg.SinCfg.SinFreqWord = AD5940_WGFreqWordCal(sin_freq, imp_cfg.SysClkFreq);
    HsLoopCfg.WgCfg.SinCfg.SinAmplitudeWord = (uint32_t)(imp_cfg.DacVoltPP/800.0f*2047 + 0.5f);
    HsLoopCfg.WgCfg.SinCfg.SinOffsetWord = 0;
    HsLoopCfg.WgCfg.SinCfg.SinPhaseWord = 0;
    AD5940_HSLoopCfgS(&HsLoopCfg);
    if(imp_cfg.BiasVolt != 0.0f)    /* With bias voltage */
    {
        LPDACCfg_Type lpdac_cfg;

        lpdac_cfg.LpdacSel = LPDAC0;
        lpdac_cfg.LpDacVbiasMux = LPDACVBIAS_12BIT; /* Use Vbias to tuning BiasVolt. */
        lpdac_cfg.LpDacVzeroMux = LPDACVZERO_6BIT;  /* Vbias-Vzero = BiasVolt */
        lpdac_cfg.DacData6Bit = 0x40>>1;            /* Set Vzero to middle scale. */
        if(imp_cfg.BiasVolt<-1100.0f) imp_cfg.BiasVolt = -1100.0f + DAC12BITVOLT_1LSB;
        if(imp_cfg.BiasVolt> 1100.0f) imp_cfg.BiasVolt = 1100.0f - DAC12BITVOLT_1LSB;
        lpdac_cfg.DacData12Bit = (uint32_t)((imp_cfg.BiasVolt + 1100.0f)/DAC12BITVOLT_1LSB);
        lpdac_cfg.DataRst = bFALSE;      /* Do not reset data register */
        lpdac_cfg.LpDacSW = LPDACSW_VBIAS2LPPA|LPDACSW_VBIAS2PIN|LPDACSW_VZERO2LPTIA|LPDACSW_VZERO2PIN|LPDACSW_VZERO2HSTIA;
        lpdac_cfg.LpDacRef = LPDACREF_2P5;
        lpdac_cfg.LpDacSrc = LPDACSRC_MMR;      /* Use MMR data, we use LPDAC to generate bias voltage for LPTIA - the Vzero */
        lpdac_cfg.PowerEn = bTRUE;              /* Power up LPDAC */
        AD5940_LPDACCfgS(&lpdac_cfg);
    }
    dsp_cfg.ADCBaseCfg.ADCMuxN = ADCMUXN_HSTIA_N;
    dsp_cfg.ADCBaseCfg.ADCMuxP = ADCMUXP_HSTIA_P;
    dsp_cfg.ADCBaseCfg.ADCPga = imp_cfg.AdcPgaGain;

    memset(&dsp_cfg.ADCDigCompCfg, 0, sizeof(dsp_cfg.ADCDigCompCfg));

    dsp_cfg.ADCFilterCfg.ADCAvgNum = imp_cfg.ADCAvgNum;
    dsp_cfg.ADCFilterCfg.ADCRate = ADCRATE_800KHZ;	/* Tell filter block clock rate of ADC*/
    dsp_cfg.ADCFilterCfg.ADCSinc2Osr = imp_cfg.ADCSinc2Osr;
    dsp_cfg.ADCFilterCfg.ADCSinc3Osr = imp_cfg.ADCSinc3Osr;
    dsp_cfg.ADCFilterCfg.BpNotch = bTRUE;
    dsp_cfg.ADCFilterCfg.BpSinc3 = bFALSE;
    dsp_cfg.ADCFilterCfg.Sinc2NotchEnable = bTRUE;
    dsp_cfg.DftCfg.DftNum = imp_cfg.DftNum;
    dsp_cfg.DftCfg.DftSrc = imp_cfg.DftSrc;
    dsp_cfg.DftCfg.HanWinEn = imp_cfg.HanWinEn;

    memset(&dsp_cfg.StatCfg, 0, sizeof(dsp_cfg.StatCfg));
    AD5940_DSPCfgS(&dsp_cfg);

    /* Enable all of them. They are automatically turned off during hibernate mode to save power */
    if(imp_cfg.BiasVolt == 0.0f)
        AD5940_AFECtrlS(AFECTRL_HSTIAPWR|AFECTRL_INAMPPWR|AFECTRL_EXTBUFPWR|\
                        AFECTRL_WG|AFECTRL_DACREFPWR|AFECTRL_HSDACPWR|\
                        AFECTRL_SINC2NOTCH, bTRUE);
    else
        AD5940_AFECtrlS(AFECTRL_HSTIAPWR|AFECTRL_INAMPPWR|AFECTRL_EXTBUFPWR|\
                        AFECTRL_WG|AFECTRL_DACREFPWR|AFECTRL_HSDACPWR|\
                        AFECTRL_SINC2NOTCH|AFECTRL_DCBUFPWR, bTRUE);
    /* Sequence end. */
    AD5940_SEQGenInsert(SEQ_STOP()); /* Add one extra command to disable sequencer for initialization sequence because we only want it to run one time. */

    /* Stop here */
    error = AD5940_SEQGenFetchSeq(&pSeqCmd, &SeqLen);
    AD5940_SEQGenCtrl(bFALSE); /* Stop sequencer generator */
    if(error == AD5940ERR_OK)
    {
        imp_cfg.InitSeqInfo.SeqId = SEQID_1;
        imp_cfg.InitSeqInfo.SeqRamAddr = imp_cfg.SeqStartAddr;
        imp_cfg.InitSeqInfo.pSeqCmd = pSeqCmd;
        imp_cfg.InitSeqInfo.SeqLen = SeqLen;
        /* Write command to SRAM */
        AD5940_SEQCmdWrite(imp_cfg.InitSeqInfo.SeqRamAddr, pSeqCmd, SeqLen);
    }
    else
        return error; /* Error */
    
    return AD5940ERR_OK;
}


static AD5940Err AppIMPSeqMeasureGen(void)
{
    AD5940Err error = AD5940ERR_OK;
    const uint32_t *pSeqCmd;
    uint32_t SeqLen;

    uint32_t WaitClks;
    SWMatrixCfg_Type sw_cfg;
    ClksCalInfo_Type clks_cal;

    clks_cal.DataType = DATATYPE_DFT;
    clks_cal.DftSrc = imp_cfg.DftSrc;
    clks_cal.DataCount = 1L<<(imp_cfg.DftNum+2); /* 2^(DFTNUMBER+2) */
    clks_cal.ADCSinc2Osr = imp_cfg.ADCSinc2Osr;
    clks_cal.ADCSinc3Osr = imp_cfg.ADCSinc3Osr;
    clks_cal.ADCAvgNum = imp_cfg.ADCAvgNum;
    clks_cal.RatioSys2AdcClk = imp_cfg.SysClkFreq/imp_cfg.AdcClkFreq;
    AD5940_ClksCalculate(&clks_cal, &WaitClks);

    AD5940_SEQGenCtrl(bTRUE);
    AD5940_SEQGpioCtrlS(AGPIO_Pin2); /* Set GPIO1, clear others that under control */
    AD5940_SEQGenInsert(SEQ_WAIT(16*250));  /* @todo wait 250us? */
    sw_cfg.Dswitch = SWD_RCAL0;
    sw_cfg.Pswitch = SWP_RCAL0;
    sw_cfg.Nswitch = SWN_RCAL1;
    sw_cfg.Tswitch = SWT_RCAL1|SWT_TRTIA;
    AD5940_SWMatrixCfgS(&sw_cfg);
	AD5940_AFECtrlS(AFECTRL_HSTIAPWR|AFECTRL_INAMPPWR|AFECTRL_EXTBUFPWR|\
                    AFECTRL_WG|AFECTRL_DACREFPWR|AFECTRL_HSDACPWR|\
                    AFECTRL_SINC2NOTCH, bTRUE);
    AD5940_AFECtrlS(AFECTRL_WG|AFECTRL_ADCPWR, bTRUE);  /* Enable Waveform generator */
    //delay for signal settling DFT_WAIT
    AD5940_SEQGenInsert(SEQ_WAIT(16*10));
    AD5940_AFECtrlS(AFECTRL_ADCCNV|AFECTRL_DFT, bTRUE);  /* Start ADC convert and DFT */
    AD5940_SEQGenInsert(SEQ_WAIT(WaitClks));
    //wait for first data ready
    AD5940_AFECtrlS(AFECTRL_ADCPWR|AFECTRL_ADCCNV|AFECTRL_DFT|AFECTRL_WG, bFALSE);  /* Stop ADC convert and DFT */

    /* Configure matrix for external Rz */
    sw_cfg.Dswitch = imp_cfg.DswitchSel;
    sw_cfg.Pswitch = imp_cfg.PswitchSel;
    sw_cfg.Nswitch = imp_cfg.NswitchSel;
    sw_cfg.Tswitch = SWT_TRTIA|imp_cfg.TswitchSel;
    AD5940_SWMatrixCfgS(&sw_cfg);
    AD5940_AFECtrlS(AFECTRL_ADCPWR|AFECTRL_WG, bTRUE);  /* Enable Waveform generator */
    AD5940_SEQGenInsert(SEQ_WAIT(16*10));  //delay for signal settling DFT_WAIT
    AD5940_AFECtrlS(AFECTRL_ADCCNV|AFECTRL_DFT, bTRUE);  /* Start ADC convert and DFT */
    AD5940_SEQGenInsert(SEQ_WAIT(WaitClks));  /* wait for first data ready */
    AD5940_AFECtrlS(AFECTRL_ADCCNV|AFECTRL_DFT|AFECTRL_WG|AFECTRL_ADCPWR, bFALSE);  /* Stop ADC convert and DFT */
    AD5940_AFECtrlS(AFECTRL_HSTIAPWR|AFECTRL_INAMPPWR|AFECTRL_EXTBUFPWR|\
                AFECTRL_WG|AFECTRL_DACREFPWR|AFECTRL_HSDACPWR|\
                AFECTRL_SINC2NOTCH, bFALSE);
    AD5940_SEQGpioCtrlS(0); /* Clr GPIO1 */

    AD5940_EnterSleepS();/* Goto hibernate */

    /* Sequence end. */
    error = AD5940_SEQGenFetchSeq(&pSeqCmd, &SeqLen);
    AD5940_SEQGenCtrl(bFALSE); /* Stop sequencer generator */

    if(error == AD5940ERR_OK)
    {
        imp_cfg.MeasureSeqInfo.SeqId = SEQID_0;
        imp_cfg.MeasureSeqInfo.SeqRamAddr = imp_cfg.InitSeqInfo.SeqRamAddr + imp_cfg.InitSeqInfo.SeqLen ;
        imp_cfg.MeasureSeqInfo.pSeqCmd = pSeqCmd;
        imp_cfg.MeasureSeqInfo.SeqLen = SeqLen;
        /* Write command to SRAM */
        AD5940_SEQCmdWrite(imp_cfg.MeasureSeqInfo.SeqRamAddr, pSeqCmd, SeqLen);
    }
    else
        return error; /* Error */
    
    return AD5940ERR_OK;
}

static AD5940Err AppADCPgaCal(void)
{
    ADCPGACal_Type pga_cal;

    /* Calibrate ADC PGA(offset and gain) */
    pga_cal.AdcClkFreq = imp_cfg.AdcClkFreq;
    pga_cal.SysClkFreq = imp_cfg.SysClkFreq;
    pga_cal.ADCPga = ADCPGA_1;
    pga_cal.ADCSinc2Osr = ADCSINC2OSR_1333;	/* 800kSPS/4/1333 = 150Hz,  T = 6.67ms*/
    pga_cal.ADCSinc3Osr = ADCSINC3OSR_4;
    pga_cal.TimeOut10us = 10*100;			/* 10ms max */
    pga_cal.VRef1p82 = 1.82f;  
    pga_cal.VRef1p11  = 1.0979f;
    pga_cal.PGACalType = PGACALTYPE_OFFSETGAIN; /* Calibrate Offset and Gain errors */
    AD5940_ADCPGACal(&pga_cal);
    /* Calibrate Offset and Gain for PGA = 1.5 */
    pga_cal.ADCPga = ADCPGA_1P5;
    AD5940_ADCPGACal(&pga_cal);
    /* Calibrate Offset and Gain for PGA = 2 */
    pga_cal.ADCPga = ADCPGA_2;
    AD5940_ADCPGACal(&pga_cal);
    /* Calibrate Offset and Gain for PGA = 4 */
    pga_cal.ADCPga = ADCPGA_4;
    AD5940_ADCPGACal(&pga_cal);
    /* Calibrate Offset and Gain for PGA = 9 */
    pga_cal.ADCPga = ADCPGA_9;
    AD5940_ADCPGACal(&pga_cal);
    return AD5940ERR_OK;
}

static AD5940Err AppHSDACCal(void)
{
    HSDACCal_Type hsdac_cal;
    
    hsdac_cal.ExcitBufGain = EXCITBUFGAIN_2;	/**< Select from  EXCITBUFGAIN_2, EXCITBUFGAIN_0P25 */ 
    hsdac_cal.HsDacGain = HSDACGAIN_1; 				/**< Select from  HSDACGAIN_1, HSDACGAIN_0P2 */ 
    hsdac_cal.AfePwrMode = AFEPWR_LP;
    hsdac_cal.ADCSinc2Osr = ADCSINC2OSR_1333;
    hsdac_cal.ADCSinc3Osr = ADCSINC3OSR_4;
    AD5940_HSDACCal(&hsdac_cal);
    
    return AD5940ERR_OK;
}

//static AD5940Err AppRtiaCal(void)
//{
//   HSRTIACal_Type hsrtia_cal;
//   hsrtia_cal.AdcClkFreq = imp_cfg.AdcClkFreq;
//   hsrtia_cal.ADCSinc2Osr = imp_cfg.ADCSinc2Osr;
//   hsrtia_cal.ADCSinc3Osr = imp_cfg.ADCSinc3Osr;
//   hsrtia_cal.bPolarResult = bTRUE; /* We need magnitude and phase here */
//   hsrtia_cal.DftCfg.DftNum = imp_cfg.DftNum;
//   hsrtia_cal.DftCfg.DftSrc = imp_cfg.DftSrc;
//   hsrtia_cal.DftCfg.HanWinEn = imp_cfg.HanWinEn;
//   hsrtia_cal.fRcal= imp_cfg.RcalVal;
//   hsrtia_cal.HsTiaCfg.DiodeClose = bFALSE;
//   hsrtia_cal.HsTiaCfg.HstiaBias = HSTIABIAS_1P1;
//   hsrtia_cal.HsTiaCfg.HstiaCtia = imp_cfg.CtiaSel;
//   hsrtia_cal.HsTiaCfg.HstiaDeRload = HSTIADERLOAD_OPEN;
//   hsrtia_cal.HsTiaCfg.HstiaDeRtia = HSTIADERTIA_TODE;
//   hsrtia_cal.HsTiaCfg.HstiaRtiaSel = imp_cfg.HstiaRtiaSel;
//   hsrtia_cal.SysClkFreq = imp_cfg.SysClkFreq;
//   if(imp_cfg.SweepCfg.SweepEn == bTRUE)
//   {
//      uint32_t i;
//      imp_cfg.SweepCfg.SweepIndex = 0;  /* Reset index */
//      for(i=0;i<imp_cfg.SweepCfg.SweepPoints;i++)
//      {
//         AD5940_SweepNext(&imp_cfg.SweepCfg, &hsrtia_cal.fFreq);
//         AD5940_HSRtiaCal(&hsrtia_cal, imp_cfg.RtiaCalTable[i]);
//         printf("Freq:%.2f,Mag:%.2f,Phase:%fDegree\n", hsrtia_cal.fFreq, imp_cfg.RtiaCalTable[i][0], 
//         imp_cfg.RtiaCalTable[i][1]*180/MATH_PI);
//       }
//       imp_cfg.RtiaCurrValue[imp_cfg.SweepCfg.SweepIndex] = imp_cfg.RtiaCalTable[i][0];
//       imp_cfg.RtiaCurrValue[imp_cfg.SweepCfg.SweepIndex] = imp_cfg.RtiaCalTable[i][0];
//       imp_cfg.SweepCfg.SweepIndex = 0;  /* Reset index */
//   }
//   else
//   {
//      hsrtia_cal.fFreq = imp_cfg.SinFreq;
//      AD5940_HSRtiaCal(&hsrtia_cal, imp_cfg.RtiaCurrValue);
//      printf("RtiaMag:%.2f,Phase:%fDegree\n", imp_cfg.RtiaCurrValue[0], 
//      imp_cfg.RtiaCurrValue[1]*180/MATH_PI);
//   }
//   return AD5940ERR_OK;
//}


/* Modify registers when AFE wakeup */
int32_t AppIMPRegModify(int32_t * const pData, uint32_t *pDataCount)
{
    if(imp_cfg.NumOfData > 0)
    {
        imp_cfg.FifoDataCount += *pDataCount/4;
        if(imp_cfg.FifoDataCount >= imp_cfg.NumOfData)
        {
            AD5940_WUPTCtrl(bFALSE);
            return AD5940ERR_OK;
        }
    }
    if(imp_cfg.StopRequired == bTRUE)
    {
        AD5940_WUPTCtrl(bFALSE);
        return AD5940ERR_OK;
    }
    return AD5940ERR_OK;
}

/**
 * @brief  从笛卡尔坐标（实部/虚部）计算极坐标（幅度/相位）
 * @param  
 * @retval 
 * @note   
 */
static void calc_polar_from_cartesian(const iImpCar_Type *pCart, float *pMag, float *pPhase)
{
    float real = (float)pCart->Real;
    float imag = (float)pCart->Image;
    *pMag = sqrtf(real * real + imag * imag);
    *pPhase = atan2f(-imag, real);
}

/**
 * @brief  
 * @param  
 * @retval 
 * @note   
 */
int32_t loop_imp_data_process(int32_t * const pData, uint32_t *pDataCount)
{
    uint32_t DataCount = *pDataCount;
    uint32_t ImpResCount = DataCount / 4;
    fImpPol_Type * const pOut = (fImpPol_Type*)pData;
    iImpCar_Type * pSrcData = (iImpCar_Type*)pData;

    *pDataCount = 0;
    DataCount = (DataCount / 4) * 4;

    /* Convert DFT result to int32_t type */
    convert_to_int32(pData, DataCount);
    
    for(uint32_t i = 0; i < ImpResCount; i++)
    {
        iImpCar_Type *pDftRcal = pSrcData++;
        iImpCar_Type *pDftRz = pSrcData++;
        float RcalMag, RcalPhase;
        float RzMag, RzPhase;
        
        calc_polar_from_cartesian(pDftRcal, &RcalMag, &RcalPhase);
        calc_polar_from_cartesian(pDftRz, &RzMag, &RzPhase);
        
        pOut[i].Magnitude = RcalMag / RzMag * imp_cfg.RcalVal;
        pOut[i].Phase = RcalPhase - RzPhase;
//        LOG_D("RzMag: %f Ohm , RzPhase: %f", pOut[i].Magnitude, pOut[i].Phase*180/MATH_PI);
    }
    
    *pDataCount = ImpResCount;
    return 0;
}


int32_t loop_imp_isr(void *pBuff, uint32_t *pCount)
{
    uint32_t BuffCount;
    uint32_t FifoCnt;
    BuffCount = *pCount;
    
    *pCount = 0;
    
    if(AD5940_WakeUp(10) > 10)          /* Wakeup AFE by read register, read 10 times at most */
        return AD5940ERR_WAKEUP;        /* Wakeup Failed */
    AD5940_SleepKeyCtrlS(SLPKEY_LOCK);  /* Prohibit AFE to enter sleep mode. */

    if(AD5940_INTCTestFlag(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH) == bTRUE)
    {
        /* Now there should be 4 data in FIFO */
        FifoCnt = (AD5940_FIFOGetCnt() / 4) * 4;
        
        if(FifoCnt > BuffCount)
        {
            ///@todo buffer is limited.
        }
        AD5940_FIFORd((uint32_t *)pBuff, FifoCnt);
        AD5940_INTCClrFlag(AFEINTSRC_DATAFIFOTHRESH);
        AppIMPRegModify(pBuff, &FifoCnt);   /* If there is need to do AFE re-configure, do it here when AFE is in active state */
        AD5940_SleepKeyCtrlS(SLPKEY_UNLOCK);  /* Allow AFE to enter sleep mode. */
        /* Process data */ 
        loop_imp_data_process((int32_t*)pBuff,&FifoCnt); 
        *pCount = FifoCnt;
        return 0;
    }
    
    return 0;
}

void loop_imp_task(void)
{
    uint32_t temp = 0U;
    
    if (port_ctrl_get_mode() == PORT_MODE_LOOP_IMP) {
        
        if(AD5940_GetMCUIntFlag()) {
            AD5940_ClrMCUIntFlag();
            temp = APPBUFF_SIZE;
            if (loop_imp_isr(AppBuff, &temp) != 0) {
                temp = 0U; 
            }
        }
        if (temp > 0U) {
            fImpPol_Type *pImp = (fImpPol_Type*)AppBuff;
            uint32_t imp_count = temp;
            const uint32_t max_imp_count = (APPBUFF_SIZE * sizeof(uint32_t)) / sizeof(fImpPol_Type);
            
            if ((imp_count > 0U) && (imp_count <= max_imp_count)) {
                float raw_mag = pImp[imp_count - 1U].Magnitude;
                float raw_phase = pImp[imp_count - 1U].Phase;
                float cal_phase = raw_phase - SYSTEM_PHASE_OFFSET*MATH_PI/180;
                float z_real_total = raw_mag * cosf(cal_phase);
                float z_imag_total = raw_mag * sinf(cal_phase);
                float z_real_load = z_real_total - R_BASE_OFFSET;
                float vector_res_final = 0.0f;
                if (z_real_load > 1.0f) {
                    vector_res_final = z_real_load + (pow(z_imag_total,2) / z_real_load );
                } else if (z_real_load > -50.0f) {
                    vector_res_final = 0.0f; 
                }
                g_latest_imp_real = vector_res_final;
            }
        }
    }
}

/**
 * @brief 获取最新的回路阻抗实部数据
 * @return 最新的阻抗实部数据（幅度值，单位：Ω）
 */
float loop_imp_get_data(void)
{
    return g_latest_imp_real;
}

/* Private functions ---------------------------------------------------------*/
static void convert_to_int32(int32_t *pData, uint32_t DataCount)
{
    for(uint32_t i = 0; i < DataCount; i++)
    {
        pData[i] &= 0x3ffff;        /* 提取低18位 */
        if( pData[i] & (1L << 17) ) /* Bit17是符号位 */
        {
            pData[i] |= 0xfffc0000; /* 18位补码，bit17是符号位 */
        }
    }
}