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
#include "major_logic.h"
#include "stimer.h"
#include "safety.h"
#include "custom_slave.h"

/* 后续可能需要删除的头文件 */
#include "loop_imp.h"
#include "my_imp.h"

#define  LOG_TAG             "main"
#define  LOG_LVL             4
#include "log.h"

/*------------------------------ Macro definition -----------------------------*/
#define LOW_PIN   (TCA6424_PIN(1, 3))

/*------------------------------ typedef definition ---------------------------*/


/*------------------------------ variables prototypes -------------------------*/



/*------------------------------ function prototypes --------------------------*/

/*------------------------------ application ----------------------------------*/

/**
 * @brief  Main program
 * @param  None
 * @retval None
 */
int main(void)
{
    imp2w_cfg_t cfg;
    imp2w_result_t out;
    
    /* 底层驱动初始化 */
    board_init();
    
    /* 系统服务初始化 */
    stimer_init(HAL_GetTick);
    
    /* 协议应用层初始化 */
    slave_proto_init();
    
    /* 应用主逻辑协调器初始化 */
    major_logic_init();
    
    /* 安全模块初始化 */
    safety_init();
    
    memset(&cfg, 0, sizeof(cfg));

    cfg.sys_clk_hz = 16000000.0f;
    cfg.freq_hz    = 50000.0f;

    cfg.rcal_ohm     = 3000.0f;   /* 典型 */
    cfg.excit_vpp_mv = 200.0f;    /* 30Ω也不容易饱和；500Ω信噪可通过avg提升 */

    cfg.hstia_rtia_sel = HSTIARTIA_10K;
    cfg.hstia_ctia     = 16;      /* 8~16起步 */
    cfg.adc_pga         = ADCPGA_1P5;
    cfg.avg_n = 64;               /* 64次平均很稳 */

    cfg.open_z_min_ohm = 2000.0f; /* 超出即视为超量程/开路 */
    cfg.open_sigma_k   = 5.0f;    /* open阈值=mean+5*std */

    cfg.enable_short_comp = 0;
    cfg.enable_open_comp  = 0;
    
//    IMP2W_50K_Init(&cfg);
//    IMP2W_50K_CalibrateRcal(16);

    AppIMPInit();                   /* Initialize IMP application. Provide a buffer, which is used to store sequencer commands */
    AppIMPCtrl(IMPCTRL_START, 0);   /* Control IMP measurement to start. Second parameter has no meaning with this command. */
    
    while (1)
    {
        safety_task();          /* 安全任务 */
        stimer_service();       /* 软件定时器服务 */
        slave_proto_task();     /* 协议处理任务 */
        major_logic_task();     /* 主逻辑协调任务 */
        AppImpTask();
//        imp2w_status_t st = IMP2W_50K_Measure(&out);
//        if(st == IMP2W_OK) {
//            LOG_D("z_re=%f z_im=%f z_mag=%f phase=%f", out.z_re, out.z_im, out.z_mag, out.z_phase_rad);
//        }
//        HAL_Delay(10);
    }
}

/******************************* End Of File ************************************/


