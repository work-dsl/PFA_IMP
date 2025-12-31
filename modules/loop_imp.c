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

#define  LOG_TAG             "loop_imp"
#define  LOG_LVL             4
#include "log.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/
/* Default LPDAC resolution(2.5V internal reference). */
#define DAC12BITVOLT_1LSB   (2200.0f/4095)  /* mV */
#define DAC6BITVOLT_1LSB    (DAC12BITVOLT_1LSB*64)  /* mV */

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

/* Exported variables  -------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
static AD5940Err AppIMPSeqCfgGen(void);
static AD5940Err AppIMPSeqMeasureGen(void);
static int32_t AppIMPRegModify(int32_t * const pData, uint32_t *pDataCount);
static int32_t AppIMPDataProcess(int32_t * const pData, uint32_t *pDataCount);

/* Exported functions --------------------------------------------------------*/
/* 
    Application configuration structure. Specified by user from template.
    The variables are usable in this whole application.
    It includes basic configuration for sequencer generator and application related parameters
*/
AppIMPCfg_Type AppIMPCfg = 
{
    .bParaChanged = bFALSE,
    .SeqStartAddr = 0,
    .MaxSeqLen = 0,
    
    .SeqStartAddrCal = 0,
    .MaxSeqLenCal = 0,

    .ImpODR = 1,
    .NumOfData = -1,
    .SysClkFreq = 16000000.0,
    .WuptClkFreq = 32000.0,
    .AdcClkFreq = 16000000.0,
    .RcalVal = 10000.0,

    .DswitchSel = SWD_CE0,
    .PswitchSel = SWP_RE0,
    .NswitchSel = SWN_SE0,
    .TswitchSel = SWT_SE0LOAD,

    .PwrMod = AFEPWR_HP,

    .HstiaRtiaSel = HSTIARTIA_1K,
    .ExcitBufGain = EXCITBUFGAIN_2,
    .HsDacGain = HSDACGAIN_1,
    .HsDacUpdateRate = 7,
    .DacVoltPP = 500.0,
    .BiasVolt = -0.0f,

    .SinFreq = 50000.0, /* 50000Hz */

    .DftNum = DFTNUM_16384,
    .DftSrc = DFTSRC_SINC3,
    .HanWinEn = bTRUE,

    .AdcPgaGain = ADCPGA_1,
    .ADCSinc3Osr = ADCSINC3OSR_2,
    .ADCSinc2Osr = ADCSINC2OSR_22,

    .ADCAvgNum = ADCAVGNUM_16,

    .SweepCfg.SweepEn = bFALSE,
    .SweepCfg.SweepStart = 1000,
    .SweepCfg.SweepStop = 100000.0,
    .SweepCfg.SweepPoints = 101,
    .SweepCfg.SweepLog = bFALSE,
    .SweepCfg.SweepIndex = 0,

    .FifoThresh = 4,
    .IMPInited = bFALSE,
    .StopRequired = bFALSE,
};

/**
    This function is provided for upper controllers that want to change 
    application parameters specially for user defined parameters.
*/
int32_t AppIMPGetCfg(void *pCfg)
{
    if(pCfg)
    {
        *(AppIMPCfg_Type**)pCfg = &AppIMPCfg;
        return AD5940ERR_OK;
    }
    return AD5940ERR_PARA;
}

int32_t AppIMPCtrl(uint32_t Command, void *pPara)
{
    switch (Command)
    {
        case IMPCTRL_START:
        {
            WUPTCfg_Type wupt_cfg;

            if(AD5940_WakeUp(10) > 10)  /* Wakeup AFE by read register, read 10 times at most */
                return AD5940ERR_WAKEUP;  /* Wakeup Failed */
            if(AppIMPCfg.IMPInited == bFALSE)
                return AD5940ERR_APPERROR;
            /* Start it */
            wupt_cfg.WuptEn = bTRUE;
            wupt_cfg.WuptEndSeq = WUPTENDSEQ_A;
            wupt_cfg.WuptOrder[0] = SEQID_0;
            wupt_cfg.SeqxSleepTime[SEQID_0] = 4;
            wupt_cfg.SeqxWakeupTime[SEQID_0] = (uint32_t)(AppIMPCfg.WuptClkFreq/AppIMPCfg.ImpODR)-4;
            AD5940_WUPTCfg(&wupt_cfg);
            
            AppIMPCfg.FifoDataCount = 0;  /* restart */
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
            AppIMPCfg.StopRequired = bTRUE;
            break;
        }
        case IMPCTRL_GETFREQ:
            {
                if(pPara == 0)
                    return AD5940ERR_PARA;
                if(AppIMPCfg.SweepCfg.SweepEn == bTRUE)
                    *(float*)pPara = AppIMPCfg.FreqofData;
                else
                    *(float*)pPara = AppIMPCfg.SinFreq;
            }
        break;
        case IMPCTRL_SHUTDOWN:
        {
            AppIMPCtrl(IMPCTRL_STOPNOW, 0);  /* Stop the measurement if it's running. */
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
    if(AppIMPCfg.SweepCfg.SweepEn == bTRUE)
        return AppIMPCfg.FreqofData;
    else
        return AppIMPCfg.SinFreq;
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
    if(AppIMPCfg.BiasVolt != 0.0f)    /* With bias voltage */
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
    HsLoopCfg.HsDacCfg.ExcitBufGain = AppIMPCfg.ExcitBufGain;
    HsLoopCfg.HsDacCfg.HsDacGain = AppIMPCfg.HsDacGain;
    HsLoopCfg.HsDacCfg.HsDacUpdateRate = AppIMPCfg.HsDacUpdateRate;

    HsLoopCfg.HsTiaCfg.DiodeClose = bFALSE;
	if(AppIMPCfg.BiasVolt != 0.0f)    /* With bias voltage */
		HsLoopCfg.HsTiaCfg.HstiaBias = HSTIABIAS_VZERO0;
	else
		HsLoopCfg.HsTiaCfg.HstiaBias = HSTIABIAS_1P1;
    HsLoopCfg.HsTiaCfg.HstiaCtia = 31; /* 31pF + 2pF */
    HsLoopCfg.HsTiaCfg.HstiaDeRload = HSTIADERLOAD_OPEN;
    HsLoopCfg.HsTiaCfg.HstiaDeRtia = HSTIADERTIA_OPEN;
    HsLoopCfg.HsTiaCfg.HstiaRtiaSel = AppIMPCfg.HstiaRtiaSel;

    HsLoopCfg.SWMatCfg.Dswitch = AppIMPCfg.DswitchSel;
    HsLoopCfg.SWMatCfg.Pswitch = AppIMPCfg.PswitchSel;
    HsLoopCfg.SWMatCfg.Nswitch = AppIMPCfg.NswitchSel;
    HsLoopCfg.SWMatCfg.Tswitch = SWT_TRTIA|AppIMPCfg.TswitchSel;

    HsLoopCfg.WgCfg.WgType = WGTYPE_SIN;
    HsLoopCfg.WgCfg.GainCalEn = bTRUE;
    HsLoopCfg.WgCfg.OffsetCalEn = bTRUE;
    if(AppIMPCfg.SweepCfg.SweepEn == bTRUE)
    {
        AppIMPCfg.FreqofData = AppIMPCfg.SweepCfg.SweepStart;
        AppIMPCfg.SweepCurrFreq = AppIMPCfg.SweepCfg.SweepStart;
        AD5940_SweepNext(&AppIMPCfg.SweepCfg, &AppIMPCfg.SweepNextFreq);
        sin_freq = AppIMPCfg.SweepCurrFreq;
    }
    else
    {
        sin_freq = AppIMPCfg.SinFreq;
        AppIMPCfg.FreqofData = sin_freq;
    }
    HsLoopCfg.WgCfg.SinCfg.SinFreqWord = AD5940_WGFreqWordCal(sin_freq, AppIMPCfg.SysClkFreq);
    HsLoopCfg.WgCfg.SinCfg.SinAmplitudeWord = (uint32_t)(AppIMPCfg.DacVoltPP/800.0f*2047 + 0.5f);
    HsLoopCfg.WgCfg.SinCfg.SinOffsetWord = 0;
    HsLoopCfg.WgCfg.SinCfg.SinPhaseWord = 0;
    AD5940_HSLoopCfgS(&HsLoopCfg);
    if(AppIMPCfg.BiasVolt != 0.0f)    /* With bias voltage */
    {
        LPDACCfg_Type lpdac_cfg;
        
        lpdac_cfg.LpdacSel = LPDAC0;
        lpdac_cfg.LpDacVbiasMux = LPDACVBIAS_12BIT; /* Use Vbias to tuning BiasVolt. */
        lpdac_cfg.LpDacVzeroMux = LPDACVZERO_6BIT;  /* Vbias-Vzero = BiasVolt */
        lpdac_cfg.DacData6Bit = 0x40>>1;            /* Set Vzero to middle scale. */
        if(AppIMPCfg.BiasVolt<-1100.0f) AppIMPCfg.BiasVolt = -1100.0f + DAC12BITVOLT_1LSB;
        if(AppIMPCfg.BiasVolt> 1100.0f) AppIMPCfg.BiasVolt = 1100.0f - DAC12BITVOLT_1LSB;
        lpdac_cfg.DacData12Bit = (uint32_t)((AppIMPCfg.BiasVolt + 1100.0f)/DAC12BITVOLT_1LSB);
        lpdac_cfg.DataRst = bFALSE;      /* Do not reset data register */
        lpdac_cfg.LpDacSW = LPDACSW_VBIAS2LPPA|LPDACSW_VBIAS2PIN|LPDACSW_VZERO2LPTIA|LPDACSW_VZERO2PIN|LPDACSW_VZERO2HSTIA;
        lpdac_cfg.LpDacRef = LPDACREF_2P5;
        lpdac_cfg.LpDacSrc = LPDACSRC_MMR;      /* Use MMR data, we use LPDAC to generate bias voltage for LPTIA - the Vzero */
        lpdac_cfg.PowerEn = bTRUE;              /* Power up LPDAC */
        AD5940_LPDACCfgS(&lpdac_cfg);
    }
    dsp_cfg.ADCBaseCfg.ADCMuxN = ADCMUXN_HSTIA_N;
    dsp_cfg.ADCBaseCfg.ADCMuxP = ADCMUXP_HSTIA_P;
    dsp_cfg.ADCBaseCfg.ADCPga = AppIMPCfg.AdcPgaGain;
    
    memset(&dsp_cfg.ADCDigCompCfg, 0, sizeof(dsp_cfg.ADCDigCompCfg));
    
    dsp_cfg.ADCFilterCfg.ADCAvgNum = AppIMPCfg.ADCAvgNum;
    dsp_cfg.ADCFilterCfg.ADCRate = ADCRATE_800KHZ;	/* Tell filter block clock rate of ADC*/
    dsp_cfg.ADCFilterCfg.ADCSinc2Osr = AppIMPCfg.ADCSinc2Osr;
    dsp_cfg.ADCFilterCfg.ADCSinc3Osr = AppIMPCfg.ADCSinc3Osr;
    dsp_cfg.ADCFilterCfg.BpNotch = bTRUE;
    dsp_cfg.ADCFilterCfg.BpSinc3 = bFALSE;
    dsp_cfg.ADCFilterCfg.Sinc2NotchEnable = bTRUE;
    dsp_cfg.DftCfg.DftNum = AppIMPCfg.DftNum;
    dsp_cfg.DftCfg.DftSrc = AppIMPCfg.DftSrc;
    dsp_cfg.DftCfg.HanWinEn = AppIMPCfg.HanWinEn;
    
    memset(&dsp_cfg.StatCfg, 0, sizeof(dsp_cfg.StatCfg));
    AD5940_DSPCfgS(&dsp_cfg);
        
    /* Enable all of them. They are automatically turned off during hibernate mode to save power */
    if(AppIMPCfg.BiasVolt == 0.0f)
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
        AppIMPCfg.InitSeqInfo.SeqId = SEQID_1;
        AppIMPCfg.InitSeqInfo.SeqRamAddr = AppIMPCfg.SeqStartAddr;
        AppIMPCfg.InitSeqInfo.pSeqCmd = pSeqCmd;
        AppIMPCfg.InitSeqInfo.SeqLen = SeqLen;
        /* Write command to SRAM */
        AD5940_SEQCmdWrite(AppIMPCfg.InitSeqInfo.SeqRamAddr, pSeqCmd, SeqLen);
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
    clks_cal.DftSrc = AppIMPCfg.DftSrc;
    clks_cal.DataCount = 1L<<(AppIMPCfg.DftNum+2); /* 2^(DFTNUMBER+2) */
    clks_cal.ADCSinc2Osr = AppIMPCfg.ADCSinc2Osr;
    clks_cal.ADCSinc3Osr = AppIMPCfg.ADCSinc3Osr;
    clks_cal.ADCAvgNum = AppIMPCfg.ADCAvgNum;
    clks_cal.RatioSys2AdcClk = AppIMPCfg.SysClkFreq/AppIMPCfg.AdcClkFreq;
    AD5940_ClksCalculate(&clks_cal, &WaitClks);

    AD5940_SEQGenCtrl(bTRUE);
    AD5940_SEQGpioCtrlS(AGPIO_Pin2); /* Set GPIO1, clear others that under control */
    AD5940_SEQGenInsert(SEQ_WAIT(16*250));  /* wait 250us? */
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
    sw_cfg.Dswitch = AppIMPCfg.DswitchSel;
    sw_cfg.Pswitch = AppIMPCfg.PswitchSel;
    sw_cfg.Nswitch = AppIMPCfg.NswitchSel;
    sw_cfg.Tswitch = SWT_TRTIA|AppIMPCfg.TswitchSel;
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
        AppIMPCfg.MeasureSeqInfo.SeqId = SEQID_0;
        AppIMPCfg.MeasureSeqInfo.SeqRamAddr = AppIMPCfg.InitSeqInfo.SeqRamAddr + AppIMPCfg.InitSeqInfo.SeqLen ;
        AppIMPCfg.MeasureSeqInfo.pSeqCmd = pSeqCmd;
        AppIMPCfg.MeasureSeqInfo.SeqLen = SeqLen;
        /* Write command to SRAM */
        AD5940_SEQCmdWrite(AppIMPCfg.MeasureSeqInfo.SeqRamAddr, pSeqCmd, SeqLen);
    }
    else
        return error; /* Error */
    return AD5940ERR_OK;
}

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
    /* Configure clock */
    clk_cfg.ADCClkDiv = ADCCLKDIV_1;
    clk_cfg.ADCCLkSrc = ADCCLKSRC_HFOSC;
    clk_cfg.SysClkDiv = SYSCLKDIV_1;
    clk_cfg.SysClkSrc = SYSCLKSRC_HFOSC;
    clk_cfg.HfOSC32MHzMode = false;
    clk_cfg.HFOSCEn = true;
    clk_cfg.HFXTALEn = false;
    clk_cfg.LFOSCEn = true;
    AD5940_CLKCfg(&clk_cfg);
    
    /* Configure FIFO and Sequencer*/
    fifo_cfg.FIFOEn = false;
    fifo_cfg.FIFOMode = FIFOMODE_FIFO;
    fifo_cfg.FIFOSize = FIFOSIZE_4KB;   /* 4kB for FIFO, The reset 2kB for sequencer */
    fifo_cfg.FIFOSrc = FIFOSRC_DFT;
    fifo_cfg.FIFOThresh = 4;            /* DFT result. One pair for RCAL, another for Rz. One DFT result have real part and imaginary part */
    AD5940_FIFOCfg(&fifo_cfg);
    fifo_cfg.FIFOEn = true;
    AD5940_FIFOCfg(&fifo_cfg);

    /* Interrupt controller */
    AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, true);   /* Enable all interrupt in INTC1, so we can check INTC flags */
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, true); 
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

static AD5940Err AppADCPgaCal(void)
{
   ADCPGACal_Type pga_cal;
   
   /* Calibrate ADC PGA(offset and gain) */
   pga_cal.AdcClkFreq = AppIMPCfg.AdcClkFreq;
   pga_cal.SysClkFreq = AppIMPCfg.SysClkFreq;
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
//   hsrtia_cal.AdcClkFreq = AppIMPCfg.AdcClkFreq;
//   hsrtia_cal.ADCSinc2Osr = AppIMPCfg.ADCSinc2Osr;
//   hsrtia_cal.ADCSinc3Osr = AppIMPCfg.ADCSinc3Osr;
//   hsrtia_cal.bPolarResult = bTRUE; /* We need magnitude and phase here */
//   hsrtia_cal.DftCfg.DftNum = AppIMPCfg.DftNum;
//   hsrtia_cal.DftCfg.DftSrc = AppIMPCfg.DftSrc;
//   hsrtia_cal.DftCfg.HanWinEn = AppIMPCfg.HanWinEn;
//   hsrtia_cal.fRcal= AppIMPCfg.RcalVal;
//   hsrtia_cal.HsTiaCfg.DiodeClose = bFALSE;
//   hsrtia_cal.HsTiaCfg.HstiaBias = HSTIABIAS_1P1;
//   hsrtia_cal.HsTiaCfg.HstiaCtia = AppIMPCfg.CtiaSel;
//   hsrtia_cal.HsTiaCfg.HstiaDeRload = HSTIADERLOAD_OPEN;
//   hsrtia_cal.HsTiaCfg.HstiaDeRtia = HSTIADERTIA_TODE;
//   hsrtia_cal.HsTiaCfg.HstiaRtiaSel = AppIMPCfg.HstiaRtiaSel;
//   hsrtia_cal.SysClkFreq = AppIMPCfg.SysClkFreq;
//   if(AppIMPCfg.SweepCfg.SweepEn == bTRUE)
//   {
//      uint32_t i;
//      AppIMPCfg.SweepCfg.SweepIndex = 0;  /* Reset index */
//      for(i=0;i<AppIMPCfg.SweepCfg.SweepPoints;i++)
//      {
//         AD5940_SweepNext(&AppIMPCfg.SweepCfg, &hsrtia_cal.fFreq);
//         AD5940_HSRtiaCal(&hsrtia_cal, AppIMPCfg.RtiaCalTable[i]);
//         printf("Freq:%.2f,Mag:%.2f,Phase:%fDegree\n", hsrtia_cal.fFreq, AppIMPCfg.RtiaCalTable[i][0], 
//         AppIMPCfg.RtiaCalTable[i][1]*180/MATH_PI);
//       }
//       AppIMPCfg.RtiaCurrValue[AppIMPCfg.SweepCfg.SweepIndex] = AppIMPCfg.RtiaCalTable[i][0];
//       AppIMPCfg.RtiaCurrValue[AppIMPCfg.SweepCfg.SweepIndex] = AppIMPCfg.RtiaCalTable[i][0];
//       AppIMPCfg.SweepCfg.SweepIndex = 0;  /* Reset index */
//   }
//   else
//   {
//      hsrtia_cal.fFreq = AppIMPCfg.SinFreq;
//      AD5940_HSRtiaCal(&hsrtia_cal, AppIMPCfg.RtiaCurrValue);
//      printf("RtiaMag:%.2f,Phase:%fDegree\n", AppIMPCfg.RtiaCurrValue[0], 
//      AppIMPCfg.RtiaCurrValue[1]*180/MATH_PI);
//   }
//   return AD5940ERR_OK;
//}

void AD5940ImpedanceStructInit(void)
{
    AppIMPCfg_Type *pImpedanceCfg;

    AppIMPGetCfg(&pImpedanceCfg);
    /* Step1: configure initialization sequence Info */
    pImpedanceCfg->SeqStartAddr = 0;
    pImpedanceCfg->MaxSeqLen = 512; /* add checker in function */

    pImpedanceCfg->RcalVal = 3000.0;
    pImpedanceCfg->SinFreq = 50000.0;
    pImpedanceCfg->FifoThresh = 4;

    /* Set switch matrix to onboard(EVAL-AD5940ELECZ) dummy sensor. */
    /* Note the RCAL0 resistor is 10kOhm. */
    pImpedanceCfg->DswitchSel = SWD_CE0;
    pImpedanceCfg->PswitchSel = SWP_CE0;
    pImpedanceCfg->NswitchSel = SWN_AIN2;
    pImpedanceCfg->TswitchSel = SWT_AIN2;
    /* The dummy sensor is as low as 5kOhm. We need to make sure RTIA is small enough that HSTIA won't be saturated. */
    pImpedanceCfg->HstiaRtiaSel = HSTIARTIA_200;

    /* Configure the sweep function. */
    pImpedanceCfg->SweepCfg.SweepEn = false;
    pImpedanceCfg->SweepCfg.SweepStart = 100.0f;	/* Start from 1kHz */
    pImpedanceCfg->SweepCfg.SweepStop = 100e3f;		/* Stop at 100kHz */
    pImpedanceCfg->SweepCfg.SweepPoints = 101;		/* Points is 101 */
    pImpedanceCfg->SweepCfg.SweepLog = true;
    /* Configure Power Mode. Use HP mode if frequency is higher than 80kHz. */
    pImpedanceCfg->PwrMod = AFEPWR_HP;
    /* Configure filters if necessary */
    pImpedanceCfg->ADCSinc3Osr = ADCSINC3OSR_2;		/* Sample rate is 800kSPS/2 = 400kSPS */
    pImpedanceCfg->DftNum = DFTNUM_16384;
    pImpedanceCfg->DftSrc = DFTSRC_SINC3;
}


/* This function provide application initialize. It can also enable Wupt that will automatically trigger sequence. Or it can configure  */
int32_t AppIMPInit(void)
{
    AD5940Err error = AD5940ERR_OK;  
    SEQCfg_Type seq_cfg;
    FIFOCfg_Type fifo_cfg;
    
    error = spi_device_attach(&my_spi_device, "spi1");
    if (error != 0) {
        LOG_E("spi_device_attach errno!");
        return error;
    }
    
    uint8_t tx_data[] = {0x20, 0x04, 0x00};
    spi_write(&my_spi_device, tx_data, 3);
    
    AD5940_MCUResourceInit(0);
    AD5940PlatformCfg();
    AD5940ImpedanceStructInit();

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
    fifo_cfg.FIFOThresh = AppIMPCfg.FifoThresh;              /* DFT result. One pair for RCAL, another for Rz. One DFT result have real part and imaginary part */
    AD5940_FIFOCfg(&fifo_cfg);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

    /* Start sequence generator */
    /* Initialize sequencer generator */
    if((AppIMPCfg.IMPInited == bFALSE)||\
            (AppIMPCfg.bParaChanged == bTRUE))
    {
        AD5940_SEQGenInit(AppBuff, APPBUFF_SIZE);

        /* Generate initialize sequence */
        error = AppIMPSeqCfgGen(); /* Application initialization sequence using either MCU or sequencer */
        if(error != AD5940ERR_OK) return error;

        /* Generate measurement sequence */
        error = AppIMPSeqMeasureGen();
        if(error != AD5940ERR_OK) return error;

        AppIMPCfg.bParaChanged = bFALSE; /* Clear this flag as we already implemented the new configuration */
    }

    /* Initialization sequencer  */
    AppIMPCfg.InitSeqInfo.WriteSRAM = bFALSE;
    AD5940_SEQInfoCfg(&AppIMPCfg.InitSeqInfo);
    seq_cfg.SeqEnable = bTRUE;
    AD5940_SEQCfg(&seq_cfg);  /* Enable sequencer */
    AD5940_SEQMmrTrig(AppIMPCfg.InitSeqInfo.SeqId);
    while(AD5940_INTCTestFlag(AFEINTC_1, AFEINTSRC_ENDSEQ) == bFALSE);
    
    /* Measurement sequence  */
    AppIMPCfg.MeasureSeqInfo.WriteSRAM = bFALSE;
    AD5940_SEQInfoCfg(&AppIMPCfg.MeasureSeqInfo);

    seq_cfg.SeqEnable = bTRUE;
    AD5940_SEQCfg(&seq_cfg);  /* Enable sequencer, and wait for trigger */
    AD5940_ClrMCUIntFlag();   /* Clear interrupt flag generated before */

    AD5940_AFEPwrBW(AppIMPCfg.PwrMod, AFEBW_250KHZ);

    AppIMPCfg.IMPInited = bTRUE;  /* IMP application has been initialized. */
    return AD5940ERR_OK;
}

/* Modify registers when AFE wakeup */
int32_t AppIMPRegModify(int32_t * const pData, uint32_t *pDataCount)
{
    if(AppIMPCfg.NumOfData > 0)
    {
        AppIMPCfg.FifoDataCount += *pDataCount/4;
        if(AppIMPCfg.FifoDataCount >= AppIMPCfg.NumOfData)
        {
            AD5940_WUPTCtrl(bFALSE);
            return AD5940ERR_OK;
        }
    }
    if(AppIMPCfg.StopRequired == bTRUE)
    {
        AD5940_WUPTCtrl(bFALSE);
        return AD5940ERR_OK;
    }
    if(AppIMPCfg.SweepCfg.SweepEn) /* Need to set new frequency and set power mode */
    {
        AD5940_WGFreqCtrlS(AppIMPCfg.SweepNextFreq, AppIMPCfg.SysClkFreq);
    }
    return AD5940ERR_OK;
}

/* Depending on the data type, do appropriate data pre-process before return back to controller */
int32_t AppIMPDataProcess(int32_t * const pData, uint32_t *pDataCount)
{
    uint32_t DataCount = *pDataCount;
    uint32_t ImpResCount = DataCount/4;

    fImpPol_Type * const pOut = (fImpPol_Type*)pData;
    iImpCar_Type * pSrcData = (iImpCar_Type*)pData;

    *pDataCount = 0;

    DataCount = (DataCount/4)*4;/* We expect RCAL data together with Rz data. One DFT result has two data in FIFO, real part and imaginary part.  */

    /* Convert DFT result to int32_t type */
    for(uint32_t i=0; i<DataCount; i++)
    {
        pData[i] &= 0x3ffff; /* @todo option to check ECC */
        if(pData[i]&(1L<<17)) /* Bit17 is sign bit */
        {
            pData[i] |= 0xfffc0000; /* Data is 18bit in two's complement, bit17 is the sign bit */
        }
    }
    for(uint32_t i=0; i<ImpResCount; i++)
    {
        iImpCar_Type *pDftRcal, *pDftRz;

        pDftRcal = pSrcData++;
        pDftRz = pSrcData++;
        float RzMag,RzPhase;
        float RcalMag, RcalPhase;
        
        RcalMag = sqrt((float)pDftRcal->Real*pDftRcal->Real+(float)pDftRcal->Image*pDftRcal->Image);
        RcalPhase = atan2(-pDftRcal->Image,pDftRcal->Real);
        RzMag = sqrt((float)pDftRz->Real*pDftRz->Real+(float)pDftRz->Image*pDftRz->Image);
        RzPhase = atan2(-pDftRz->Image,pDftRz->Real);

        RzMag = RcalMag/RzMag*AppIMPCfg.RcalVal;
        RzPhase = RcalPhase - RzPhase;
        //printf("V:%d,%d,I:%d,%d ",pDftRcal->Real,pDftRcal->Image, pDftRz->Real, pDftRz->Image);
        
        pOut[i].Magnitude = RzMag;
        pOut[i].Phase = RzPhase;
    }
    *pDataCount = ImpResCount; 
    AppIMPCfg.FreqofData = AppIMPCfg.SweepCurrFreq;
    /* Calculate next frequency point */
    if(AppIMPCfg.SweepCfg.SweepEn == bTRUE)
    {
        AppIMPCfg.FreqofData = AppIMPCfg.SweepCurrFreq;
        AppIMPCfg.SweepCurrFreq = AppIMPCfg.SweepNextFreq;
        AD5940_SweepNext(&AppIMPCfg.SweepCfg, &AppIMPCfg.SweepNextFreq);
    }

    return 0;
}

/**

*/
int32_t AppIMPISR(void *pBuff, uint32_t *pCount)
{
    uint32_t BuffCount;
    uint32_t FifoCnt;
    BuffCount = *pCount;
    
    *pCount = 0;
    
    if(AD5940_WakeUp(10) > 10)  /* Wakeup AFE by read register, read 10 times at most */
        return AD5940ERR_WAKEUP;  /* Wakeup Failed */
    AD5940_SleepKeyCtrlS(SLPKEY_LOCK);  /* Prohibit AFE to enter sleep mode. */

    if(AD5940_INTCTestFlag(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH) == bTRUE)
    {
        /* Now there should be 4 data in FIFO */
        FifoCnt = (AD5940_FIFOGetCnt()/4)*4;
        
        if(FifoCnt > BuffCount)
        {
            ///@todo buffer is limited.
        }
        AD5940_FIFORd((uint32_t *)pBuff, FifoCnt);
        AD5940_INTCClrFlag(AFEINTSRC_DATAFIFOTHRESH);
        AppIMPRegModify(pBuff, &FifoCnt);   /* If there is need to do AFE re-configure, do it here when AFE is in active state */
        //AD5940_EnterSleepS(); /* Manually put AFE back to hibernate mode. This operation only takes effect when register value is ACTIVE previously */
        AD5940_SleepKeyCtrlS(SLPKEY_UNLOCK);  /* Allow AFE to enter sleep mode. */
        /* Process data */ 
        AppIMPDataProcess((int32_t*)pBuff,&FifoCnt); 
        *pCount = FifoCnt;
        return 0;
    }
    
    return 0;
}

void AppImpTask(void)
{
    uint32_t temp;
    
    if(AD5940_GetMCUIntFlag())
    {
        AD5940_ClrMCUIntFlag();
        temp = APPBUFF_SIZE;
        AppIMPISR(AppBuff, &temp);
        ImpedanceShowResult(AppBuff, temp);
    }
}

/* Private functions ---------------------------------------------------------*/
