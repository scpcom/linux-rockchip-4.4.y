/*
 *  (C) Copyright 2011
 *  Author : Freescale Semiconductor, Inc.
 *
 *  See file CREDITS for list of people who contributed to this
 *  project.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of
 *  the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 *  MA 02111-1307 USA
 * */

#include <linux/module.h>
#include <linux/platform_device.h>

#include "pfe_mod.h"
#include "pfe_ctrl_hal.h"
#include "module_qm.h"

#define PE_EXCEPTION_DUMP_ADDRESS 0x1fa8

static char register_name[20][5] = {
	"EPC", "ECAS", "EID", "ED",
	"r0", "r1", "r2", "r3",
	"r4", "r5", "r6", "r7",
	"r8", "r9", "r10", "r11",
	"r12", "r13", "r14", "r15",
};

static char exception_name[14][20] = {
	"Reset",
	"HardwareFailure",
	"NMI",
	"InstBreakpoint",
	"DataBreakpoint",
	"Unsupported",
	"PrivilegeViolation",
	"InstBusError",
	"DataBusError",
	"AlignmentError",
	"ArithmeticError",
	"SystemCall",
	"MemoryManagement",
	"Interrupt",
};

static unsigned long class_do_clear = 0;
static unsigned long tmu_do_clear = 0;
static unsigned long util_do_clear = 0;

static ssize_t display_pe_status(char *buf, int id, u32 dmem_addr, unsigned long do_clear)
{
	ssize_t len = 0;
	u32 val;
	char statebuf[5];
	struct pfe_cpumon *cpumon = &pfe->cpumon;
	u32 debug_indicator;
	u32 debug[20];

	*(u32 *)statebuf = pe_dmem_read(id, dmem_addr, 4);
	dmem_addr += 4;

	statebuf[4] = '\0';
	len += sprintf(buf + len, "state=%4s ", statebuf);

	val = pe_dmem_read(id, dmem_addr, 4);
	dmem_addr += 4;
	len += sprintf(buf + len, "ctr=%08x ", cpu_to_be32(val));

	val = pe_dmem_read(id, dmem_addr, 4);
	if (do_clear && val)
		pe_dmem_write(id, 0, dmem_addr, 4);
	dmem_addr += 4;
	len += sprintf(buf + len, "rx=%u ", cpu_to_be32(val));

	val = pe_dmem_read(id, dmem_addr, 4);
	if (do_clear && val)
		pe_dmem_write(id, 0, dmem_addr, 4);
	dmem_addr += 4;
	if (id >= TMU0_ID && id <= TMU_MAX_ID)
		len += sprintf(buf + len, "qstatus=%x", cpu_to_be32(val));
	else
		len += sprintf(buf + len, "tx=%u", cpu_to_be32(val));

	val = pe_dmem_read(id, dmem_addr, 4);
	if (do_clear && val)
		pe_dmem_write(id, 0, dmem_addr, 4);
	dmem_addr += 4;
	if (val)
		len += sprintf(buf + len, " drop=%u", cpu_to_be32(val));

	len += sprintf(buf + len, " load=%d%%", cpumon->cpu_usage_pct[id]);

	len += sprintf(buf + len, "\n");

	debug_indicator = pe_dmem_read(id, dmem_addr, 4);
	dmem_addr += 4;
	if (!strncmp((char *)&debug_indicator, "DBUG", 4))
	{
		int j, last = 0;
		for (j = 0; j < 16; j++)
		{
			debug[j] = pe_dmem_read(id, dmem_addr, 4);
			if (debug[j])
			{
				if (do_clear)
					pe_dmem_write(id, 0, dmem_addr, 4);
				last = j + 1;
			}
			dmem_addr += 4;
		}
		for (j = 0; j < last; j++)
		{
			len += sprintf(buf + len, "%08x%s", cpu_to_be32(debug[j]),
							(j & 0x7) == 0x7 || j == last - 1 ? "\n" : " ");
		}
	}

	if (!strncmp(statebuf, "DEAD", 4))
	{
		u32 i, dump = PE_EXCEPTION_DUMP_ADDRESS;

		len += sprintf(buf + len, "Exception details:\n");
		for (i = 0; i < 20; i++) {
			debug[i] = pe_dmem_read(id, dump, 4);
			dump +=4;
			if (i == 2)
				len += sprintf(buf + len, "%4s = %08x (=%s) ", register_name[i], cpu_to_be32(debug[i]), exception_name[min((u32) cpu_to_be32(debug[i]), (u32)13)]);
			else
				len += sprintf(buf + len, "%4s = %08x%s", register_name[i], cpu_to_be32(debug[i]),
							(i & 0x3) == 0x3 || i == 19 ? "\n" : " ");
		}
	}

	return len;
}

static ssize_t class_phy_stats(char *buf, int phy)
{
	ssize_t len = 0;
	int off1 = phy * 0x28;
	int off2 = phy * 0x10;

	if (phy == 3)
		off1 = CLASS_PHY4_RX_PKTS - CLASS_PHY1_RX_PKTS;

	len += sprintf(buf + len, "phy: %d\n", phy);
	len += sprintf(buf + len, "  rx:   %10u, tx:   %10u, intf:  %10u, ipv4:    %10u, ipv6: %10u\n",
			readl(CLASS_PHY1_RX_PKTS + off1), readl(CLASS_PHY1_TX_PKTS + off1),
			readl(CLASS_PHY1_INTF_MATCH_PKTS + off1), readl(CLASS_PHY1_V4_PKTS + off1),
			readl(CLASS_PHY1_V6_PKTS + off1));

	len += sprintf(buf + len, "  icmp: %10u, igmp: %10u, tcp:   %10u, udp:     %10u\n",
			readl(CLASS_PHY1_ICMP_PKTS + off2), readl(CLASS_PHY1_IGMP_PKTS + off2),
			readl(CLASS_PHY1_TCP_PKTS + off2), readl(CLASS_PHY1_UDP_PKTS + off2));

	len += sprintf(buf + len, "  err\n");
	len += sprintf(buf + len, "  lp:   %10u, intf: %10u, l3:    %10u, chcksum: %10u, ttl:  %10u\n",
			readl(CLASS_PHY1_LP_FAIL_PKTS + off1), readl(CLASS_PHY1_INTF_FAIL_PKTS + off1),
			readl(CLASS_PHY1_L3_FAIL_PKTS + off1), readl(CLASS_PHY1_CHKSUM_ERR_PKTS + off1),
			readl(CLASS_PHY1_TTL_ERR_PKTS + off1));

	return len;
}

static ssize_t tmu_queue_stats(char *buf, int tmu, int queue)
{
	ssize_t len = 0;
	u32 drops;

	len += sprintf(buf + len, "%d-%02d, ", tmu, queue);

	drops = qm_read_drop_stat(tmu, queue, NULL, FALSE);

	/* Select queue */
	writel((tmu << 8) | queue, TMU_TEQ_CTRL);
	writel((tmu << 8) | queue, TMU_LLM_CTRL);

	len += sprintf(buf + len, "(teq) drop: %10u, tx: %10u (llm) head: %08x, tail: %08x, drop: %10u\n",
		drops, readl(TMU_TEQ_TRANS_STAT),
		readl(TMU_LLM_QUE_HEADPTR), readl(TMU_LLM_QUE_TAILPTR),
		readl(TMU_LLM_QUE_DROPCNT));

	return len;
}


static ssize_t tmu_queues(char *buf, int tmu)
{
	ssize_t len = 0;
	int queue;

	for (queue = 0; queue < 16; queue++)
		len += tmu_queue_stats(buf + len, tmu, queue);

	return len;
}

static ssize_t tmu_ctx(char *buf, int tmu)
{
	ssize_t len = 0;
	int i;
	u32 val, tmu_context_addr = TMU_CONTEXT_ADDR;

	len += sprintf(buf+len, " TMU %d \n", TMU0_ID+tmu);
	for (i = 1; i <= 160 ; i++, tmu_context_addr += 4)
	{
		val = pe_dmem_read(TMU0_ID+tmu, tmu_context_addr , 4);
		if (i == 5)
			len += sprintf(buf+len, "\nShapers: Each shaper structure is 8 bytes and there are 10 shapers\n");

		if (i == 25)
			len += sprintf(buf+len, "\nScheduler: Each scheduler structure is 48 bytes and there are 8 schedulers\n");
		if (i == 121)
			len += sprintf(buf+len, "\nQueue: Each queue structure is 2 bytes and there are 16 queues\n");

		if (i == 129)
			len += sprintf(buf+len, "\nqlenmasks array for  16 queues\n");
		if (i == 145)
			len += sprintf(buf+len, "\nqresultmap array for  16 queues\n");
		if (i%8 == 0)
			len += sprintf(buf+len, "%08x \n",  cpu_to_be32(val));
		else
			len += sprintf(buf+len, "%08x ",  cpu_to_be32(val));
	}
	
	len += sprintf(buf+len, "\n");
		
	return len;
}

static ssize_t block_version(char *buf, void *addr)
{
	ssize_t len = 0;
	u32 val;

	val = readl(addr);
	len += sprintf(buf + len, "revision: %x, version: %x, id: %x\n", (val >> 24) & 0xff, (val >> 16) & 0xff, val & 0xffff);

	return len;
}

static ssize_t bmu(char *buf, int id, void *base)
{
	ssize_t len = 0;

	len += sprintf(buf + len, "bmu: %d\n  ", id);

	len += block_version(buf + len, base + BMU_VERSION);

	len += sprintf(buf + len, "  buf size:  %x\n", (1 << readl(base + BMU_BUF_SIZE)));
	len += sprintf(buf + len, "  buf count: %x\n", readl(base + BMU_BUF_CNT));
	len += sprintf(buf + len, "  buf rem:   %x\n", readl(base + BMU_REM_BUF_CNT));
	len += sprintf(buf + len, "  buf curr:  %x\n", readl(base + BMU_CURR_BUF_CNT));
	len += sprintf(buf + len, "  free err:  %x\n", readl(base + BMU_FREE_ERR_ADDR));

	return len;
}

static ssize_t gpi(char *buf, int id, void *base)
{
	ssize_t len = 0;
	u32 val;

	len += sprintf(buf + len, "gpi%d:\n  ", id);
	len += block_version(buf + len, base + GPI_VERSION);

	len += sprintf(buf + len, "  tx under stick: %x\n", readl(base + GPI_FIFO_STATUS));
	val = readl(base + GPI_FIFO_DEBUG);
	len += sprintf(buf + len, "  tx pkts:        %x\n", (val >> 23) & 0x3f);
	len += sprintf(buf + len, "  rx pkts:        %x\n", (val >> 18) & 0x3f);
	len += sprintf(buf + len, "  tx bytes:       %x\n", (val >> 9) & 0x1ff);
	len += sprintf(buf + len, "  rx bytes:       %x\n", (val >> 0) & 0x1ff);
	len += sprintf(buf + len, "  overrun:        %x\n", readl(base + GPI_OVERRUN_DROPCNT));

	return len;
}

static ssize_t pfe_set_class(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	class_do_clear = simple_strtoul(buf, NULL, 0);
	return count;
}

static ssize_t pfe_show_class(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	int id;
	u32 val;
	struct pfe_cpumon *cpumon = &pfe->cpumon;

	len += block_version(buf + len, CLASS_VERSION);

	for (id = CLASS0_ID; id <= CLASS_MAX_ID; id++)
	{
		len += sprintf(buf + len, "%d: ", id - CLASS0_ID);

		val = readl(CLASS_PE0_DEBUG + id * 4);
		len += sprintf(buf + len, "pc=1%04x ", val & 0xffff);

		len += display_pe_status(buf + len, id, PESTATUS_ADDR_CLASS, class_do_clear);
	}
	len += sprintf(buf + len, "aggregate load=%d%%\n\n", cpumon->class_usage_pct);

	len += sprintf(buf + len, "pe status:   0x%x\n", readl(CLASS_PE_STATUS));
	len += sprintf(buf + len, "max buf cnt: 0x%x   afull thres: 0x%x\n", readl(CLASS_MAX_BUF_CNT), readl(CLASS_AFULL_THRES));
	len += sprintf(buf + len, "tsq max cnt: 0x%x   tsq fifo thres: 0x%x\n", readl(CLASS_TSQ_MAX_CNT), readl(CLASS_TSQ_FIFO_THRES));
	len += sprintf(buf + len, "state:       0x%x\n", readl(CLASS_STATE));

	len += class_phy_stats(buf + len, 0);
	len += class_phy_stats(buf + len, 1);
	len += class_phy_stats(buf + len, 2);
	len += class_phy_stats(buf + len, 3);

	return len;
}

static ssize_t pfe_set_tmu(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	tmu_do_clear = simple_strtoul(buf, NULL, 0);
	return count;
}

static ssize_t pfe_show_tmu(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	int id;
	u32 val;

	len += block_version(buf + len, TMU_VERSION);

	for (id = TMU0_ID; id <= TMU_MAX_ID; id++)
	{
		len += sprintf(buf + len, "%d: ", id - TMU0_ID);

		len += display_pe_status(buf + len, id, PESTATUS_ADDR_TMU, tmu_do_clear);
	}

	len += sprintf(buf + len, "pe status:    %x\n", readl(TMU_PE_STATUS));
	len += sprintf(buf + len, "inq fifo cnt: %x\n", readl(TMU_PHY_INQ_FIFO_CNT));
	val = readl(TMU_INQ_STAT);
	len += sprintf(buf + len, "inq wr ptr:     %x\n", val & 0x3ff);
	len += sprintf(buf + len, "inq rd ptr:     %x\n", val >> 10);


	return len;
}


static unsigned long drops_do_clear = 0;
static u32 CLASS_DMEM_SH2(drop_counter)[CLASS_NUM_DROP_COUNTERS];
static u32 UTIL_DMEM_SH2(drop_counter)[UTIL_NUM_DROP_COUNTERS];

char *class_drop_description[CLASS_NUM_DROP_COUNTERS] = {
	"ICC",
	"Host Pkt Error",
	"Rx Error",
	"IPsec Outbound",
	"IPsec Inbound",
	"EXPT IPsec Error",
	"Reassembly",
	"Fragmenter",
	"NAT-T",
	"Socket",
	"Multicast",
	"NAT-PT",
	"Tx Disabled",
};

char *util_drop_description[UTIL_NUM_DROP_COUNTERS] = {
	"IPsec Outbound",
	"IPsec Inbound",
	"IPsec Rate Limiter",
	"Fragmenter",
	"Socket",
	"Tx Disabled",
	"Rx Error",
};

static ssize_t pfe_set_drops(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	drops_do_clear = simple_strtoul(buf, NULL, 0);
	return count;
}

static u32 tmu_drops[4][16];
static ssize_t pfe_show_drops(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	int id, dropnum;
	int tmu, queue;
	u32 val;
	u32 dmem_addr;
	int num_class_drops = 0, num_tmu_drops = 0, num_util_drops = 0;
	struct pfe_ctrl *ctrl = &pfe->ctrl;

	memset(class_drop_counter, 0, sizeof(class_drop_counter));
	for (id = CLASS0_ID; id <= CLASS_MAX_ID; id++)
	{
		if (drops_do_clear)
			pe_sync_stop(ctrl, (1 << id));
		for (dropnum = 0; dropnum < CLASS_NUM_DROP_COUNTERS; dropnum++)
		{
			dmem_addr = virt_to_class_dmem(&class_drop_counter[dropnum]);
			val = be32_to_cpu(pe_dmem_read(id, dmem_addr, 4));
			class_drop_counter[dropnum] += val;
			num_class_drops += val;
			if (drops_do_clear)
				pe_dmem_write(id, 0, dmem_addr, 4);
		}
		if (drops_do_clear)
			pe_start(ctrl, (1 << id));
	}

	if (drops_do_clear)
		pe_sync_stop(ctrl, (1 << UTIL_ID));
	for (dropnum = 0; dropnum < UTIL_NUM_DROP_COUNTERS; dropnum++)
	{
		dmem_addr = virt_to_util_dmem(&util_drop_counter[dropnum]);
		val = be32_to_cpu(pe_dmem_read(UTIL_ID, dmem_addr, 4));
		util_drop_counter[dropnum] = val;
		num_util_drops += val;
		if (drops_do_clear)
			pe_dmem_write(UTIL_ID, 0, dmem_addr, 4);
	}
	if (drops_do_clear)
		pe_start(ctrl, (1 << UTIL_ID));

	for (tmu = 0; tmu < 4; tmu++)
	{
		for (queue = 0; queue < 16; queue++)
		{
			qm_read_drop_stat(tmu, queue, &tmu_drops[tmu][queue], drops_do_clear);
			num_tmu_drops += tmu_drops[tmu][queue];
		}
	}

	if (num_class_drops == 0 && num_util_drops == 0 && num_tmu_drops == 0)
		len += sprintf(buf + len, "No PE drops\n\n");

	if (num_class_drops > 0)
	{
		len += sprintf(buf + len, "Class PE drops --\n");
		for (dropnum = 0; dropnum < CLASS_NUM_DROP_COUNTERS; dropnum++)
		{
			if (class_drop_counter[dropnum] > 0)
				len += sprintf(buf + len, "  %s: %d\n", class_drop_description[dropnum], class_drop_counter[dropnum]);
		}
		len += sprintf(buf + len, "\n");
	}

	if (num_util_drops > 0)
	{
		len += sprintf(buf + len, "Util PE drops --\n");
		for (dropnum = 0; dropnum < UTIL_NUM_DROP_COUNTERS; dropnum++)
		{
			if (util_drop_counter[dropnum] > 0)
				len += sprintf(buf + len, "  %s: %d\n", util_drop_description[dropnum], util_drop_counter[dropnum]);
		}
		len += sprintf(buf + len, "\n");
	}

	if (num_tmu_drops > 0)
	{
		len += sprintf(buf + len, "TMU drops --\n");
		for (tmu = 0; tmu < 4; tmu++)
		{
			for (queue = 0; queue < 16; queue++)
			{
				if (tmu_drops[tmu][queue] > 0)
					len += sprintf(buf + len, "  TMU%d-Q%d: %d\n", tmu, queue, tmu_drops[tmu][queue]);
			}
		}
		len += sprintf(buf + len, "\n");
	}

	return len;
}

static ssize_t pfe_show_tmu0_queues(struct device *dev, struct device_attribute *attr, char *buf)
{
	return tmu_queues(buf, 0);
}

static ssize_t pfe_show_tmu1_queues(struct device *dev, struct device_attribute *attr, char *buf)
{
	return tmu_queues(buf, 1);
}

static ssize_t pfe_show_tmu2_queues(struct device *dev, struct device_attribute *attr, char *buf)
{
	return tmu_queues(buf, 2);
}

static ssize_t pfe_show_tmu3_queues(struct device *dev, struct device_attribute *attr, char *buf)
{
	return tmu_queues(buf, 3);
}

static ssize_t pfe_show_tmu0_ctx(struct device *dev, struct device_attribute *attr, char *buf)
{
        return tmu_ctx(buf, 0);
}
static ssize_t pfe_show_tmu1_ctx(struct device *dev, struct device_attribute *attr, char *buf)
{
        return tmu_ctx(buf, 1);
}
static ssize_t pfe_show_tmu2_ctx(struct device *dev, struct device_attribute *attr, char *buf)
{
        return tmu_ctx(buf, 2);
}

static ssize_t pfe_show_tmu3_ctx(struct device *dev, struct device_attribute *attr, char *buf)
{
        return tmu_ctx(buf, 3);
}


#if !defined(CONFIG_UTIL_DISABLED)
static ssize_t pfe_set_util(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	util_do_clear = simple_strtoul(buf, NULL, 0);
	return count;
}

static ssize_t pfe_show_util(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct pfe_ctrl *ctrl = &pfe->ctrl;


	len += block_version(buf + len, UTIL_VERSION);

	pe_sync_stop(ctrl, (1 << UTIL_ID));
	len += display_pe_status(buf + len, UTIL_ID, PESTATUS_ADDR_UTIL, util_do_clear);
	pe_start(ctrl, (1 << UTIL_ID));

	len += sprintf(buf + len, "pe status:   %x\n", readl(UTIL_PE_STATUS));
	len += sprintf(buf + len, "max buf cnt: %x\n", readl(UTIL_MAX_BUF_CNT));
	len += sprintf(buf + len, "tsq max cnt: %x\n", readl(UTIL_TSQ_MAX_CNT));

	return len;
}
#endif

static ssize_t pfe_show_bmu(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;

	len += bmu(buf + len, 1, BMU1_BASE_ADDR);
	len += bmu(buf + len, 2, BMU2_BASE_ADDR);

	return len;
}

static ssize_t pfe_show_hif(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;

	len += sprintf(buf + len, "hif:\n  ");
	len += block_version(buf + len, HIF_VERSION);

	len += sprintf(buf + len, "  tx curr bd:    %x\n", readl(HIF_TX_CURR_BD_ADDR));
	len += sprintf(buf + len, "  tx status:     %x\n", readl(HIF_TX_STATUS));
	len += sprintf(buf + len, "  tx dma status: %x\n", readl(HIF_TX_DMA_STATUS));

	len += sprintf(buf + len, "  rx curr bd:    %x\n", readl(HIF_RX_CURR_BD_ADDR));
	len += sprintf(buf + len, "  rx status:     %x\n", readl(HIF_RX_STATUS));
	len += sprintf(buf + len, "  rx dma status: %x\n", readl(HIF_RX_DMA_STATUS));

	len += sprintf(buf + len, "hif nocopy:\n  ");
	len += block_version(buf + len, HIF_NOCPY_VERSION);

	len += sprintf(buf + len, "  tx curr bd:    %x\n", readl(HIF_NOCPY_TX_CURR_BD_ADDR));
	len += sprintf(buf + len, "  tx status:     %x\n", readl(HIF_NOCPY_TX_STATUS));
	len += sprintf(buf + len, "  tx dma status: %x\n", readl(HIF_NOCPY_TX_DMA_STATUS));

	len += sprintf(buf + len, "  rx curr bd:    %x\n", readl(HIF_NOCPY_RX_CURR_BD_ADDR));
	len += sprintf(buf + len, "  rx status:     %x\n", readl(HIF_NOCPY_RX_STATUS));
	len += sprintf(buf + len, "  rx dma status: %x\n", readl(HIF_NOCPY_RX_DMA_STATUS));

	return len;
}


static ssize_t pfe_show_gpi(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;

	len += gpi(buf + len, 0, EGPI1_BASE_ADDR);
	len += gpi(buf + len, 1, EGPI2_BASE_ADDR);
	len += gpi(buf + len, 2, EGPI3_BASE_ADDR);
	len += gpi(buf + len, 3, HGPI_BASE_ADDR);

	return len;
}
void ipsec_standalone_init(void);
static ssize_t pfe_set_ipsec_cmd(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	
	ipsec_standalone_init();
	len += sprintf(buf + len, " ipsec details added ");
	return len;
}

static ssize_t pfe_show_ipsec_cntrs(struct device *dev, struct device_attribute *attr, char *buf)
{
        ssize_t len = 0;
	u32 dmem_addr = IPSEC_CNTRS_ADDR;
	u32 val;
	int id = UTIL_ID;

	val = pe_dmem_read(id, dmem_addr, 4);
	dmem_addr += 4;
	len += sprintf(buf + len, " \noutbound cntrs:\n ");
	len += sprintf(buf + len, " rcvd: %08x ", cpu_to_be32(val));
	val = pe_dmem_read(id, dmem_addr, 4);
	dmem_addr += 4;
	len += sprintf(buf + len, " xmit: %08x ", cpu_to_be32(val));
	val = pe_dmem_read(id, dmem_addr, 4);
	dmem_addr += 4;
	len += sprintf(buf + len, " tohost: %08x ", cpu_to_be32(val));
	val = pe_dmem_read(id, dmem_addr, 4);
	dmem_addr += 4;
	len += sprintf(buf + len, " dropped: %08x ", cpu_to_be32(val));

	len += sprintf(buf + len, " \ninbound cntrs:\n ");
	val = pe_dmem_read(id, dmem_addr, 4);
	dmem_addr += 4;
	len += sprintf(buf + len, " rcvd: %08x ", cpu_to_be32(val));
	val = pe_dmem_read(id, dmem_addr, 4);
	dmem_addr += 4;
	len += sprintf(buf + len, " xmit: %08x ", cpu_to_be32(val));
	val = pe_dmem_read(id, dmem_addr, 4);
	dmem_addr += 4;
	len += sprintf(buf + len, " tohost: %08x ", cpu_to_be32(val));
	val = pe_dmem_read(id, dmem_addr, 4);
	dmem_addr += 4;
	len += sprintf(buf + len, " dropped: %08x\n ", cpu_to_be32(val));

	val = pe_dmem_read(id, dmem_addr, 4);
	dmem_addr += 4;
	len += sprintf(buf + len, " wa_drops: %08x\n ", cpu_to_be32(val));
        return len;
}

static ssize_t pfe_show_pfemem(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct pfe_memmon *memmon = &pfe->memmon;

	len += sprintf(buf + len, "Kernel Memory: %d Bytes (%d KB)\n", memmon->kernel_memory_allocated, (memmon->kernel_memory_allocated + 1023) / 1024);

	return len;
}

#ifdef HIF_NAPI_STATS
static ssize_t pfe_show_hif_napi_stats(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pfe *pfe = platform_get_drvdata(pdev);
	ssize_t len = 0;

	len += sprintf(buf + len, "sched:  %u\n", pfe->hif.napi_counters[NAPI_SCHED_COUNT]);
	len += sprintf(buf + len, "poll:   %u\n", pfe->hif.napi_counters[NAPI_POLL_COUNT]);
	len += sprintf(buf + len, "packet: %u\n", pfe->hif.napi_counters[NAPI_PACKET_COUNT]);
	len += sprintf(buf + len, "budget: %u\n", pfe->hif.napi_counters[NAPI_FULL_BUDGET_COUNT]);
	len += sprintf(buf + len, "desc:   %u\n", pfe->hif.napi_counters[NAPI_DESC_COUNT]);
	len += sprintf(buf + len, "full:   %u\n", pfe->hif.napi_counters[NAPI_CLIENT_FULL_COUNT]);

	return len;
}

static ssize_t pfe_set_hif_napi_stats(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pfe *pfe = platform_get_drvdata(pdev);

	memset(pfe->hif.napi_counters, 0, sizeof(pfe->hif.napi_counters));

	return count;
}

static DEVICE_ATTR(hif_napi_stats, 0644, pfe_show_hif_napi_stats, pfe_set_hif_napi_stats);
#endif


static DEVICE_ATTR(class, 0644, pfe_show_class, pfe_set_class);
static DEVICE_ATTR(tmu, 0644, pfe_show_tmu, pfe_set_tmu);
#if !defined(CONFIG_UTIL_DISABLED)
static DEVICE_ATTR(util, 0644, pfe_show_util, pfe_set_util);
#endif
static DEVICE_ATTR(bmu, 0444, pfe_show_bmu, NULL);
static DEVICE_ATTR(hif, 0444, pfe_show_hif, NULL);
static DEVICE_ATTR(gpi, 0444, pfe_show_gpi, NULL);
static DEVICE_ATTR(drops, 0644, pfe_show_drops, pfe_set_drops);
static DEVICE_ATTR(tmu0_queues, 0444, pfe_show_tmu0_queues, NULL);
static DEVICE_ATTR(tmu1_queues, 0444, pfe_show_tmu1_queues, NULL);
static DEVICE_ATTR(tmu2_queues, 0444, pfe_show_tmu2_queues, NULL);
static DEVICE_ATTR(tmu3_queues, 0444, pfe_show_tmu3_queues, NULL);
static DEVICE_ATTR(tmu0_ctx, 0444, pfe_show_tmu0_ctx, NULL);
static DEVICE_ATTR(tmu1_ctx, 0444, pfe_show_tmu1_ctx, NULL);
static DEVICE_ATTR(tmu2_ctx, 0444, pfe_show_tmu2_ctx, NULL);
static DEVICE_ATTR(tmu3_ctx, 0444, pfe_show_tmu3_ctx, NULL);
static DEVICE_ATTR(ipsec_cntrs, 0444, pfe_show_ipsec_cntrs, NULL);
static DEVICE_ATTR(ipsec_cmd, 0444, pfe_set_ipsec_cmd, NULL);
static DEVICE_ATTR(pfemem, 0444, pfe_show_pfemem, NULL);


int pfe_sysfs_init(struct pfe *pfe)
{
	if (device_create_file(pfe->dev, &dev_attr_class))
		goto err_class;

	if (device_create_file(pfe->dev, &dev_attr_tmu))
		goto err_tmu;

#if !defined(CONFIG_UTIL_DISABLED)
	if (device_create_file(pfe->dev, &dev_attr_util))
		goto err_util;
#endif

	if (device_create_file(pfe->dev, &dev_attr_bmu))
		goto err_bmu;

	if (device_create_file(pfe->dev, &dev_attr_hif))
		goto err_hif;

	if (device_create_file(pfe->dev, &dev_attr_gpi))
		goto err_gpi;

	if (device_create_file(pfe->dev, &dev_attr_drops))
		goto err_drops;

	if (device_create_file(pfe->dev, &dev_attr_tmu0_queues))
		goto err_tmu0_queues;

	if (device_create_file(pfe->dev, &dev_attr_tmu1_queues))
		goto err_tmu1_queues;

	if (device_create_file(pfe->dev, &dev_attr_tmu2_queues))
		goto err_tmu2_queues;

	if (device_create_file(pfe->dev, &dev_attr_tmu3_queues))
		goto err_tmu3_queues;

	if (device_create_file(pfe->dev, &dev_attr_tmu0_ctx))
		goto err_tmu0_ctx;

	if (device_create_file(pfe->dev, &dev_attr_tmu1_ctx))
		goto err_tmu1_ctx;

	if (device_create_file(pfe->dev, &dev_attr_tmu2_ctx))
		goto err_tmu2_ctx;

	if (device_create_file(pfe->dev, &dev_attr_tmu3_ctx))
		goto err_tmu3_ctx;

	if (device_create_file(pfe->dev, &dev_attr_ipsec_cmd))
		goto err_ipsec_cmd;

	if (device_create_file(pfe->dev, &dev_attr_ipsec_cntrs))
		goto err_ipsec_cntrs;

	if (device_create_file(pfe->dev, &dev_attr_pfemem))
		goto err_pfemem;

#ifdef HIF_NAPI_STATS
	if (device_create_file(pfe->dev, &dev_attr_hif_napi_stats))
		goto err_hif_napi_stats;
#endif

	return 0;

#ifdef HIF_NAPI_STATS
err_hif_napi_stats:
	device_remove_file(pfe->dev, &dev_attr_pfemem);
#endif

err_pfemem:
	device_remove_file(pfe->dev, &dev_attr_ipsec_cntrs);

err_ipsec_cntrs:
	device_remove_file(pfe->dev, &dev_attr_ipsec_cmd);

err_ipsec_cmd:
	device_remove_file(pfe->dev, &dev_attr_tmu3_ctx);

err_tmu3_ctx:
	device_remove_file(pfe->dev, &dev_attr_tmu2_ctx);

err_tmu2_ctx:
	device_remove_file(pfe->dev, &dev_attr_tmu1_ctx);

err_tmu1_ctx:
	device_remove_file(pfe->dev, &dev_attr_tmu0_ctx);

err_tmu0_ctx:
	device_remove_file(pfe->dev, &dev_attr_tmu3_queues);

err_tmu3_queues:
	device_remove_file(pfe->dev, &dev_attr_tmu2_queues);

err_tmu2_queues:
	device_remove_file(pfe->dev, &dev_attr_tmu1_queues);

err_tmu1_queues:
	device_remove_file(pfe->dev, &dev_attr_tmu0_queues);

err_tmu0_queues:
	device_remove_file(pfe->dev, &dev_attr_drops);

err_drops:
	device_remove_file(pfe->dev, &dev_attr_gpi);

err_gpi:
	device_remove_file(pfe->dev, &dev_attr_hif);

err_hif:
	device_remove_file(pfe->dev, &dev_attr_bmu);

err_bmu:
#if !defined(CONFIG_UTIL_DISABLED)
	device_remove_file(pfe->dev, &dev_attr_util);

err_util:
#endif
	device_remove_file(pfe->dev, &dev_attr_tmu);

err_tmu:
	device_remove_file(pfe->dev, &dev_attr_class);

err_class:
	return -1;
}


void pfe_sysfs_exit(struct pfe *pfe)
{
#ifdef HIF_NAPI_STATS
	device_remove_file(pfe->dev, &dev_attr_hif_napi_stats);
#endif

	device_remove_file(pfe->dev, &dev_attr_pfemem);
	device_remove_file(pfe->dev, &dev_attr_ipsec_cntrs);
	device_remove_file(pfe->dev, &dev_attr_ipsec_cmd);
	device_remove_file(pfe->dev, &dev_attr_tmu3_ctx);
	device_remove_file(pfe->dev, &dev_attr_tmu2_ctx);
	device_remove_file(pfe->dev, &dev_attr_tmu1_ctx);
	device_remove_file(pfe->dev, &dev_attr_tmu0_ctx);
	device_remove_file(pfe->dev, &dev_attr_tmu3_queues);
	device_remove_file(pfe->dev, &dev_attr_tmu2_queues);
	device_remove_file(pfe->dev, &dev_attr_tmu1_queues);
	device_remove_file(pfe->dev, &dev_attr_tmu0_queues);
	device_remove_file(pfe->dev, &dev_attr_drops);
	device_remove_file(pfe->dev, &dev_attr_gpi);
	device_remove_file(pfe->dev, &dev_attr_hif);
	device_remove_file(pfe->dev, &dev_attr_bmu);
#if !defined(CONFIG_UTIL_DISABLED)
	device_remove_file(pfe->dev, &dev_attr_util);
#endif
	device_remove_file(pfe->dev, &dev_attr_tmu);
	device_remove_file(pfe->dev, &dev_attr_class);
}

