/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for s5e3830 SoC.
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <soc/samsung/cal-if.h>
#include <dt-bindings/clock/s5e3830.h>

#include "../../soc/samsung/cal-if/s5e3830/cmucal-vclk.h"
#include "../../soc/samsung/cal-if/s5e3830/cmucal-node.h"
#include "../../soc/samsung/cal-if/s5e3830/cmucal-qch.h"
#include "../../soc/samsung/cal-if/s5e3830/clkout_s5e3830.h"
#include "composite.h"

static struct samsung_clk_provider *s5e3830_clk_provider;
/*
 * list of controller registers to be saved and restored during a
 * suspend/resume cycle.
 */
/* fixed rate clocks generated outside the soc */
struct samsung_fixed_rate s5e3830_fixed_rate_ext_clks[] = {
	FRATE(OSCCLK, "fin_pll", NULL, 0, 26000000),
};

/* HWACG VCLK */
struct init_vclk s5e3830_apm_hwacg_vclks[] = {
	HWACG_VCLK(UMUX_DLL_USER, MUX_DLL_USER, "UMUX_DLL_USER", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(UMUX_CLK_APM_BUS, MUX_CLK_APM_BUS, "UMUX_CLK_APM_BUS", NULL, 0, VCLK_GATE, NULL),

	HWACG_VCLK(GATE_APM_CMU_APM_QCH, APM_CMU_APM_QCH, "GATE_APM_CMU_APM_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_GREBEINTEGRATION_QCH_GREBE, GREBEINTEGRATION_QCH_GREBE, "GATE_GREBEINTEGRATION_QCH_GREBE", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_GREBEINTEGRATION_QCH_DBG, GREBEINTEGRATION_QCH_DBG, "GATE_GREBEINTEGRATION_QCH_DBG", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_I3C_APM_PMIC_QCH_S_I3C, I3C_APM_PMIC_QCH_S_I3C, "GATE_I3C_APM_PMIC_QCH_S_I3C", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_I3C_APM_PMIC_QCH, I3C_APM_PMIC_QCH, "GATE_I3C_APM_PMIC_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_INTMEM_QCH, INTMEM_QCH, "GATE_INTMEM_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_APM_AP_QCH, MAILBOX_APM_AP_QCH, "GATE_MAILBOX_APM_AP_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_APM_CHUB_QCH, MAILBOX_APM_CHUB_QCH, "GATE_MAILBOX_APM_CHUB_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_APM_CP_QCH, MAILBOX_APM_CP_QCH, "GATE_MAILBOX_APM_CP_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_APM_GNSS_QCH, MAILBOX_APM_GNSS_QCH, "GATE_MAILBOX_APM_GNSS_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_APM_WLBT_QCH, MAILBOX_APM_WLBT_QCH, "GATE_MAILBOX_APM_WLBT_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_AP_CHUB_QCH, MAILBOX_AP_CHUB_QCH, "GATE_MAILBOX_AP_CHUB_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_AP_CP_QCH, MAILBOX_AP_CP_QCH, "GATE_MAILBOX_AP_CP_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_AP_CP_S_QCH, MAILBOX_AP_CP_S_QCH, "GATE_MAILBOX_AP_CP_S_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_AP_GNSS_QCH, MAILBOX_AP_GNSS_QCH, "GATE_MAILBOX_AP_GNSS_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_AP_WLBT_QCH, MAILBOX_AP_WLBT_QCH, "GATE_MAILBOX_AP_WLBT_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_CP_CHUB_QCH, MAILBOX_CP_CHUB_QCH, "GATE_MAILBOX_CP_CHUB_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_CP_GNSS_QCH, MAILBOX_CP_GNSS_QCH, "GATE_MAILBOX_CP_GNSS_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_CP_WLBT_QCH, MAILBOX_CP_WLBT_QCH, "GATE_MAILBOX_CP_WLBT_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_GNSS_CHUB_QCH, MAILBOX_GNSS_CHUB_QCH, "GATE_MAILBOX_GNSS_CHUB_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_GNSS_WLBT_QCH, MAILBOX_GNSS_WLBT_QCH, "GATE_MAILBOX_GNSS_WLBT_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_WLBT_ABOX_QCH, MAILBOX_WLBT_ABOX_QCH, "GATE_MAILBOX_WLBT_ABOX_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MAILBOX_WLBT_CHUB_QCH, MAILBOX_WLBT_CHUB_QCH, "GATE_MAILBOX_WLBT_CHUB_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_PMU_INTR_GEN_QCH, PMU_INTR_GEN_QCH, "GATE_PMU_INTR_GEN_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_ROM_CRC32_HOST_QCH, ROM_CRC32_HOST_QCH, "GATE_ROM_CRC32_HOST_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_SPEEDY_APM_QCH, SPEEDY_APM_QCH, "GATE_SPEEDY_APM_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_WDT_APM_QCH, WDT_APM_QCH, "GATE_WDT_APM_QCH", NULL, 0, VCLK_GATE, NULL),
};

struct init_vclk s5e3830_aud_hwacg_vclks[] = {
	HWACG_VCLK(UMUX_CLK_AUD_CPU_HCH, MUX_CLK_AUD_CPU_HCH, "UMUX_CLK_AUD_CPU_HCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(UMUX_CLK_AUD_FM, MUX_CLK_AUD_FM, "UMUX_CLK_AUD_FM", NULL, 0, VCLK_GATE, NULL),

	HWACG_VCLK(GATE_ABOX_QCH_CPU, ABOX_QCH_CPU, "GATE_ABOX_QCH_CPU", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_ABOX_QCH_ACLK, ABOX_QCH_ACLK, "GATE_ABOX_QCH_ACLK", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_ABOX_QCH_BCLK0, ABOX_QCH_BCLK0, "GATE_ABOX_QCH_BCLK0", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_ABOX_QCH_BCLK1, ABOX_QCH_BCLK1, "GATE_ABOX_QCH_BCLK1", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_ABOX_QCH_FM, ABOX_QCH_FM, "GATE_ABOX_QCH_FM", "UMUX_CLK_AUD_FM", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_ABOX_QCH_BCLK2, ABOX_QCH_BCLK2, "GATE_ABOX_QCH_BCLK2", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_ABOX_QCH_CCLK_ASB, ABOX_QCH_CCLK_ASB, "GATE_ABOX_QCH_CCLK_ASB", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_ABOX_QCH_BCLK3, ABOX_QCH_BCLK3, "GATE_ABOX_QCH_BCLK3", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_ABOX_QCH_BCLK4, ABOX_QCH_BCLK4, "GATE_ABOX_QCH_BCLK4", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_ABOX_QCH_BCLK5, ABOX_QCH_BCLK5, "GATE_ABOX_QCH_BCLK5", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_ABOX_QCH_BCLK6, ABOX_QCH_BCLK6, "GATE_ABOX_QCH_BCLK6", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_ABOX_QCH_BCLK_CNT, ABOX_QCH_BCLK_CNT, "GATE_ABOX_QCH_BCLK_CNT", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_AUD_CMU_AUD_QCH, AUD_CMU_AUD_QCH, "GATE_AUD_CMU_AUD_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_GPIO_AUD_QCH, GPIO_AUD_QCH, "GATE_GPIO_AUD_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_SYSMMU_AUD_QCH_S1, SYSMMU_AUD_QCH_S1, "GATE_SYSMMU_AUD_QCH_S1", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_WDT_AUD_QCH, WDT_AUD_QCH, "GATE_WDT_AUD_QCH", NULL, 0, VCLK_GATE, NULL),
};

struct init_vclk s5e3830_chub_hwacg_vclks[] = {
	HWACG_VCLK(GATE_BAAW_C_CHUB_QCH, BAAW_C_CHUB_QCH, "GATE_BAAW_C_CHUB_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_BAAW_D_CHUB_QCH, BAAW_D_CHUB_QCH, "GATE_BAAW_D_CHUB_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_CHUB_CMU_CHUB_QCH, CHUB_CMU_CHUB_QCH, "GATE_CHUB_CMU_CHUB_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_CM4_CHUB_QCH, CM4_CHUB_QCH, "GATE_CM4_CHUB_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_DMIC_AHB0_QCH, DMIC_AHB0_QCH, "GATE_DMIC_AHB0_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_DMIC_IF_QCH_PCLK, DMIC_IF_QCH_PCLK, "GATE_DMIC_IF_QCH_PCLK", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_DMIC_IF_QCH_DMIC_CLK, DMIC_IF_QCH_DMIC_CLK, "GATE_DMIC_IF_QCH_DMIC_CLK", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_HWACG_SYS_DMIC0_QCH, HWACG_SYS_DMIC0_QCH, "GATE_HWACG_SYS_DMIC0_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_PWM_CHUB_QCH, PWM_CHUB_QCH, "GATE_PWM_CHUB_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_SWEEPER_C_CHUB_QCH, SWEEPER_C_CHUB_QCH, "GATE_SWEEPER_C_CHUB_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_SWEEPER_D_CHUB_QCH, SWEEPER_D_CHUB_QCH, "GATE_SWEEPER_D_CHUB_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_TIMER_CHUB_QCH, TIMER_CHUB_QCH, "GATE_TIMER_CHUB_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_WDT_CHUB_QCH, WDT_CHUB_QCH, "GATE_WDT_CHUB_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_U_DMIC_CLK_SCAN_MUX_QCH, U_DMIC_CLK_SCAN_MUX_QCH, "GATE_U_DMIC_CLK_SCAN_MUX_QCH", NULL, 0, VCLK_GATE, NULL),
};

struct init_vclk s5e3830_cmgp_hwacg_vclks[] = {
	HWACG_VCLK(UMUX_CLK_CMGP_USI_CMGP0, MUX_CLK_CMGP_USI_CMGP0, "UMUX_CLK_CMGP_USI_CMGP0", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(UMUX_CLK_CMGP_USI_CMGP1, MUX_CLK_CMGP_USI_CMGP1, "UMUX_CLK_CMGP_USI_CMGP1", NULL, 0, VCLK_GATE, NULL),

	HWACG_VCLK(GATE_ADC_CMGP_QCH_S0, ADC_CMGP_QCH_S0, "GATE_ADC_CMGP_QCH_S0", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_ADC_CMGP_QCH_S1, ADC_CMGP_QCH_S1, "GATE_ADC_CMGP_QCH_S1", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_ADC_CMGP_QCH_ADC, ADC_CMGP_QCH_ADC, "GATE_ADC_CMGP_QCH_ADC", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_CMGP_CMU_CMGP_QCH, CMGP_CMU_CMGP_QCH, "GATE_CMGP_CMU_CMGP_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_GPIO_CMGP_QCH, GPIO_CMGP_QCH, "GATE_GPIO_CMGP_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_USI_CMGP0_QCH, USI_CMGP0_QCH, "GATE_USI_CMGP0_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_USI_CMGP1_QCH, USI_CMGP1_QCH, "GATE_USI_CMGP1_QCH", NULL, 0, VCLK_GATE, NULL),
};

struct init_vclk s5e3830_top_hwacg_vclks[] = {
	HWACG_VCLK(GATE_CMU_TOP_CMUREF_QCH, CMU_TOP_CMUREF_QCH, "GATE_CMU_TOP_CMUREF_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_DFTMUX_CMU_QCH_CLK_CIS0, DFTMUX_CMU_QCH_CLK_CIS0, "GATE_DFTMUX_CMU_QCH_CLK_CIS0", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_DFTMUX_CMU_QCH_CLK_CIS1, DFTMUX_CMU_QCH_CLK_CIS1, "GATE_DFTMUX_CMU_QCH_CLK_CIS1", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_DFTMUX_CMU_QCH_CLK_CIS2, DFTMUX_CMU_QCH_CLK_CIS2, "GATE_DFTMUX_CMU_QCH_CLK_CIS2", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_OTP_QCH, OTP_QCH, "GATE_OTP_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_ADM_AHB_SSS_QCH, ADM_AHB_SSS_QCH, "GATE_ADM_AHB_SSS_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_BAAW_P_CHUB_QCH, BAAW_P_CHUB_QCH, "GATE_BAAW_P_CHUB_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_BAAW_P_GNSS_QCH, BAAW_P_GNSS_QCH, "GATE_BAAW_P_GNSS_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_BAAW_P_MODEM_QCH, BAAW_P_MODEM_QCH, "GATE_BAAW_P_MODEM_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_BAAW_P_WLBT_QCH, BAAW_P_WLBT_QCH, "GATE_BAAW_P_WLBT_QCH", NULL, 0, VCLK_GATE, NULL),
};

struct init_vclk s5e3830_core_hwacg_vclks[] = {
	HWACG_VCLK(GATE_CCI_550_QCH, CCI_550_QCH, "GATE_CCI_550_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_CORE_CMU_CORE_QCH, CORE_CMU_CORE_QCH, "GATE_CORE_CMU_CORE_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_GIC_QCH, GIC_QCH, "GATE_GIC_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_GPIO_CORE_QCH, GPIO_CORE_QCH, "GATE_GPIO_CORE_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MMC_EMBD_QCH, MMC_EMBD_QCH, "GATE_MMC_EMBD_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_PDMA_CORE_QCH, PDMA_CORE_QCH, "GATE_PDMA_CORE_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_RTIC_QCH, RTIC_QCH, "GATE_RTIC_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_SPDMA_CORE_QCH, SPDMA_CORE_QCH, "GATE_SPDMA_CORE_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_SSS_QCH, SSS_QCH, "GATE_SSS_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_TREX_D_CORE_QCH, TREX_D_CORE_QCH, "GATE_TREX_D_CORE_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_TREX_P_CORE_QCH, TREX_P_CORE_QCH, "GATE_TREX_P_CORE_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_CSSYS_DBG_QCH, CSSYS_DBG_QCH, "GATE_CSSYS_DBG_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_SECJTAG_QCH, SECJTAG_QCH, "GATE_SECJTAG_QCH", NULL, 0, VCLK_GATE, NULL),
};

struct init_vclk s5e3830_dpu_hwacg_vclks[] = {
	HWACG_VCLK(GATE_DPU_QCH_S_DPP, DPU_QCH_S_DPP, "GATE_DPU_QCH_S_DPP", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_DPU_QCH_S_DMA, DPU_QCH_S_DMA, "GATE_DPU_QCH_S_DMA", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_DPU_QCH_S_DECON, DPU_QCH_S_DECON, "GATE_DPU_QCH_S_DECON", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_DPU_CMU_DPU_QCH, DPU_CMU_DPU_QCH, "GATE_DPU_CMU_DPU_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_SMMU_DPU_QCH, SMMU_DPU_QCH, "GATE_SMMU_DPU_QCH", NULL, 0, VCLK_GATE, NULL),
};

struct init_vclk s5e3830_g3d_hwacg_vclks[] = {
	HWACG_VCLK(GATE_G3D_CMU_G3D_QCH, G3D_CMU_G3D_QCH, "GATE_G3D_CMU_G3D_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_GPU_QCH, GPU_QCH, "GATE_GPU_QCH", NULL, 0, VCLK_GATE, NULL),
};

struct init_vclk s5e3830_hsi_hwacg_vclks[] = {
	HWACG_VCLK(GATE_GPIO_HSI_QCH, GPIO_HSI_QCH, "GATE_GPIO_HSI_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_HSI_CMU_HSI_QCH, HSI_CMU_HSI_QCH, "GATE_HSI_CMU_HSI_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MMC_CARD_QCH, MMC_CARD_QCH, "GATE_MMC_CARD_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_USB20DRD_TOP_QCH_LINK, USB20DRD_TOP_QCH_LINK, "GATE_USB20DRD_TOP_QCH_LINK", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_USB20DRD_TOP_QCH_20CTRL, USB20DRD_TOP_QCH_20CTRL, "GATE_USB20DRD_TOP_QCH_20CTRL", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_USB20DRD_TOP_QCH_REFCLK, USB20DRD_TOP_QCH_REFCLK, "GATE_USB20DRD_TOP_QCH_REFCLK", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_USB20DRD_TOP_QCH_RTC, USB20DRD_TOP_QCH_RTC, "GATE_USB20DRD_TOP_QCH_RTC", NULL, 0, VCLK_GATE, NULL),
};

struct init_vclk s5e3830_is_hwacg_vclks[] = {
	HWACG_VCLK(UMUX_CLK_IS_BUS, MUX_CLKCMU_IS_BUS_USER, "UMUX_CLK_IS_BUS", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(UMUX_CLK_IS_ITP, MUX_CLKCMU_IS_ITP_USER, "UMUX_CLK_IS_ITP", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(UMUX_CLK_IS_VRA, MUX_CLKCMU_IS_VRA_USER, "UMUX_CLK_IS_VRA", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(UMUX_CLK_IS_GDC, MUX_CLKCMU_IS_GDC_USER, "UMUX_CLK_IS_GDC", NULL, 0, VCLK_GATE, NULL),

	HWACG_VCLK(GATE_CSIS0_QCH, CSIS0_QCH, "GATE_CSIS0_QCH", "UMUX_CLK_IS_BUS", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_CSIS1_QCH, CSIS1_QCH, "GATE_CSIS1_QCH", "UMUX_CLK_IS_BUS", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_CSIS2_QCH, CSIS2_QCH, "GATE_CSIS2_QCH", "UMUX_CLK_IS_BUS", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_IS_CMU_IS_QCH, IS_CMU_IS_QCH, "GATE_IS_CMU_IS_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_IS_TOP_QCH_S_00, IS_TOP_QCH_S_00, "GATE_IS_TOP_QCH_S_00", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_IS_TOP_QCH_S_02, IS_TOP_QCH_S_02, "GATE_IS_TOP_QCH_S_02", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_IS_TOP_QCH_S_03, IS_TOP_QCH_S_03, "GATE_IS_TOP_QCH_S_03", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_IS_TOP_QCH_S_04, IS_TOP_QCH_S_04, "GATE_IS_TOP_QCH_S_04", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_IS_TOP_QCH_S_05, IS_TOP_QCH_S_05, "GATE_IS_TOP_QCH_S_05", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_IS_TOP_QCH_S_06, IS_TOP_QCH_S_06, "GATE_IS_TOP_QCH_S_06", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_SYSMMU_IS0_QCH, SYSMMU_IS0_QCH, "GATE_SYSMMU_IS0_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_SYSMMU_IS1_QCH, SYSMMU_IS1_QCH, "GATE_SYSMMU_IS1_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_CLK_ITP, IS_TOP_QCH_S_03, "GATE_CLK_ITP", "UMUX_CLK_IS_ITP", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_CLK_VRA, IS_TOP_QCH_S_05, "GATE_CLK_VRA", "UMUX_CLK_IS_VRA", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_CLK_GDC, IS_TOP_QCH_S_06, "GATE_CLK_GDC", "UMUX_CLK_IS_GDC", 0, VCLK_GATE, NULL),

};

struct init_vclk s5e3830_mfcmscl_hwacg_vclks[] = {
	HWACG_VCLK(UMUX_CLKCMU_MFCMSCL_MFC, MUX_CLKCMU_MFCMSCL_MFC,"UMUX_CLKCMU_MFCMSCL_MFC", NULL, 0, 0, NULL),
	HWACG_VCLK(UMUX_CLKCMU_MFCMSCL_M2M, MUX_CLKCMU_MFCMSCL_M2M,"UMUX_CLKCMU_MFCMSCL_M2M", "UMUX_CLKCMU_MFCMSCL_MFC", 0, 0, NULL),
	HWACG_VCLK(UMUX_CLKCMU_MFCMSCL_MCSC, MUX_CLKCMU_MFCMSCL_MCSC,"UMUX_CLKCMU_MFCMSCL_MCSC", "UMUX_CLKCMU_MFCMSCL_MFC", 0, 0, NULL),
	HWACG_VCLK(UMUX_CLKCMU_MFCMSCL_JPEG, MUX_CLKCMU_MFCMSCL_JPEG,"UMUX_CLKCMU_MFCMSCL_JPEG", "UMUX_CLKCMU_MFCMSCL_MFC", 0, 0, NULL),

	HWACG_VCLK(GATE_JPEG_QCH, JPEG_QCH, "GATE_JPEG_QCH", "UMUX_CLKCMU_MFCMSCL_JPEG", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_M2M_QCH, M2M_QCH, "GATE_M2M_QCH", "UMUX_CLKCMU_MFCMSCL_M2M", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MCSC_QCH, MCSC_QCH, "GATE_MCSC_QCH", "UMUX_CLKCMU_MFCMSCL_MCSC", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MFC_QCH, MFC_QCH, "GATE_MFC_QCH", "UMUS_CLKCMU_MFCMSCL_MFC", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MFCMSCL_CMU_MFCMSCL_QCH, MFCMSCL_CMU_MFCMSCL_QCH, "GATE_MFCMSCL_CMU_MFCMSCL_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_SYSMMU_MFCMSCL_QCH, SYSMMU_MFCMSCL_QCH, "GATE_SYSMMU_MFCMSCL_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_CMU_MIF_CMUREF_QCH, CMU_MIF_CMUREF_QCH, "GATE_CMU_MIF_CMUREF_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_DMC_QCH, DMC_QCH, "GATE_DMC_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MIF_CMU_MIF_QCH, MIF_CMU_MIF_QCH, "GATE_MIF_CMU_MIF_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_CMU_MIF1_CMU_REF_QCH, CMU_MIF1_CMU_REF_QCH, "GATE_CMU_MIF1_CMU_REF_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_DMC1_QCH, DMC1_QCH, "GATE_DMC1_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MIF1_CMU_MIF1_QCH, MIF1_CMU_MIF1_QCH, "GATE_MIF1_CMU_MIF1_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MODEM_CMU_MODEM_QCH, MODEM_CMU_MODEM_QCH, "GATE_MODEM_CMU_MODEM_QCH", NULL, 0, VCLK_GATE, NULL),
};

struct init_vclk s5e3830_peri_hwacg_vclks[] = {
	HWACG_VCLK(UMUX_CLKCMU_PERI_BUS_USER, MUX_CLKCMU_PERI_BUS_USER, "UMUX_CLKCMU_PERI_BUS_USER", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(UMUX_CLKCMU_PERI_UART_USER, MUX_CLKCMU_PERI_UART_USER, "UMUX_CLKCMU_PERI_UART_USER", NULL, 0, VCLK_GATE, NULL),

	HWACG_VCLK(UMUX_CLKCMU_PERI_HSI2C_USER, MUX_CLKCMU_PERI_HSI2C_USER, "UMUX_CLKCMU_PERI_HSI2C_USER", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(UMUX_CLKCMU_PERI_SPI_USER, MUX_CLKCMU_PERI_SPI_USER, "UMUX_CLKCMU_PERI_SPI_USER", NULL, 0, VCLK_GATE, NULL),

	HWACG_VCLK(GATE_BUSIF_TMU_QCH, BUSIF_TMU_QCH, "GATE_BUSIF_TMU_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_GPIO_PERI_QCH, GPIO_PERI_QCH, "GATE_GPIO_PERI_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_HSI2C_0_QCH, HSI2C_0_QCH, "GATE_HSI2C_0_QCH", "UMUX_CLKCMU_PERI_HSI2C_USER", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_HSI2C_1_QCH, HSI2C_1_QCH, "GATE_HSI2C_1_QCH", "UMUX_CLKCMU_PERI_HSI2C_USER", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_HSI2C_2_QCH, HSI2C_2_QCH, "GATE_HSI2C_2_QCH", "UMUX_CLKCMU_PERI_HSI2C_USER", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_I2C_0_QCH, I2C_0_QCH, "GATE_I2C_0_QCH", "UMUX_CLKCMU_PERI_BUS_USER", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_I2C_1_QCH, I2C_1_QCH, "GATE_I2C_1_QCH", "UMUX_CLKCMU_PERI_BUS_USER", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_I2C_2_QCH, I2C_2_QCH, "GATE_I2C_2_QCH", "UMUX_CLKCMU_PERI_BUS_USER", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_I2C_3_QCH, I2C_3_QCH, "GATE_I2C_3_QCH", "UMUX_CLKCMU_PERI_BUS_USER", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_I2C_4_QCH, I2C_4_QCH, "GATE_I2C_4_QCH", "UMUX_CLKCMU_PERI_BUS_USER", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_I2C_5_QCH, I2C_5_QCH, "GATE_I2C_5_QCH", "UMUX_CLKCMU_PERI_BUS_USER", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_I2C_6_QCH, I2C_6_QCH, "GATE_I2C_6_QCH", "UMUX_CLKCMU_PERI_BUS_USER", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_MCT_QCH, MCT_QCH, "GATE_MCT_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_OTP_CON_TOP_QCH, OTP_CON_TOP_QCH, "GATE_OTP_CON_TOP_QCH", NULL, 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_PWM_MOTOR_QCH, PWM_MOTOR_QCH, "GATE_PWM_MOTOR_QCH", "UMUX_CLKCMU_PERI_BUS_USER", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_SPI_0_QCH, SPI_0_QCH, "GATE_SPI_0_QCH", "UMUX_CLKCMU_PERI_SPI_USER", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_UART_QCH, UART_QCH, "GATE_UART_QCH", "UMUX_CLKCMU_PERI_UART_USER", 0, VCLK_GATE, "console-pclk0"),
	HWACG_VCLK(GATE_WDT_0_QCH, WDT_0_QCH, "GATE_WDT_0_QCH", "UMUX_CLKCMU_PERI_BUS_USER", 0, VCLK_GATE, NULL),
	HWACG_VCLK(GATE_WDT_1_QCH, WDT_1_QCH, "GATE_WDT_1_QCH", "UMUX_CLKCMU_PERI_BUS_USER", 0, VCLK_GATE, NULL),
};


/* Special VCLK */
struct init_vclk s5e3830_aud_vclks[] = {
	VCLK(PLL_AUD_OUT, PLL_AUD, "PLL_AUD_OUT", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_CPU, DIV_CLK_AUD_CPU, "DOUT_CLK_AUD_CPU", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_CPU_ACLK, DIV_CLK_AUD_CPU_ACLK, "DOUT_CLK_AUD_CPU_ACLK", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_CPU_PCLKDBG, DIV_CLK_AUD_CPU_PCLKDBG, "DOUT_CLK_AUD_CPU_PCLKDBG", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_BUSD, DIV_CLK_AUD_BUSD, "DOUT_CLK_AUD_BUSD", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_UAIF0, DIV_CLK_AUD_UAIF0, "DOUT_CLK_AUD_UAIF0", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_UAIF1, DIV_CLK_AUD_UAIF1, "DOUT_CLK_AUD_UAIF1", 0, 0, NULL),
	VCLK(MOUT_CLK_AUD_UAIF1, MUX_CLK_AUD_UAIF1, "MOUT_CLK_AUD_UAIF1", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_FM, DIV_CLK_AUD_FM, "DOUT_CLK_AUD_FM", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_BUSP, DIV_CLK_AUD_BUSP, "DOUT_CLK_AUD_BUSP", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_UAIF2, DIV_CLK_AUD_UAIF2, "DOUT_CLK_AUD_UAIF2", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_CNT, DIV_CLK_AUD_CNT, "DOUT_CLK_AUD_CNT", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_UAIF3, DIV_CLK_AUD_UAIF3, "DOUT_CLK_AUD_UAIF3", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_UAIF4, DIV_CLK_AUD_UAIF4, "DOUT_CLK_AUD_UAIF4", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_UAIF5, DIV_CLK_AUD_UAIF5, "DOUT_CLK_AUD_UAIF5", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_UAIF6, DIV_CLK_AUD_UAIF6, "DOUT_CLK_AUD_UAIF6", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_AUDIF, DIV_CLK_AUD_AUDIF, "DOUT_CLK_AUD_AUDIF", 0, 0, NULL),
	VCLK(DOUT_CLK_AUD_MCLK, CLK_AUD_MCLK, "DOUT_CLK_AUD_MCLK", 0, 0, NULL),
};

struct init_vclk s5e3830_chub_vclks[] = {
	VCLK(DOUT_CLK_CHUB_BUS, DIV_CLK_CHUB_BUS, "DOUT_CLK_CHUB_BUS", 0, 0, NULL),
	VCLK(DOUT_CLK_CHUB_DMIC_IF, DIV_CLK_CHUB_DMIC_IF, "DOUT_CLK_CHUB_DMIC_IF", 0, 0, NULL),
	VCLK(DOUT_CLK_CHUB_DMIC_IF_DIV2, DIV_CLK_CHUB_DMIC_IF_DIV2, "DOUT_CLK_CHUB_DMIC_IF_DIV2", 0, 0, NULL),
	VCLK(DOUT_CLK_CHUB_DMIC, DIV_CLK_CHUB_DMIC, "DOUT_CLK_CHUB_DMIC", 0, 0, NULL),
};

struct init_vclk s5e3830_cmgp_vclks[] = {
	VCLK(DOUT_CLK_CMGP_ADC, DIV_CLK_CMGP_ADC, "DOUT_CLK_CMGP_ADC", 0, 0, NULL),
	VCLK(DOUT_CLK_CMGP_USI_CMGP0, DIV_CLK_CMGP_USI_CMGP0, "DOUT_CLK_CMGP_USI_CMGP0", 0, 0, NULL),
	VCLK(DOUT_CLK_CMGP_USI_CMGP1, DIV_CLK_CMGP_USI_CMGP1, "DOUT_CLK_CMGP_USI_CMGP1", 0, 0, NULL),
        VCLK(DOUT_VCLK_CMGP_USI_CMGP0, VCLK_DIV_CLK_CMGP_USI_CMGP0, "DOUT_VCLK_CMGP_USI_CMGP0", 0, 0, NULL),
        VCLK(DOUT_VCLK_CMGP_USI_CMGP1, VCLK_DIV_CLK_CMGP_USI_CMGP1, "DOUT_VCLK_CMGP_USI_CMGP1", 0, 0, NULL),
};

struct init_vclk s5e3830_core_vclks[] = {
	VCLK(DOUT_CORE_MMC_EMBD, CLKCMU_CORE_MMC_EMBD, "DOUT_CORE_MMC_EMBD", 0, 0, NULL),
};

struct init_vclk s5e3830_dpu_vclks[] = {
	VCLK(DOUT_CLK_DPU_BUSP, DIV_CLK_DPU_BUSP, "DOUT_CLK_DPU_BUSP", 0, 0, NULL),
};

struct init_vclk s5e3830_g3d_vclks[] = {
	VCLK(DOUT_CLK_G3D_BUSP, DIV_CLK_G3D_BUSP, "DOUT_CLK_G3D_BUSP", 0, 0, NULL),
};

struct init_vclk s5e3830_hsi_vclks[] = {
	VCLK(PLL_MMC_OUT, PLL_MMC, "PLL_MMC_OUT", 0, 0, NULL),
	VCLK(HSI_BUS, CLKCMU_HSI_BUS, "HSI_BUS", 0, 0, NULL),
	VCLK(HSI_MMC_CARD, CLKCMU_HSI_MMC_CARD, "HSI_MMC_CARD", 0, 0, NULL),
	VCLK(HSI_USB20DRD, CLKCMU_HSI_USB20DRD, "HSI_USB20DRD", 0, 0, NULL),
};

struct init_vclk s5e3830_is_vclks[] = {
	VCLK(IS_BUS, CLKCMU_IS_BUS, "IS_BUS", 0, 0, NULL),
	VCLK(IS_VRA, CLKCMU_IS_VRA, "IS_VRA", 0, 0, NULL),
	VCLK(IS_ITP, CLKCMU_IS_ITP, "IS_ITP", 0, 0, NULL),
	VCLK(IS_GDC, CLKCMU_IS_GDC, "IS_GDC", 0, 0, NULL),
	VCLK(CIS_CLK0, CLKCMU_CIS_CLK0, "CIS_CLK0", 0, 0, NULL),
	VCLK(CIS_CLK1, CLKCMU_CIS_CLK1, "CIS_CLK1", 0, 0, NULL),
	VCLK(CIS_CLK2, CLKCMU_CIS_CLK2, "CIS_CLK2", 0, 0, NULL),
};

struct init_vclk s5e3830_mfcmscl_vclks[] = {
	VCLK(DOUT_CLK_MFCMSCL_BUSP, DIV_CLK_MFCMSCL_BUSP, "DOUT_CLK_MFCMSCL_BUSP", 0, 0, NULL),
	VCLK(MFCMSCL_MFC, CLKCMU_MFCMSCL_MFC, "MFCMSCL_MFC", 0, 0, NULL),
	VCLK(MFCMSCL_M2M, CLKCMU_MFCMSCL_M2M, "MFCMSCL_M2M", 0, 0, NULL),
	VCLK(MFCMSCL_MCSC, CLKCMU_MFCMSCL_MCSC, "MFCMSCL_MCSC", 0, 0, NULL),
	VCLK(MFCMSCL_JPEG, CLKCMU_MFCMSCL_JPEG, "MFCMSCL_JPEG", 0, 0, NULL),
};

struct init_vclk s5e3830_peri_vclks[] = {
	VCLK(DOUT_CLK_PERI_SPI_0, DIV_CLK_PERI_SPI_0, "DOUT_CLK_PERI_SPI_0", 0, 0, NULL),
	VCLK(DOUT_CLK_PERI_HSI2C_0, DIV_CLK_PERI_HSI2C_0, "DOUT_CLK_PERI_HSI2C_0", 0, 0, NULL),
	VCLK(DOUT_CLK_PERI_HSI2C_1, DIV_CLK_PERI_HSI2C_1, "DOUT_CLK_PERI_HSI2C_1", 0, 0, NULL),
	VCLK(DOUT_CLK_PERI_HSI2C_2, DIV_CLK_PERI_HSI2C_2, "DOUT_CLK_PERI_HSI2C_2", 0, 0, NULL),
	VCLK(DOUT_I2C_0, CLKCMU_PERI_BUS, "DOUT_I2C_0", 0, 0, NULL),
	VCLK(DOUT_I2C_1, CLKCMU_PERI_BUS, "DOUT_I2C_1", 0, 0, NULL),
	VCLK(DOUT_I2C_2, CLKCMU_PERI_BUS, "DOUT_I2C_2", 0, 0, NULL),
	VCLK(DOUT_I2C_3, CLKCMU_PERI_BUS, "DOUT_I2C_3", 0, 0, NULL),
	VCLK(DOUT_I2C_4, CLKCMU_PERI_BUS, "DOUT_I2C_4", 0, 0, NULL),
	VCLK(DOUT_I2C_5, CLKCMU_PERI_BUS, "DOUT_I2C_5", 0, 0, NULL),
	VCLK(DOUT_I2C_6, CLKCMU_PERI_BUS, "DOUT_I2C_6", 0, 0, NULL),
	VCLK(DOUT_UART, CLKCMU_PERI_UART, "DOUT_UART", 0, 0, "console-sclk0"),
};

static struct init_vclk s5e3830_clkout_vclks[] = {
	VCLK(OSC_NFC, VCLK_CLKOUT0, "OSC_NFC", 0, 0, NULL),
	/*VCLK(OSC_AUD, VCLK_CLKOUT0, "OSC_AUD", 0, 0, NULL),*/
};

static struct of_device_id ext_clk_match[] = {
	{.compatible = "samsung,s5e3830-oscclk", .data = (void *)0},
	{},
};

void s5e3830_vclk_init(void)
{
	/* Common clock init */
}

/* register s5e3830 clocks */
static int s5e3830_clock_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	void __iomem *reg_base;

	if (np) {
		reg_base = of_iomap(np, 0);
		if (!reg_base)
			panic("%s: failed to map registers\n", __func__);
	} else {
		panic("%s: unable to determine soc\n", __func__);
	}
#if 0
	ret = cal_if_init(np);
	if (ret)
		panic("%s: unable to initialize cal-if\n", __func__);
#endif
	s5e3830_clk_provider = samsung_clk_init(np, reg_base, CLK_NR_CLKS);
	if (!s5e3830_clk_provider)
		panic("%s: unable to allocate context.\n", __func__);

	samsung_register_of_fixed_ext(s5e3830_clk_provider, s5e3830_fixed_rate_ext_clks,
					  ARRAY_SIZE(s5e3830_fixed_rate_ext_clks),
					  ext_clk_match);

	/* register HWACG vclk */
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_apm_hwacg_vclks, ARRAY_SIZE(s5e3830_apm_hwacg_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_aud_hwacg_vclks, ARRAY_SIZE(s5e3830_aud_hwacg_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_chub_hwacg_vclks, ARRAY_SIZE(s5e3830_chub_hwacg_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_cmgp_hwacg_vclks, ARRAY_SIZE(s5e3830_cmgp_hwacg_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_core_hwacg_vclks, ARRAY_SIZE(s5e3830_core_hwacg_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_top_hwacg_vclks, ARRAY_SIZE(s5e3830_top_hwacg_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_dpu_hwacg_vclks, ARRAY_SIZE(s5e3830_dpu_hwacg_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_g3d_hwacg_vclks, ARRAY_SIZE(s5e3830_g3d_hwacg_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_hsi_hwacg_vclks, ARRAY_SIZE(s5e3830_hsi_hwacg_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_is_hwacg_vclks, ARRAY_SIZE(s5e3830_is_hwacg_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_mfcmscl_hwacg_vclks, ARRAY_SIZE(s5e3830_mfcmscl_hwacg_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_peri_hwacg_vclks, ARRAY_SIZE(s5e3830_peri_hwacg_vclks));
	/* register special vclk */
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_aud_vclks, ARRAY_SIZE(s5e3830_aud_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_chub_vclks, ARRAY_SIZE(s5e3830_chub_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_cmgp_vclks, ARRAY_SIZE(s5e3830_cmgp_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_core_vclks, ARRAY_SIZE(s5e3830_core_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_dpu_vclks, ARRAY_SIZE(s5e3830_dpu_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_g3d_vclks, ARRAY_SIZE(s5e3830_g3d_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_hsi_vclks, ARRAY_SIZE(s5e3830_hsi_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_is_vclks, ARRAY_SIZE(s5e3830_is_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_mfcmscl_vclks, ARRAY_SIZE(s5e3830_mfcmscl_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_peri_vclks, ARRAY_SIZE(s5e3830_peri_vclks));
	samsung_register_vclk(s5e3830_clk_provider, s5e3830_clkout_vclks, ARRAY_SIZE(s5e3830_clkout_vclks));
	clk_register_fixed_factor(NULL, "pwm-clock", "fin_pll", CLK_SET_RATE_PARENT, 1, 1);

	samsung_clk_of_add_provider(np, s5e3830_clk_provider);

	s5e3830_vclk_init();

	pr_info("s5e3830: Clock setup completed\n");
	return 0;
}

static const struct of_device_id of_exynos_clock_match[] = {
        { .compatible = "samsung,s5e3830-clock", },
        { },
};
MODULE_DEVICE_TABLE(of, of_exynos_clock_match);

static const struct platform_device_id exynos_clock_ids[] = {
        { "s5e3830-clock", },
        { }
};

static struct platform_driver s5e3830_clock_driver = {
        .driver = {
                .name = "s5e3830-clock",
                .of_match_table = of_exynos_clock_match,
        },
        .probe          = s5e3830_clock_probe,
        .id_table       = exynos_clock_ids,
};

static int s5e3830_clock_init(void)
{
        return platform_driver_register(&s5e3830_clock_driver);
}
rootfs_initcall(s5e3830_clock_init);

static void s5e3830_clock_exit(void)
{
        return platform_driver_unregister(&s5e3830_clock_driver);
}
module_exit(s5e3830_clock_exit);

MODULE_LICENSE("GPL");

//CLK_OF_DECLARE(s5e3830_clk, "samsung,s5e3830-clock", s5e3830_clk_init);
