// SPDX-License-Identifier: GPL-2.0
/*
 * Freescale LS1024A reset controller defines
 * Copyright (c) 2018 Hugo Grostabussiat <bonstra@bonstra.fr.eu.org>
 */

#ifndef _DT_BINDINGS_RESET_LS1024A
#define _DT_BINDINGS_RESET_LS1024A

/* DEVICE_RST_CNTRL */
#define LS1024A_DEVICE_PWR_ON_SOFT_RST		0x0000
#define LS1024A_DEVICE_GLB_SCLR_RST		0x0001
#define LS1024A_DEVICE_FUNC_SCLR_RST		0x0002
#define LS1024A_DEVICE_CLKRST_SCLR_RST		0x0003
#define LS1024A_DEVICE_DEBUG_RST		0x0004
#define LS1024A_DEVICE_CLK_DIV_RESTART		0x0007

/* SERDES_RST_CNTRL */
#define LS1024A_SERDES_SERDES0_RESET		0x0020
#define LS1024A_SERDES_SERDES1_RESET		0x0021
#define LS1024A_SERDES_SERDES2_RESET		0x0022

/* PCIe_SATA_RST_CNTRL */
#define LS1024A_PCIe_SATA_PCIE0_PWR_RST		0x0040
#define LS1024A_PCIe_SATA_PCIE0_REG_RST		0x0041
#define LS1024A_PCIe_SATA_PCIE1_PWR_RST		0x0042
#define LS1024A_PCIe_SATA_PCIE1_REG_RST		0x0043
#define LS1024A_PCIe_SATA_SATA0_RX_RST		0x0044
#define LS1024A_PCIe_SATA_SATA0_TX_RST		0x0045
#define LS1024A_PCIe_SATA_SATA1_RX_RST		0x0046
#define LS1024A_PCIe_SATA_SATA1_TX_RST		0x0047

/* USB_RST_CNTRL */
#define LS1024A_USB_USB0_PHY_RESET		0x0060
#define LS1024A_USB_USB0_UTMI_RESET		0x0061
#define LS1024A_USB_USB1_PHY_RESET		0x0064
#define LS1024A_USB_USB1_UTMI_RESET		0x0065

/* AXI_RESET_x */
#define LS1024A_AXI_DPI_CIE_RST			0x0285
#define LS1024A_AXI_DPI_DECOMP_RST		0x0286

#define LS1024A_AXI_DUS_RST			0x02a0
#define LS1024A_AXI_IPSEC_EAPE_RST		0x02a1
#define LS1024A_AXI_IPSEC_SPACC_RST		0x02a2
#define LS1024A_AXI_PFE_SYS_RST			0x02a3
#define LS1024A_AXI_TDM_RST			0x02a4
#define LS1024A_AXI_I2CSPI_RST			0x02a5
#define LS1024A_AXI_UART_RST			0x02a6
#define LS1024A_AXI_RTC_TIM_RST			0x02a7

#define LS1024A_AXI_PCIE0_RST			0x02c0
#define LS1024A_AXI_PCIE1_RST			0x02c1
#define LS1024A_AXI_SATA_RST			0x02c2
#define LS1024A_AXI_USB0_RST			0x02c3
#define LS1024A_AXI_USB1_RST			0x02c4

/* A9DP_CPU_RESET */
#define LS1024A_CPU0_RST			0x03c0
#define LS1024A_NEON0_RST			0x03c1
#define LS1024A_CPU1_RST			0x03c2
#define LS1024A_NEON1_RST			0x03c3

/* A9DP_RESET */
#define LS1024A_A9DP_RST			0x0440

/* L2CC_RESET */
#define LS1024A_L2CC_RST			0x04c0

/* TPI_RESET */
#define LS1024A_TPI_RST				0x0540

/* CSYS_RESET */
#define LS1024A_CSYS_RST			0x05c0

/* EXTPHY0_RESET */
#define LS1024A_EXTPHY0_RST			0x0640

/* EXTPHY1_RESET */
#define LS1024A_EXTPHY1_RST			0x06c0

/* EXTPHY2_RESET */
#define LS1024A_EXTPHY2_RST			0x0740

/* DDR_RESET */
#define LS1024A_DDRPHY_RST			0x07c0
#define LS1024A_DDRCNTRL_RST			0x07c1

/* PFE_RESET */
#define LS1024A_PFE_CORE_RST			0x0840

/* IPSEC_RESET */
#define LS1024A_IPSEC_EAPE_CORE_RST		0x08c0

/* DECT_RESET */
#define LS1024A_DECT_RST			0x0940

/* GEMTX_RESET */
#define LS1024A_GEMTX_RST			0x09c0

/* TDMNTG_RESET */
#define LS1024A_TDMNTG_RST			0x0a40

/* TSUNTG_RESET */
#define LS1024A_TSUNTG_RST			0x0ac0

/* SATA_PMU_RESET */
#define LS1024A_SATA_PMU_RST			0x0b40

/* SATA_OOB_RESET */
#define LS1024A_SATA_OOB_RST			0x0bc0

/* SATA_OCC_RESET */
#define LS1024A_SATA_OCC_RST			0x0c40

/* PCIE_OCC_RESET */
#define LS1024A_PCIE_OCC_RST			0x0cc0

/* SGMII_OCC_RESET */
#define LS1024A_SGMII_OCC_RST			0x0d40

/* Maximum allowable reset value */
#define LS1024A_MAX_RST				0x1fff

#endif  /* _DT_BINDINGS_RESET_LS1024A */
