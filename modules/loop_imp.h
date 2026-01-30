/**
    ******************************************************************************
    * @file        : loop_imp.h
    * @author      : ZJY
    * @version     : V1.0
    * @date        : 2025-01-XX
    * @brief       : 回路阻抗测量模块头文件
    * @attention   : None
    ******************************************************************************
    * @history     :
    *         V1.0 : 1. 回路阻抗测量模块头文件
    ******************************************************************************
    */
#ifndef __LOOP_IMP_H__
#define __LOOP_IMP_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Includes ------------------------------------------------------------------*/
#include "ad5940.h"
#include "stdio.h"
#include "string.h"
#include "math.h"

/* Exported types ------------------------------------------------------------*/
typedef struct
{
    BoolFlag bParaChanged;        /* Indicate to generate sequence again. */

    uint32_t SeqStartAddr;        /* 初始化序列在 AD5940 SRAM 中的起始地址。用于存储初始化命令序列。  */
    uint32_t MaxSeqLen;           /* 初始化序列的最大长度限制，防止序列超出 SRAM 容量。   */
    uint32_t SeqStartAddrCal;     /* 测量序列在 AD5940 SRAM 中的起始地址。用于存储测量命令序列。 */
    uint32_t MaxSeqLenCal;        /* 测量序列的最大长度限制，防止序列超出 SRAM 容量。 */

    float ImpODR;                 /* 阻抗输出数据率（Output Data Rate），单位 Hz，控制阻抗数据的输出频率。 */
    int32_t NumOfData;            /* 数据采集数量。默认 -1 表示持续采集不停止；设置为正数时，采集到指定数量后停止。 */
    float WuptClkFreq;            /* 唤醒定时器时钟频率（Hz），通常为 32kHz。用于软件校准时钟。 */
    float SysClkFreq;             /* 系统时钟实际频率（Hz），默认 16MHz。用于频率计算和时序控制。 */
    float AdcClkFreq;             /* ADC 时钟实际频率（Hz），默认 16MHz。用于 ADC 采样率计算。 */
    float RcalVal;                /* 校准电阻值（Ω），用于阻抗校准计算。 */

    /* Switch Configuration */
    uint32_t DswitchSel;
    uint32_t PswitchSel;
    uint32_t NswitchSel;
    uint32_t TswitchSel;
    
    uint32_t PwrMod;              /* Control Chip power mode(LP/HP) */
    uint32_t HstiaRtiaSel;        /* Use internal RTIA, select from RTIA_INT_200, RTIA_INT_1K, RTIA_INT_5K, RTIA_INT_10K, RTIA_INT_20K, RTIA_INT_40K, RTIA_INT_80K, RTIA_INT_160K */
    uint32_t ExcitBufGain;        /* Select from  EXCTBUFGAIN_2, EXCTBUFGAIN_0P25 */     
    uint32_t HsDacGain;           /* Select from  HSDACGAIN_1, HSDACGAIN_0P2 */
    uint32_t HsDacUpdateRate;
    
    float DacVoltPP;              /* DAC output voltage in mV peak to peak. Maximum value is 800mVpp. Peak to peak voltage  */
    float BiasVolt;               /* The excitation signal is DC+AC. This parameter decides the DC value in mV unit. 0.0mV means no DC bias.*/
    float SinFreq;                /* Frequency of excitation signal */
    uint32_t DftNum;              /* DFT number */
    uint32_t DftSrc;              /* DFT Source */
    BoolFlag HanWinEn;            /* Enable Hanning window */
    uint32_t AdcPgaGain;          /* PGA Gain select from GNPGA_1, GNPGA_1_5, GNPGA_2, GNPGA_4, GNPGA_9 !!! We must ensure signal is in range of +-1.5V which is limited by ADC input stage */   
    uint8_t ADCSinc3Osr;
    uint8_t ADCSinc2Osr;  
    uint8_t ADCAvgNum;
    /* Sweep Function Control */
    uint32_t FifoThresh;           /* FIFO threshold. Should be N*4 */
    float FreqofData;                         /* The frequency of latest data sampled */
    BoolFlag IMPInited;                       /* If the program run firstly, generated sequence commands */
    SEQInfo_Type InitSeqInfo;
    SEQInfo_Type MeasureSeqInfo;
    BoolFlag StopRequired;          /* After FIFO is ready, stop the measurement sequence */
    uint32_t FifoDataCount;         /* Count how many times impedance have been measured */
}loop_imp_cfg_t;

/* Exported constants --------------------------------------------------------*/

/* Exported macros -----------------------------------------------------------*/
#define IMPCTRL_START          0
#define IMPCTRL_STOPNOW        1
#define IMPCTRL_STOPSYNC       2
#define IMPCTRL_GETFREQ        3   /* Get Current frequency of returned data from ISR */
#define IMPCTRL_SHUTDOWN       4   /* Note: shutdown here means turn off everything and put AFE to hibernate mode. The word 'SHUT DOWN' is only used here. */

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/
int32_t loop_imp_init(void);
int32_t loop_imp_isr(void *pBuff, uint32_t *pCount);
int32_t loop_imp_ctrl(uint32_t Command, void *pPara);
void    loop_imp_task(void);
float   loop_imp_get_data(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __LOOP_IMP_H__ */
