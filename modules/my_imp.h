#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  IMP2W_OK = 0,
  IMP2W_NOT_CALIBRATED,
  IMP2W_OPEN_CIRCUIT,
  IMP2W_SATURATION,
  IMP2W_NUMERIC_ERROR,
} imp2w_status_t;

typedef struct
{
  /* 固定要求：50kHz */
  float   sys_clk_hz;        /* e.g. 16e6 */
  float   freq_hz;           /* 50e3 */
  float   rcal_ohm;          /* e.g. 1000 */
  float   excit_vpp_mv;      /* e.g. 150~400mVpp */

  /* 量程 30~500Ω 推荐：RTIA=200Ω 起步 */
  uint32_t hstia_rtia_sel;   /* e.g. HSTIARTIA_200 */
  uint32_t hstia_ctia;       /* e.g. 8~16 (pF code) */
  uint32_t adc_pga;          /* e.g. ADCPGA_1P5 */

  /* 平均次数：测量阶段建议 8~32 */
  uint16_t avg_n;            /* e.g. 16 */

  /* 开路判定阈值（双条件） */
  float   open_z_min_ohm;    /* e.g. 2000Ω：超过即认为超量程/疑似开路 */
  float   open_sigma_k;      /* e.g. 5：open阈值=mean+K*std */

  /* 是否启用 Open/Short 补偿 */
  uint8_t enable_short_comp; /* 1: yes */
  uint8_t enable_open_comp;  /* 1: yes */
} imp2w_cfg_t;

typedef struct
{
  float z_re;
  float z_im;
  float z_mag;
  float z_phase_rad;
} imp2w_result_t;

/* 初始化：配置 50kHz + ADC 800k + DFT16 等 */
void IMP2W_50K_Init(const imp2w_cfg_t *cfg);

/* 只做 Rcal 校准（必须先做） */
imp2w_status_t IMP2W_50K_CalibrateRcal(uint16_t avg_n);

/* 短路校准：外部短接 CE0-SE0 */
imp2w_status_t IMP2W_50K_CalibrateShort(uint16_t avg_n);

/* 开路校准：外部断开 CE0-SE0 */
imp2w_status_t IMP2W_50K_CalibrateOpen(uint16_t avg_n);

/* 测量：自动做开路检测 + short/open 补偿 */
imp2w_status_t IMP2W_50K_Measure(imp2w_result_t *out);

#ifdef __cplusplus
}
#endif
