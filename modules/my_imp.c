#include "my_imp.h"
#include <string.h>
#include <math.h>

/* 你工程里应当已经有 ad5940.h */
#include "ad5940.h"
#include "spi.h"

#define  LOG_TAG             "my_imp"
#define  LOG_LVL             4
#include "log.h"

/* ----------------- 简单复数工具 ----------------- */
typedef struct { float re, im; } cplx_f;

static cplx_f c_add(cplx_f a, cplx_f b){ cplx_f r={a.re+b.re, a.im+b.im}; return r; }
static cplx_f c_sub(cplx_f a, cplx_f b){ cplx_f r={a.re-b.re, a.im-b.im}; return r; }
static cplx_f c_mul(cplx_f a, cplx_f b){
  cplx_f r={a.re*b.re - a.im*b.im, a.re*b.im + a.im*b.re}; return r;
}
static cplx_f c_div(cplx_f a, cplx_f b){
  float den = b.re*b.re + b.im*b.im;
  cplx_f r={0};
  if(den < 1e-20f) return r;
  r.re = (a.re*b.re + a.im*b.im)/den;
  r.im = (a.im*b.re - a.re*b.im)/den;
  return r;
}
static float c_mag(cplx_f a){ return sqrtf(a.re*a.re + a.im*a.im); }
static float c_phase(cplx_f a){ return atan2f(a.im, a.re); }
static cplx_f c_inv(cplx_f a){
  float den = a.re*a.re + a.im*a.im;
  cplx_f r={0};
  if(den < 1e-20f) return r;
  r.re = a.re/den;
  r.im = -a.im/den;
  return r;
}

/* ----------------- 模块内部状态 ----------------- */
static imp2w_cfg_t g_cfg;

static uint8_t g_cal_rcal_ok  = 0;
static uint8_t g_cal_short_ok = 0;
static uint8_t g_cal_open_ok  = 0;

static cplx_f g_i_cal;     /* Rcal 回路下 DFT电流(复数) */
static cplx_f g_z_short;   /* Short测得的等效阻抗(复数) */
static cplx_f g_z_open;    /* Open测得的等效阻抗(复数) */

static float g_ix_open_mean = 0.0f; /* Open时 Ix幅值均值(DFT原始幅值单位) */
static float g_ix_open_std  = 0.0f; /* Open时 Ix幅值标准差 */
static float g_ix_open_th   = 0.0f; /* 判定开路的 Ix 幅值阈值 */

/* 定义设备 */
static struct spi_device spi_device = {
    .name = "ad5940",
    .controller = NULL,
    .max_speed_hz = 10000000U,      /* 16MHz */
    .chip_select = 0U,              /* 硬件CS编号 */
    .mode = SPI_MODE_0 | SPI_MODE_MSB | SPI_MODE_SW_CS | SPI_MODE_4WIRE,
    .bits_per_word = 8U,
    .cs_pin = 4U,
    .controller_data = NULL
};

/* ----------------- 关键：开关矩阵路径 -----------------
 * 你的引脚：CE0 / SE0
 * 实际矩阵一般要用 SE0LOAD（经过约100Ω），否则T矩阵无法直接选SE0。
 */
static void path_set_rcal(void)
{
  SWMatrixCfg_Type sw;
  sw.Dswitch = SWD_RCAL0;
  sw.Pswitch = SWP_RCAL0;
  sw.Nswitch = SWN_RCAL1;
  sw.Tswitch = SWT_RCAL1 | SWT_TRTIA;
  AD5940_SWMatrixCfgS(&sw);
}

static void path_set_short_or_meas(void)
{
  SWMatrixCfg_Type sw;
  sw.Dswitch = SWD_CE0;
  sw.Pswitch = SWP_CE0;
  sw.Nswitch = SWN_SE0LOAD;
  sw.Tswitch = SWT_SE0LOAD | SWT_TRTIA;
  AD5940_SWMatrixCfgS(&sw);
}

/* ----------------- 读DFT结果（电流相量） ----------------- */
static cplx_f read_dft_i(void)
{
  int32_t re = (int32_t)AD5940_ReadAfeResult(AFERESULT_DFTREAL);
  int32_t im = (int32_t)AD5940_ReadAfeResult(AFERESULT_DFTIMAGE);

  /* 18-bit signed sign extension */
  if(re & (1L<<17)) re |= (int32_t)0xFFFC0000;
  if(im & (1L<<17)) im |= (int32_t)0xFFFC0000;

  /* AD5940 Imag 寄存器符号通常需取反 */
  im = -im;
    
//  LOG_D("[DFT]re=%ld im=%ld |I|=%ld",
//      (long)re, (long)im, (long)((int32_t)sqrtf((float)re*re + (float)im*im)));

  cplx_f r;
  r.re = (float)re;
  r.im = (float)im;
  return r;
}

/* 粗略饱和判断：DFT接近满量程 */
static uint8_t is_dft_saturated(cplx_f d)
{
  const float lim = 0.95f * 131071.0f; /* 2^17-1=131071 */
  return (fabsf(d.re) > lim) || (fabsf(d.im) > lim);
}

static cplx_f run_dft_once(void)
{
  /* 以 HSTIA 输出采样 */
  AD5940_ADCMuxCfgS(ADCMUXP_HSTIA_P, ADCMUXN_HSTIA_N);

  AD5940_INTCClrFlag(AFEINTSRC_DFTRDY);

//  AD5940_AFECtrlS(AFECTRL_WG | AFECTRL_ADCPWR, bTRUE);
  AD5940_Delay10us(25); /* 250us settle：可按实际再优化 */

  AD5940_AFECtrlS(AFECTRL_ADCCNV | AFECTRL_DFT, bTRUE);
  while(AD5940_INTCTestFlag(AFEINTC_1, AFEINTSRC_DFTRDY) == bFALSE);

  AD5940_AFECtrlS(AFECTRL_ADCCNV | AFECTRL_DFT, bFALSE);
//  AD5940_AFECtrlS(AFECTRL_WG | AFECTRL_ADCPWR, bFALSE);

  return read_dft_i();
}

static cplx_f run_dft_avg(uint16_t n, uint8_t *sat)
{
  cplx_f acc = {0};
  *sat = 0;

  if(n == 0) n = 1;
  for(uint16_t i=0;i<n;i++){
    cplx_f d = run_dft_once();
    if(is_dft_saturated(d)) *sat = 1;
    acc.re += d.re;
    acc.im += d.im;
  }
  acc.re /= (float)n;
  acc.im /= (float)n;
  return acc;
}

/* 计算 Z = Rcal * (Ical / Ix) */
static cplx_f calc_z_from_i(cplx_f i_cal, cplx_f i_x, float rcal)
{
  cplx_f ratio = c_div(i_cal, i_x);
  cplx_f z = { ratio.re * rcal, ratio.im * rcal };
  return z;
}

/* Open/Short 补偿（先短路后开路）：
 * Zs = Zmeas - Zshort
 * Ys = 1/Zs
 * Yopen = 1/(Zopen - Zshort)  (更严谨)
 * Ycorr = Ys - Yopen
 * Zcorr = 1/Ycorr
 */
static cplx_f apply_short_open_comp(cplx_f z_meas)
{
  cplx_f z = z_meas;

  if(g_cfg.enable_short_comp && g_cal_short_ok){
    z = c_sub(z, g_z_short);
  }

  if(g_cfg.enable_open_comp && g_cal_open_ok){
    cplx_f z_open_s = g_z_open;
    if(g_cfg.enable_short_comp && g_cal_short_ok){
      z_open_s = c_sub(g_z_open, g_z_short);
    }

    cplx_f y  = c_inv(z);
    cplx_f yo = c_inv(z_open_s);

    cplx_f ycorr = c_sub(y, yo);
    cplx_f zcorr = c_inv(ycorr);

    /* 防数值炸掉：如果 ycorr 太小则返回原z */
    if(c_mag(ycorr) > 1e-12f){
      z = zcorr;
    }
  }

  return z;
}

/* ----------------- 对外接口 ----------------- */

static uint32_t calc_wg_amp_word(float excit_vpp_mv, uint32_t *excbuf_gain, uint32_t *hsdac_gain)
{
  float V = excit_vpp_mv;
  uint32_t word;

  if(V <= 800.0f * 0.05f) {
    *excbuf_gain = EXCITBUFGAIN_0P25;
    *hsdac_gain  = HSDACGAIN_0P2;
    word = ((uint32_t)(V/40.0f*2047.0f*2.0f)+1)>>1;
  } else if(V <= 800.0f * 0.25f) {
    *excbuf_gain = EXCITBUFGAIN_0P25;
    *hsdac_gain  = HSDACGAIN_1;
    word = ((uint32_t)(V/200.0f*2047.0f*2.0f)+1)>>1;
  } else if(V <= 800.0f * 0.4f) {
    *excbuf_gain = EXCITBUFGAIN_2;
    *hsdac_gain  = HSDACGAIN_0P2;
    word = ((uint32_t)(V/320.0f*2047.0f*2.0f)+1)>>1;
  } else {
    *excbuf_gain = EXCITBUFGAIN_2;
    *hsdac_gain  = HSDACGAIN_1;
    word = ((uint32_t)(V/1600.0f*2047.0f*2.0f)+1)>>1;
  }

  if(word > 0x7ff) word = 0x7ff;
  return word;
}

void IMP2W_50K_Init(const imp2w_cfg_t *cfg)
{
  g_cfg = *cfg;

  g_cal_rcal_ok  = 0;
  g_cal_short_ok = 0;
  g_cal_open_ok  = 0;
  g_ix_open_mean = 0;
  g_ix_open_std  = 0;
  g_ix_open_th   = 0;

    int ret = spi_device_attach(&spi_device, "spi1");
    if (ret != 0) {
        LOG_E("spi_device_attach errno!");
        return;
    }

    uint8_t tx_data[] = {0x20, 0x04, 0x00};
    spi_write(&spi_device, tx_data, 3);
    
  AD5940_Initialize();

  /* 你可按工程需要配置时钟；这里示例 HFOSC=16MHz */
  CLKCfg_Type clk;
  memset(&clk, 0, sizeof(clk));
  clk.HFOSCEn = bTRUE;
  clk.HfOSC32MHzMode = bFALSE;
  clk.SysClkSrc = SYSCLKSRC_HFOSC;
  clk.ADCCLkSrc = ADCCLKSRC_HFOSC;
  clk.SysClkDiv = 1;
  clk.ADCClkDiv = 1;
  AD5940_CLKCfg(&clk);

  /* DFT Ready interrupt */
  AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_DFTRDY, bTRUE);

  AD5940_AFECtrlS(AFECTRL_ALL, bFALSE);

  /* Reference */
  AFERefCfg_Type ref;
  memset(&ref, 0, sizeof(ref));
  ref.HpBandgapEn = bTRUE;
  ref.Hp1V1BuffEn = bTRUE;
  ref.Hp1V8BuffEn = bTRUE;
  AD5940_REFCfgS(&ref);

  /* HS Loop */
  HSLoopCfg_Type hs;
  memset(&hs, 0, sizeof(hs));

  uint32_t excbuf_gain, hsdac_gain;
  uint32_t amp_word = calc_wg_amp_word(g_cfg.excit_vpp_mv, &excbuf_gain, &hsdac_gain);

  hs.HsDacCfg.ExcitBufGain = excbuf_gain;
  hs.HsDacCfg.HsDacGain = hsdac_gain;
  hs.HsDacCfg.HsDacUpdateRate = 7;

  HSTIACfg_Type hstia;
  memset(&hstia, 0, sizeof(hstia));
  hstia.HstiaBias    = HSTIABIAS_1P1;
  hstia.HstiaRtiaSel = g_cfg.hstia_rtia_sel;
  hstia.HstiaCtia    = g_cfg.hstia_ctia;
  hstia.DiodeClose   = bFALSE;
  hstia.HstiaDeRtia  = HSTIADERTIA_OPEN;
  hstia.HstiaDeRload = HSTIADERLOAD_OPEN;
  hstia.HstiaDe1Rtia = HSTIADERTIA_OPEN;
  hstia.HstiaDe1Rload= HSTIADERLOAD_OPEN;

  hs.HsTiaCfg = hstia;

  /* 默认先配 Rcal 路径，校准时会切换 */
  hs.SWMatCfg.Dswitch = SWD_RCAL0;
  hs.SWMatCfg.Pswitch = SWP_RCAL0;
  hs.SWMatCfg.Nswitch = SWN_RCAL1;
  hs.SWMatCfg.Tswitch = SWT_RCAL1 | SWT_TRTIA;

  hs.WgCfg.WgType = WGTYPE_SIN;
  hs.WgCfg.GainCalEn = bTRUE;
  hs.WgCfg.OffsetCalEn = bTRUE;
  hs.WgCfg.SinCfg.SinFreqWord = AD5940_WGFreqWordCal(g_cfg.freq_hz, g_cfg.sys_clk_hz);
  hs.WgCfg.SinCfg.SinAmplitudeWord = amp_word;
  hs.WgCfg.SinCfg.SinOffsetWord = 0;
  hs.WgCfg.SinCfg.SinPhaseWord = 0;

  AD5940_HSLoopCfgS(&hs);

  /* DSP：800k + DFT16 */
  DSPCfg_Type dsp;
  memset(&dsp, 0, sizeof(dsp));
  dsp.ADCBaseCfg.ADCMuxP = ADCMUXP_HSTIA_P;
  dsp.ADCBaseCfg.ADCMuxN = ADCMUXN_HSTIA_N;
  dsp.ADCBaseCfg.ADCPga  = g_cfg.adc_pga;

  dsp.ADCFilterCfg.ADCRate = ADCRATE_800KHZ;
  dsp.ADCFilterCfg.BpSinc3 = bTRUE;
  dsp.ADCFilterCfg.BpNotch = bTRUE;
  dsp.ADCFilterCfg.ADCSinc3Osr = ADCSINC3OSR_2;
  dsp.ADCFilterCfg.ADCSinc2Osr = ADCSINC2OSR_22;
  dsp.ADCFilterCfg.ADCAvgNum   = ADCAVGNUM_2;

  dsp.DftCfg.DftSrc   = DFTSRC_SINC3;
  dsp.DftCfg.DftNum   = DFTNUM_4096;
  dsp.DftCfg.HanWinEn = bFALSE;

  AD5940_DSPCfgS(&dsp);

  /* 常开模拟电源（便于先跑通；后续可用Sequencer省电） */
  AD5940_AFECtrlS(AFECTRL_HSTIAPWR | AFECTRL_INAMPPWR | AFECTRL_EXTBUFPWR |
                  AFECTRL_DACREFPWR | AFECTRL_HSDACPWR | AFECTRL_HPREFPWR,
                  bTRUE);
}

imp2w_status_t IMP2W_50K_CalibrateRcal(uint16_t avg_n)
{
  path_set_rcal();

  uint8_t sat=0;
  cplx_f i = run_dft_avg(avg_n, &sat);
//    LOG_D("[Ical]re=%f im=%f |I|=%f",
//      g_i_cal.re, g_i_cal.im,
//      sqrtf(g_i_cal.re*g_i_cal.re + g_i_cal.im*g_i_cal.im));
  if(sat) return IMP2W_SATURATION;

  if(fabsf(i.re) < 1.0f && fabsf(i.im) < 1.0f) {
    return IMP2W_NUMERIC_ERROR;
  }

  g_i_cal = i;
  g_cal_rcal_ok = 1;
  return IMP2W_OK;
}

imp2w_status_t IMP2W_50K_CalibrateShort(uint16_t avg_n)
{
  if(!g_cal_rcal_ok) return IMP2W_NOT_CALIBRATED;

  /* 外部短接 CE0-SE0 */
  path_set_short_or_meas();

  uint8_t sat=0;
  cplx_f i_short = run_dft_avg(avg_n, &sat);
  if(sat) return IMP2W_SATURATION;

  /* 由电流相量换算出“短路等效阻抗” */
  cplx_f z_short = calc_z_from_i(g_i_cal, i_short, g_cfg.rcal_ohm);

  g_z_short = z_short;
  g_cal_short_ok = 1;
  return IMP2W_OK;
}

imp2w_status_t IMP2W_50K_CalibrateOpen(uint16_t avg_n)
{
  if(!g_cal_rcal_ok) return IMP2W_NOT_CALIBRATED;

  /* 外部断开 CE0-SE0 */
  path_set_short_or_meas();

  /* 统计 Ix_open 幅值均值/方差，用于开路判定阈值 */
  if(avg_n == 0) avg_n = 1;

  float sum=0, sum2=0;
  uint8_t sat=0;
  cplx_f i_open_acc = {0};

  for(uint16_t k=0;k<avg_n;k++){
    cplx_f i = run_dft_once();
    if(is_dft_saturated(i)) sat = 1;
    float m = c_mag(i);
    sum  += m;
    sum2 += m*m;
    i_open_acc.re += i.re;
    i_open_acc.im += i.im;
  }

  if(sat) return IMP2W_SATURATION;

  float mean = sum / (float)avg_n;
  float var  = (sum2/(float)avg_n) - mean*mean;
  if(var < 0) var = 0;
  float std = sqrtf(var);

  g_ix_open_mean = mean;
  g_ix_open_std  = std;
  g_ix_open_th   = mean + g_cfg.open_sigma_k * std;

  /* 同时记录开路等效阻抗（用于Open补偿） */
  cplx_f i_open_avg = { i_open_acc.re/(float)avg_n, i_open_acc.im/(float)avg_n };
  if(fabsf(i_open_avg.re) < 1.0f && fabsf(i_open_avg.im) < 1.0f) {
    /* 极端开路、电流接近0：仍可用开路阈值判定，但Open补偿意义不大 */
    g_cal_open_ok = 0;
    return IMP2W_OK;
  }

  g_z_open = calc_z_from_i(g_i_cal, i_open_avg, g_cfg.rcal_ohm);
  g_cal_open_ok = 1;

  return IMP2W_OK;
}

imp2w_status_t IMP2W_50K_Measure(imp2w_result_t *out)
{
  if(!out) return IMP2W_NUMERIC_ERROR;
  if(!g_cal_rcal_ok) return IMP2W_NOT_CALIBRATED;

  path_set_short_or_meas();

  uint8_t sat=0;
  cplx_f i_x = run_dft_avg(g_cfg.avg_n, &sat);
    
//    LOG_D("[Ix]re=%f im=%f |I|=%f",
//          i_x.re, i_x.im,
//          sqrtf(i_x.re*i_x.re + i_x.im*i_x.im));

//    LOG_D("[Ical]re=%f im=%f |I|=%f",
//          g_i_cal.re, g_i_cal.im,
//          sqrtf(g_i_cal.re*g_i_cal.re + g_i_cal.im*g_i_cal.im));
    
  if(sat) return IMP2W_SATURATION;

  /* 先做“电流幅值”开路粗判，避免 Ix≈0 直接除法爆炸 */
  float ix_mag = c_mag(i_x);
  if(g_ix_open_th > 0.0f && ix_mag <= g_ix_open_th) {
    /* 再加一道阻抗阈值判定（双条件更稳） */
    if(fabsf(i_x.re) < 1.0f && fabsf(i_x.im) < 1.0f) {
      return IMP2W_OPEN_CIRCUIT;
    }
  }

  /* 计算原始阻抗 */
  if(fabsf(i_x.re) < 1.0f && fabsf(i_x.im) < 1.0f) {
    return IMP2W_OPEN_CIRCUIT;
  }
  cplx_f z_meas = calc_z_from_i(g_i_cal, i_x, g_cfg.rcal_ohm);

  /* 若阻抗远超量程，也可判为开路/超量程（防误判） */
  float zmag0 = c_mag(z_meas);
  if(zmag0 >= g_cfg.open_z_min_ohm && ix_mag <= g_ix_open_th) {
    return IMP2W_OPEN_CIRCUIT;
  }

  /* 应用 short/open 补偿 */
  cplx_f z_corr = apply_short_open_comp(z_meas);

  out->z_re = z_corr.re;
  out->z_im = z_corr.im;
  out->z_mag = c_mag(z_corr);
  out->z_phase_rad = c_phase(z_corr);
  

  /* 最后再做一次超量程兜底 */
  if(out->z_mag >= g_cfg.open_z_min_ohm * 5.0f) {
    return IMP2W_OPEN_CIRCUIT;
  }
  
//  LOG_D("z_re = %f, z_im = %f, z_mag = %f, z_phase_rad = %f", out->z_re, out->z_im, out->z_mag, out->z_phase_rad);

  return IMP2W_OK;
}
