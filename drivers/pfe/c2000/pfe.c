/*
 *  Copyright (c) 2011, 2014 Freescale Semiconductor, Inc.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
*/
#include "hal.h"
#include "pfe/pfe.h"
#include "module_qm.h"

void *cbus_base_addr;
void *ddr_base_addr;
unsigned long ddr_phys_base_addr;
unsigned int ddr_size;

static struct pe_info pe[MAX_PE];

/** Initializes the PFE library.
* Must be called before using any of the library functions.
*
* @param[in] cbus_base		CBUS virtual base address (as mapped in the host CPU address space)
* @param[in] ddr_base		PFE DDR range virtual base address (as mapped in the host CPU address space)
* @param[in] ddr_phys_base	PFE DDR range physical base address (as mapped in platform)
* @param[in] size		PFE DDR range size (as defined by the host software)
*/
void pfe_lib_init(void *cbus_base, void *ddr_base, unsigned long ddr_phys_base, unsigned int size)
{
	cbus_base_addr = cbus_base;
	ddr_base_addr = ddr_base;
	ddr_phys_base_addr = ddr_phys_base;
	ddr_size = size;

	pe[CLASS0_ID].dmem_base_addr = CLASS_DMEM_BASE_ADDR(0);
	pe[CLASS0_ID].pmem_base_addr = CLASS_IMEM_BASE_ADDR(0);
	pe[CLASS0_ID].pmem_size = CLASS_IMEM_SIZE;
	pe[CLASS0_ID].mem_access_wdata = CLASS_MEM_ACCESS_WDATA;
	pe[CLASS0_ID].mem_access_addr = CLASS_MEM_ACCESS_ADDR;
	pe[CLASS0_ID].mem_access_rdata = CLASS_MEM_ACCESS_RDATA;

	pe[CLASS1_ID].dmem_base_addr = CLASS_DMEM_BASE_ADDR(1);
	pe[CLASS1_ID].pmem_base_addr = CLASS_IMEM_BASE_ADDR(1);
	pe[CLASS1_ID].pmem_size = CLASS_IMEM_SIZE;
	pe[CLASS1_ID].mem_access_wdata = CLASS_MEM_ACCESS_WDATA;
	pe[CLASS1_ID].mem_access_addr = CLASS_MEM_ACCESS_ADDR;
	pe[CLASS1_ID].mem_access_rdata = CLASS_MEM_ACCESS_RDATA;

	pe[CLASS2_ID].dmem_base_addr = CLASS_DMEM_BASE_ADDR(2);
	pe[CLASS2_ID].pmem_base_addr = CLASS_IMEM_BASE_ADDR(2);
	pe[CLASS2_ID].pmem_size = CLASS_IMEM_SIZE;
	pe[CLASS2_ID].mem_access_wdata = CLASS_MEM_ACCESS_WDATA;
	pe[CLASS2_ID].mem_access_addr = CLASS_MEM_ACCESS_ADDR;
	pe[CLASS2_ID].mem_access_rdata = CLASS_MEM_ACCESS_RDATA;

	pe[CLASS3_ID].dmem_base_addr = CLASS_DMEM_BASE_ADDR(3);
	pe[CLASS3_ID].pmem_base_addr = CLASS_IMEM_BASE_ADDR(3);
	pe[CLASS3_ID].pmem_size = CLASS_IMEM_SIZE;
	pe[CLASS3_ID].mem_access_wdata = CLASS_MEM_ACCESS_WDATA;
	pe[CLASS3_ID].mem_access_addr = CLASS_MEM_ACCESS_ADDR;
	pe[CLASS3_ID].mem_access_rdata = CLASS_MEM_ACCESS_RDATA;

#if !defined(CONFIG_PLATFORM_PCI)
	pe[CLASS4_ID].dmem_base_addr = CLASS_DMEM_BASE_ADDR(4);
	pe[CLASS4_ID].pmem_base_addr = CLASS_IMEM_BASE_ADDR(4);
	pe[CLASS4_ID].pmem_size = CLASS_IMEM_SIZE;
	pe[CLASS4_ID].mem_access_wdata = CLASS_MEM_ACCESS_WDATA;
	pe[CLASS4_ID].mem_access_addr = CLASS_MEM_ACCESS_ADDR;
	pe[CLASS4_ID].mem_access_rdata = CLASS_MEM_ACCESS_RDATA;

	pe[CLASS5_ID].dmem_base_addr = CLASS_DMEM_BASE_ADDR(5);
	pe[CLASS5_ID].pmem_base_addr = CLASS_IMEM_BASE_ADDR(5);
	pe[CLASS5_ID].pmem_size = CLASS_IMEM_SIZE;
	pe[CLASS5_ID].mem_access_wdata = CLASS_MEM_ACCESS_WDATA;
	pe[CLASS5_ID].mem_access_addr = CLASS_MEM_ACCESS_ADDR;
	pe[CLASS5_ID].mem_access_rdata = CLASS_MEM_ACCESS_RDATA;
#endif
	pe[TMU0_ID].dmem_base_addr = TMU_DMEM_BASE_ADDR(0);
	pe[TMU0_ID].pmem_base_addr = TMU_IMEM_BASE_ADDR(0);
	pe[TMU0_ID].pmem_size = TMU_IMEM_SIZE;
	pe[TMU0_ID].mem_access_wdata = TMU_MEM_ACCESS_WDATA;
	pe[TMU0_ID].mem_access_addr = TMU_MEM_ACCESS_ADDR;
	pe[TMU0_ID].mem_access_rdata = TMU_MEM_ACCESS_RDATA;

#if !defined(CONFIG_TMU_DUMMY)
	pe[TMU1_ID].dmem_base_addr = TMU_DMEM_BASE_ADDR(1);
	pe[TMU1_ID].pmem_base_addr = TMU_IMEM_BASE_ADDR(1);
	pe[TMU1_ID].pmem_size = TMU_IMEM_SIZE;
	pe[TMU1_ID].mem_access_wdata = TMU_MEM_ACCESS_WDATA;
	pe[TMU1_ID].mem_access_addr = TMU_MEM_ACCESS_ADDR;
	pe[TMU1_ID].mem_access_rdata = TMU_MEM_ACCESS_RDATA;

	pe[TMU2_ID].dmem_base_addr = TMU_DMEM_BASE_ADDR(2);
	pe[TMU2_ID].pmem_base_addr = TMU_IMEM_BASE_ADDR(2);
	pe[TMU2_ID].pmem_size = TMU_IMEM_SIZE;
	pe[TMU2_ID].mem_access_wdata = TMU_MEM_ACCESS_WDATA;
	pe[TMU2_ID].mem_access_addr = TMU_MEM_ACCESS_ADDR;
	pe[TMU2_ID].mem_access_rdata = TMU_MEM_ACCESS_RDATA;

	pe[TMU3_ID].dmem_base_addr = TMU_DMEM_BASE_ADDR(3);
	pe[TMU3_ID].pmem_base_addr = TMU_IMEM_BASE_ADDR(3);
	pe[TMU3_ID].pmem_size = TMU_IMEM_SIZE;
	pe[TMU3_ID].mem_access_wdata = TMU_MEM_ACCESS_WDATA;
	pe[TMU3_ID].mem_access_addr = TMU_MEM_ACCESS_ADDR;
	pe[TMU3_ID].mem_access_rdata = TMU_MEM_ACCESS_RDATA;
#endif

#if !defined(CONFIG_UTIL_DISABLED)
	pe[UTIL_ID].dmem_base_addr = UTIL_DMEM_BASE_ADDR;
	pe[UTIL_ID].mem_access_wdata = UTIL_MEM_ACCESS_WDATA;
	pe[UTIL_ID].mem_access_addr = UTIL_MEM_ACCESS_ADDR;
	pe[UTIL_ID].mem_access_rdata = UTIL_MEM_ACCESS_RDATA;
#endif
}


/** Writes a buffer to PE internal memory from the host
 * through indirect access registers.
 *
 * @param[in] id		PE identification (CLASS0_ID, ..., TMU0_ID, ..., UTIL_ID)
 * @param[in] src		Buffer source address
 * @param[in] mem_access_addr	DMEM destination address (must be 32bit aligned)
 * @param[in] len		Number of bytes to copy
 */
static void pe_mem_memcpy_to32(int id, u32 mem_access_addr, const void *src, unsigned int len)
{
	u32 offset = 0, val, addr;
	unsigned int len32 = len >> 2;
	int i;

	addr = mem_access_addr | PE_MEM_ACCESS_WRITE | PE_MEM_ACCESS_BYTE_ENABLE(0, 4);

	for (i = 0; i < len32; i++, offset += 4, src += 4) {
		val = *(u32 *)src;
		writel(cpu_to_be32(val), pe[id].mem_access_wdata);
		writel(addr + offset, pe[id].mem_access_addr);
	}

	if ((len = (len & 0x3))) {
		val = 0;

		addr = (mem_access_addr | PE_MEM_ACCESS_WRITE | PE_MEM_ACCESS_BYTE_ENABLE(0, len)) + offset;

		for (i = 0; i < len; i++, src++)
			val |= (*(u8 *)src) << (8 * i);

		writel(cpu_to_be32(val), pe[id].mem_access_wdata);
		writel(addr, pe[id].mem_access_addr);
	}
}

/** Writes a buffer to PE internal data memory (DMEM) from the host
 * through indirect access registers.
 * @param[in] id		PE identification (CLASS0_ID, ..., TMU0_ID, ..., UTIL_ID)
 * @param[in] src		Buffer source address
 * @param[in] dst		DMEM destination address (must be 32bit aligned)
 * @param[in] len		Number of bytes to copy
 */
void pe_dmem_memcpy_to32(int id, u32 dst, const void *src, unsigned int len)
{
	pe_mem_memcpy_to32(id, pe[id].dmem_base_addr | dst | PE_MEM_ACCESS_DMEM, src, len);
}


/** Writes a buffer to PE internal program memory (PMEM) from the host
 * through indirect access registers.
 * @param[in] id		PE identification (CLASS0_ID, ..., TMU0_ID, ..., TMU3_ID)
 * @param[in] src		Buffer source address
 * @param[in] dst		PMEM destination address (must be 32bit aligned)
 * @param[in] len		Number of bytes to copy
 */
void pe_pmem_memcpy_to32(int id, u32 dst, const void *src, unsigned int len)
{
	pe_mem_memcpy_to32(id, pe[id].pmem_base_addr | (dst & (pe[id].pmem_size - 1)) | PE_MEM_ACCESS_IMEM, src, len);
}


/** Reads PE internal program memory (IMEM) from the host
 * through indirect access registers.
 * @param[in] id		PE identification (CLASS0_ID, ..., TMU0_ID, ..., TMU3_ID)
 * @param[in] addr		PMEM read address (must be aligned on size)
 * @param[in] size		Number of bytes to read (maximum 4, must not cross 32bit boundaries)
 * @return			the data read (in PE endianess, i.e BE).
 */
u32 pe_pmem_read(int id, u32 addr, u8 size)
{
	u32 offset = addr & 0x3;
	u32 mask = 0xffffffff >> ((4 - size) << 3);
	u32 val;

	addr = pe[id].pmem_base_addr | ((addr & ~0x3) & (pe[id].pmem_size - 1)) | PE_MEM_ACCESS_IMEM | PE_MEM_ACCESS_BYTE_ENABLE(offset, size);

	writel(addr, pe[id].mem_access_addr);
	val = be32_to_cpu(readl(pe[id].mem_access_rdata));

	return (val >> (offset << 3)) & mask;
}


/** Writes PE internal data memory (DMEM) from the host
 * through indirect access registers.
 * @param[in] id		PE identification (CLASS0_ID, ..., TMU0_ID, ..., UTIL_ID)
 * @param[in] addr		DMEM write address (must be aligned on size)
 * @param[in] val		Value to write (in PE endianess, i.e BE)
 * @param[in] size		Number of bytes to write (maximum 4, must not cross 32bit boundaries)
 */
void pe_dmem_write(int id, u32 val, u32 addr, u8 size)
{
	u32 offset = addr & 0x3;

	addr = pe[id].dmem_base_addr | (addr & ~0x3) | PE_MEM_ACCESS_WRITE | PE_MEM_ACCESS_DMEM | PE_MEM_ACCESS_BYTE_ENABLE(offset, size);

	/* Indirect access interface is byte swapping data being written */
	writel(cpu_to_be32(val << (offset << 3)), pe[id].mem_access_wdata);
	writel(addr, pe[id].mem_access_addr);
}


/** Reads PE internal data memory (DMEM) from the host
 * through indirect access registers.
 * @param[in] id		PE identification (CLASS0_ID, ..., TMU0_ID, ..., UTIL_ID)
 * @param[in] addr		DMEM read address (must be aligned on size)
 * @param[in] size		Number of bytes to read (maximum 4, must not cross 32bit boundaries)
 * @return			the data read (in PE endianess, i.e BE).
 */
u32 pe_dmem_read(int id, u32 addr, u8 size)
{
	u32 offset = addr & 0x3;
	u32 mask = 0xffffffff >> ((4 - size) << 3);
	u32 val;

	addr = pe[id].dmem_base_addr | (addr & ~0x3) | PE_MEM_ACCESS_DMEM | PE_MEM_ACCESS_BYTE_ENABLE(offset, size);

	writel(addr, pe[id].mem_access_addr);

	/* Indirect access interface is byte swapping data being read */
	val = be32_to_cpu(readl(pe[id].mem_access_rdata));

	return (val >> (offset << 3)) & mask;
}


/** This function is used to write to CLASS internal bus peripherals (ccu, pe-lem) from the host
* through indirect access registers.
* @param[in]	val	value to write
* @param[in]	addr	Address to write to (must be aligned on size)
* @param[in]	size	Number of bytes to write (1, 2 or 4)
*
*/
void class_bus_write(u32 val, u32 addr, u8 size)
{
	u32 offset = addr & 0x3;

	writel((addr & CLASS_BUS_ACCESS_BASE_MASK), CLASS_BUS_ACCESS_BASE);

	addr = (addr & ~CLASS_BUS_ACCESS_BASE_MASK) | PE_MEM_ACCESS_WRITE | (size << 24);

	writel(cpu_to_be32(val << (offset << 3)), CLASS_BUS_ACCESS_WDATA);
	writel(addr, CLASS_BUS_ACCESS_ADDR);
}


/** Reads from CLASS internal bus peripherals (ccu, pe-lem) from the host
* through indirect access registers.
* @param[in] addr	Address to read from (must be aligned on size)
* @param[in] size	Number of bytes to read (1, 2 or 4)
* @return		the read data
*
*/
u32 class_bus_read(u32 addr, u8 size)
{
	u32 offset = addr & 0x3;
	u32 mask = 0xffffffff >> ((4 - size) << 3);
	u32 val;

	writel((addr & CLASS_BUS_ACCESS_BASE_MASK), CLASS_BUS_ACCESS_BASE);

	addr = (addr & ~CLASS_BUS_ACCESS_BASE_MASK) | (size << 24);

	writel(addr, CLASS_BUS_ACCESS_ADDR);
	val = be32_to_cpu(readl(CLASS_BUS_ACCESS_RDATA));

	return (val >> (offset << 3)) & mask;
}


/** Writes data to the cluster memory (PE_LMEM)
* @param[in] dst	PE LMEM destination address (must be 32bit aligned)
* @param[in] src	Buffer source address
* @param[in] len	Number of bytes to copy
*/
void class_pe_lmem_memcpy_to32(u32 dst, const void *src, unsigned int len)
{
	u32 len32 = len >> 2;
	int i;

	for (i = 0; i < len32; i++, src += 4, dst += 4)
		class_bus_write(*(u32 *)src, dst, 4);

	if (len & 0x2)
	{
		class_bus_write(*(u16 *)src, dst, 2);
		src += 2;
		dst += 2;
	}

	if (len & 0x1)
	{
		class_bus_write(*(u8 *)src, dst, 1);
		src++;
		dst++;
	}
}

/** Writes value to the cluster memory (PE_LMEM)
* @param[in] dst	PE LMEM destination address (must be 32bit aligned)
* @param[in] val	Value to write
* @param[in] len	Number of bytes to write
*/
void class_pe_lmem_memset(u32 dst, int val, unsigned int len)
{
	u32 len32 = len >> 2;
	int i;

	val = val | (val << 8) | (val << 16) | (val << 24);
	
	for (i = 0; i < len32; i++, dst += 4)
		class_bus_write(val, dst, 4);

	if (len & 0x2)
	{
		class_bus_write(val, dst, 2);
		dst += 2;
	}

	if (len & 0x1)
	{
		class_bus_write(val, dst, 1);
		dst++;
	}
}


/** Writes UTIL program memory (DDR) from the host.
 *
 * @param[in] addr	Address to write (virtual, must be aligned on size)
 * @param[in] val		Value to write (in PE endianess, i.e BE)
 * @param[in] size		Number of bytes to write (2 or 4)
 */
static void util_pmem_write(u32 val, void *addr, u8 size)
{
	void *addr64 = (void *)((unsigned long)addr & ~0x7);
	unsigned long off = 8 - ((unsigned long)addr & 0x7) - size;
	
	//IMEM should  be loaded as a 64bit swapped value in a 64bit aligned location
	if (size == 4)
		writel(be32_to_cpu(val), addr64 + off);
	else
		writew(be16_to_cpu((u16)val), addr64 + off);
}


/** Writes a buffer to UTIL program memory (DDR) from the host.
 *
 * @param[in] dst	Address to write (virtual, must be at least 16bit aligned)
 * @param[in] src	Buffer to write (in PE endianess, i.e BE, must have same alignment as dst)
 * @param[in] len	Number of bytes to write (must be at least 16bit aligned)
 */
static void util_pmem_memcpy(void *dst, const void *src, unsigned int len)
{
	unsigned int len32;
	int i;

	if ((unsigned long)src & 0x2) {
		util_pmem_write(*(u16 *)src, dst, 2);
		src += 2;
		dst += 2;
		len -= 2;
	}

	len32 = len >> 2;

	for (i = 0; i < len32; i++, dst += 4, src += 4)
		util_pmem_write(*(u32 *)src, dst, 4);

	if (len & 0x2)
		util_pmem_write(*(u16 *)src, dst, len & 0x2);
}

/** Loads an elf section into pmem
 * Code needs to be at least 16bit aligned and only PROGBITS sections are supported
 *
 * @param[in] id	PE identification (CLASS0_ID, ..., TMU0_ID, ..., TMU3_ID)
 * @param[in] data	pointer to the elf firmware
 * @param[in] shdr	pointer to the elf section header
 *
 */
static int pe_load_pmem_section(int id, const void *data, Elf32_Shdr *shdr)
{
	u32 offset = be32_to_cpu(shdr->sh_offset);
	u32 addr = be32_to_cpu(shdr->sh_addr);
	u32 size = be32_to_cpu(shdr->sh_size);
	u32 type = be32_to_cpu(shdr->sh_type);

#if !defined(CONFIG_UTIL_DISABLED)
	if (id == UTIL_ID)
	{
		printk(KERN_ERR "%s: unsuported pmem section for UTIL\n", __func__);
		return -EINVAL;
	}
#endif

	if (((unsigned long)(data + offset) & 0x3) != (addr & 0x3))
	{
		printk(KERN_ERR "%s: load address(%x) and elf file address(%lx) don't have the same alignment\n",
			__func__, addr, (unsigned long) data + offset);

		return -EINVAL;
	}

	if (addr & 0x1)
	{
		printk(KERN_ERR "%s: load address(%x) is not 16bit aligned\n", __func__, addr);
		return -EINVAL;
	}

	if (size & 0x1)
	{
		printk(KERN_ERR "%s: load size(%x) is not 16bit aligned\n", __func__, size);
		return -EINVAL;
	}

	switch (type)
        {
        case SHT_PROGBITS:
		pe_pmem_memcpy_to32(id, addr, data + offset, size);

		break;

	default:
		printk(KERN_ERR "%s: unsuported section type(%x)\n", __func__, type);
		return -EINVAL;
		break;
	}

	return 0;
}


/** Loads an elf section into dmem
 * Data needs to be at least 32bit aligned, NOBITS sections are correctly initialized to 0
 *
 * @param[in] id		PE identification (CLASS0_ID, ..., TMU0_ID, ..., UTIL_ID)
 * @param[in] data		pointer to the elf firmware
 * @param[in] shdr		pointer to the elf section header
 *
 */
static int pe_load_dmem_section(int id, const void *data, Elf32_Shdr *shdr)
{
	u32 offset = be32_to_cpu(shdr->sh_offset);
	u32 addr = be32_to_cpu(shdr->sh_addr);
	u32 size = be32_to_cpu(shdr->sh_size);
	u32 type = be32_to_cpu(shdr->sh_type);
	u32 size32 = size >> 2;
	int i;

	if (((unsigned long)(data + offset) & 0x3) != (addr & 0x3))
	{
		printk(KERN_ERR "%s: load address(%x) and elf file address(%lx) don't have the same alignment\n",
			__func__, addr, (unsigned long)data + offset);

		return -EINVAL;
	}

	if (addr & 0x3)
	{
		printk(KERN_ERR "%s: load address(%x) is not 32bit aligned\n", __func__, addr);
		return -EINVAL;
	}

	switch (type)
        {
        case SHT_PROGBITS:
		pe_dmem_memcpy_to32(id, addr, data + offset, size);
		break;

	case SHT_NOBITS:
		for (i = 0; i < size32; i++, addr += 4)
			pe_dmem_write(id, 0, addr, 4);

		if (size & 0x3)
			pe_dmem_write(id, 0, addr, size & 0x3);

		break;

	default:
		printk(KERN_ERR "%s: unsuported section type(%x)\n", __func__, type);
		return -EINVAL;
		break;
	}

	return 0;
}


/** Loads an elf section into DDR
 * Data needs to be at least 32bit aligned, NOBITS sections are correctly initialized to 0
 *
 * @param[in] id		PE identification (CLASS0_ID, ..., TMU0_ID, ..., UTIL_ID)
 * @param[in] data		pointer to the elf firmware
 * @param[in] shdr		pointer to the elf section header
 *
 */
static int pe_load_ddr_section(int id, const void *data, Elf32_Shdr *shdr)
{
	u32 offset = be32_to_cpu(shdr->sh_offset);
	u32 addr = be32_to_cpu(shdr->sh_addr);
	u32 size = be32_to_cpu(shdr->sh_size);
	u32 type = be32_to_cpu(shdr->sh_type);
	u32 flags = be32_to_cpu(shdr->sh_flags);

	switch (type)
	{
	case SHT_PROGBITS:
		if (flags & SHF_EXECINSTR)
		{
			if (id <= CLASS_MAX_ID)
			{
				/* DO the loading only once in DDR */
				if (id == CLASS0_ID)
				{
					printk(KERN_ERR "%s: load address(%x) and elf file address(%lx) rcvd\n", __func__, addr, (unsigned long)data + offset);
					if (((unsigned long)(data + offset) & 0x3) != (addr & 0x3))
					{
						printk(KERN_ERR "%s: load address(%x) and elf file address(%lx) don't have the same alignment\n",
								__func__, addr, (unsigned long)data + offset);

						return -EINVAL;
					}

					if (addr & 0x1)
					{
						printk(KERN_ERR "%s: load address(%x) is not 16bit aligned\n", __func__, addr);
						return -EINVAL;
					}

					if (size & 0x1)
					{
						printk(KERN_ERR "%s: load length(%x) is not 16bit aligned\n", __func__, size);
						return -EINVAL;
					}

					memcpy(DDR_PHYS_TO_VIRT(addr), data + offset, size);
				}
			}
#if !defined(CONFIG_UTIL_DISABLED)
			else if (id == UTIL_ID)
			{
				if (((unsigned long)(data + offset) & 0x3) != (addr & 0x3))
				{
					printk(KERN_ERR "%s: load address(%x) and elf file address(%lx) don't have the same alignment\n",
							__func__, addr, (unsigned long)data + offset);

					return -EINVAL;
				}

				if (addr & 0x1)
				{
					printk(KERN_ERR "%s: load address(%x) is not 16bit aligned\n", __func__, addr);
					return -EINVAL;
				}

				if (size & 0x1)
				{
					printk(KERN_ERR "%s: load length(%x) is not 16bit aligned\n", __func__, size);
					return -EINVAL;
				}

				util_pmem_memcpy(DDR_PHYS_TO_VIRT(addr), data + offset, size);
			}
#endif
			else
			{
				printk(KERN_ERR "%s: unsuported ddr section type(%x) for PE(%d)\n", __func__, type, id);
				return -EINVAL;
			}

		}
		else
		{
			memcpy(DDR_PHYS_TO_VIRT(addr), data + offset, size);
		}

		break;

	case SHT_NOBITS:
		memset(DDR_PHYS_TO_VIRT(addr), 0, size);

		break;

	default:
		printk(KERN_ERR "%s: unsuported section type(%x)\n", __func__, type);
		return -EINVAL;
		break;
	}

	return 0;
}


/** Loads an elf section into pe lmem
 * Data needs to be at least 32bit aligned, NOBITS sections are correctly initialized to 0
 *
 * @param[in] id		PE identification (CLASS0_ID,..., CLASS5_ID)
 * @param[in] data		pointer to the elf firmware
 * @param[in] shdr		pointer to the elf section header
 *
 */
static int pe_load_pe_lmem_section(int id, const void *data, Elf32_Shdr *shdr)
{
	u32 offset = be32_to_cpu(shdr->sh_offset);
	u32 addr = be32_to_cpu(shdr->sh_addr);
	u32 size = be32_to_cpu(shdr->sh_size);
	u32 type = be32_to_cpu(shdr->sh_type);

	if (id > CLASS_MAX_ID)
	{
		printk(KERN_ERR "%s: unsuported pe-lmem section type(%x) for PE(%d)\n", __func__, type, id);
		return -EINVAL;
	}
	
	if (((unsigned long)(data + offset) & 0x3) != (addr & 0x3))
	{
		printk(KERN_ERR "%s: load address(%x) and elf file address(%lx) don't have the same alignment\n",
			__func__, addr, (unsigned long)data + offset);

		return -EINVAL;
	}

	if (addr & 0x3)
	{
		printk(KERN_ERR "%s: load address(%x) is not 32bit aligned\n", __func__, addr);
		return -EINVAL;
	}

	switch (type)
	{
	case SHT_PROGBITS:
		class_pe_lmem_memcpy_to32(addr, data + offset, size);
		break;

	case SHT_NOBITS:
		class_pe_lmem_memset(addr, 0, size);
		break;

	default:
		printk(KERN_ERR "%s: unsuported section type(%x)\n", __func__, type);
		return -EINVAL;
		break;
	}

	return 0;
}


/** Loads an elf section into a PE
 * For now only supports loading a section to dmem (all PE's), pmem (class and tmu PE's),
 * DDDR (util PE code)
 *
 * @param[in] id		PE identification (CLASS0_ID, ..., TMU0_ID, ..., UTIL_ID)
 * @param[in] data		pointer to the elf firmware
 * @param[in] shdr		pointer to the elf section header
 *
 */
int pe_load_elf_section(int id, const void *data, Elf32_Shdr *shdr)
{
	u32 addr = be32_to_cpu(shdr->sh_addr);
	u32 size = be32_to_cpu(shdr->sh_size);

	if (IS_DMEM(addr, size))
		return pe_load_dmem_section(id, data, shdr);
	else if (IS_PMEM(addr, size))
		return pe_load_pmem_section(id, data, shdr);
	else if (IS_PFE_LMEM(addr, size))
		return 0; /* FIXME */
	else if (IS_PHYS_DDR(addr, size))
		return pe_load_ddr_section(id, data, shdr);
	else if (IS_PE_LMEM(addr, size))
		return pe_load_pe_lmem_section(id, data, shdr); 
	else {
		printk(KERN_ERR "%s: unsuported memory range(%x)\n", __func__, addr);
//		return -EINVAL;
	}

	return 0;
}


/**************************** BMU ***************************/

/** Initializes a BMU block.
* @param[in] base	BMU block base address
* @param[in] cfg	BMU configuration
*/
void bmu_init(void *base, BMU_CFG *cfg)
{
	bmu_disable(base);

	bmu_set_config(base, cfg);

	bmu_reset(base);
}

/** Resets a BMU block.
* @param[in] base	BMU block base address
*/
void bmu_reset(void *base)
{
	writel(CORE_SW_RESET, base + BMU_CTRL);

	/* Wait for self clear */
	while (readl(base + BMU_CTRL) & CORE_SW_RESET) ;
}

/** Enabled a BMU block.
* @param[in] base	BMU block base address
*/
void bmu_enable(void *base)
{
	writel (CORE_ENABLE, base + BMU_CTRL);
}

/** Disables a BMU block.
* @param[in] base	BMU block base address
*/
void bmu_disable(void *base)
{
	writel (CORE_DISABLE, base + BMU_CTRL);
}

/** Sets the configuration of a BMU block.
* @param[in] base	BMU block base address
* @param[in] cfg	BMU configuration
*/
void bmu_set_config(void *base, BMU_CFG *cfg)
{	
	writel (cfg->baseaddr, base + BMU_UCAST_BASE_ADDR);
	writel (cfg->count & 0xffff, base + BMU_UCAST_CONFIG);
	writel (cfg->size & 0xffff, base + BMU_BUF_SIZE);
//	writel (BMU1_THRES_CNT, base + BMU_THRES);

	/* Interrupts are never used */
//	writel (0x0, base + BMU_INT_SRC);
	writel (0x0, base + BMU_INT_ENABLE);
}

/**************************** GEMAC ***************************/

/** Enable Rx Checksum Engine. With this enabled, Frame with bad IP, 
 *   TCP or UDP checksums are discarded
 *
 * @param[in] base	GEMAC base address. 
 */
void gemac_enable_rx_checksum_offload(void *base)
{
	writel(readl(base + EMAC_NETWORK_CONFIG) | EMAC_ENABLE_CHKSUM_RX, base + EMAC_NETWORK_CONFIG);
	writel(readl(CLASS_L4_CHKSUM_ADDR) | IPV4_CHKSUM_DROP, CLASS_L4_CHKSUM_ADDR);
}

/** Disable Rx Checksum Engine.
 *
 * @param[in] base	GEMAC base address. 
 */
void gemac_disable_rx_checksum_offload(void *base)
{
	writel(readl(base + EMAC_NETWORK_CONFIG) & ~EMAC_ENABLE_CHKSUM_RX, base + EMAC_NETWORK_CONFIG);
	writel(readl(CLASS_L4_CHKSUM_ADDR) & ~IPV4_CHKSUM_DROP, CLASS_L4_CHKSUM_ADDR);
}

/** Setup the MII Mgmt clock speed.
 * @param[in] base	GEMAC base address (GEMAC0, GEMAC1, GEMAC2)
 * @param[in] mdc_div	MII clock dividor
 */
void gemac_set_mdc_div(void *base, int mdc_div)
{
	u32 val = readl(base + EMAC_NETWORK_CONFIG) & ~EMAC_MDC_DIV_MASK;
	u32 div;

        switch (mdc_div) {
        case 8:
                div = 0;
                break;

        case 16:
                div = 1;
                break;

        case 32:
                div = 2;
                break;

        case 48:
                div = 3;
                break;

        default:
        case 64:
                div = 4;
                break;

        case 96:
                div = 5;
                break;

        case 128:
                div = 6;
                break;

        case 224:
                div = 7;
                break;
        }

        val |= div << 18;

        writel(val, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC set speed.
* @param[in] base	GEMAC base address
* @param[in] speed	GEMAC speed (10, 100 or 1000 Mbps)
*/
void gemac_set_speed(void *base, MAC_SPEED gem_speed)
{
	u32 val = readl(base + EMAC_NETWORK_CONFIG);

	val = val & ~EMAC_SPEED_MASK;

	switch (gem_speed)
	{
		case SPEED_10M:
			val &= (~EMAC_PCS_ENABLE);
			break;

		case SPEED_100M:
			val = val | EMAC_SPEED_100;
			val &= (~EMAC_PCS_ENABLE);
			break;

		case SPEED_1000M:
			val = val | EMAC_SPEED_1000;
			val &= (~EMAC_PCS_ENABLE);
			break;

		case SPEED_1000M_PCS:
			val = val | EMAC_SPEED_1000;
			val |= EMAC_PCS_ENABLE;
			break;

		default:
			val = val | EMAC_SPEED_100;
			val &= (~EMAC_PCS_ENABLE);
		break;
	}
	
	writel (val, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC set duplex.
* @param[in] base	GEMAC base address
* @param[in] duplex	GEMAC duplex mode (Full, Half)
*/
void gemac_set_duplex(void *base, int duplex)
{
	u32 val = readl(base + EMAC_NETWORK_CONFIG);

	if (duplex == DUPLEX_HALF)
		val = (val & ~EMAC_DUPLEX_MASK) | EMAC_HALF_DUP;
	else
		val = (val & ~EMAC_DUPLEX_MASK) | EMAC_FULL_DUP;
  
	writel (val, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC set mode.
* @param[in] base	GEMAC base address
* @param[in] mode	GEMAC operation mode (MII, RMII, RGMII, SGMII)
*/

#if defined(CONFIG_IP_ALIGNED)
#define IP_ALIGNED_BITVAL	EMAC_TWO_BYTES_IP_ALIGN
#else
#define	IP_ALIGNED_BITVAL	0
#endif

void gemac_set_mode(void *base, int mode)
{
	switch (mode)
	{
	case GMII:
		writel ((readl(base + EMAC_CONTROL) & ~EMAC_MODE_MASK) | EMAC_GMII_MODE_ENABLE | IP_ALIGNED_BITVAL, base + EMAC_CONTROL);
		writel (readl(base + EMAC_NETWORK_CONFIG) & (~EMAC_SGMII_MODE_ENABLE), base + EMAC_NETWORK_CONFIG);
		break;

	case RGMII:
		writel ((readl(base + EMAC_CONTROL) & ~EMAC_MODE_MASK) | EMAC_RGMII_MODE_ENABLE | IP_ALIGNED_BITVAL, base + EMAC_CONTROL);
		writel (readl(base + EMAC_NETWORK_CONFIG) & (~EMAC_SGMII_MODE_ENABLE), base + EMAC_NETWORK_CONFIG);
		break;

	case RMII:
		writel ((readl(base + EMAC_CONTROL) & ~EMAC_MODE_MASK) | EMAC_RMII_MODE_ENABLE | IP_ALIGNED_BITVAL, base + EMAC_CONTROL);
		writel (readl(base + EMAC_NETWORK_CONFIG) & (~EMAC_SGMII_MODE_ENABLE), base + EMAC_NETWORK_CONFIG);
		break;

	case MII:
		writel ((readl(base + EMAC_CONTROL) & ~EMAC_MODE_MASK) | EMAC_MII_MODE_ENABLE | IP_ALIGNED_BITVAL, base + EMAC_CONTROL);
		writel (readl(base + EMAC_NETWORK_CONFIG) & (~EMAC_SGMII_MODE_ENABLE), base + EMAC_NETWORK_CONFIG);
		break;

	case SGMII:
		writel ((readl(base + EMAC_CONTROL) & ~EMAC_MODE_MASK) | (EMAC_RMII_MODE_DISABLE | EMAC_RGMII_MODE_DISABLE) | IP_ALIGNED_BITVAL, base + EMAC_CONTROL);
		writel (readl(base + EMAC_NETWORK_CONFIG) | EMAC_SGMII_MODE_ENABLE, base + EMAC_NETWORK_CONFIG);
		break;

	default:
		writel ((readl(base + EMAC_CONTROL) & ~EMAC_MODE_MASK) | EMAC_MII_MODE_ENABLE | IP_ALIGNED_BITVAL, base + EMAC_CONTROL);
		writel (readl(base + EMAC_NETWORK_CONFIG) & (~EMAC_SGMII_MODE_ENABLE), base + EMAC_NETWORK_CONFIG);
		break;
	}
}
/** GEMAC Enable MDIO: Activate the Management interface.  This is required to program the PHY
 * @param[in] base       GEMAC base address
 */
void gemac_enable_mdio(void *base)
{
        u32 data;

        data = readl(base + EMAC_NETWORK_CONTROL);
        data |= EMAC_MDIO_EN;
        writel(data, base + EMAC_NETWORK_CONTROL);
}

/** GEMAC Disable MDIO: Disable the Management interface.
 * @param[in] base       GEMAC base address
 */
void gemac_disable_mdio(void *base)
{
        u32 data;

        data = readl(base + EMAC_NETWORK_CONTROL);
        data &= ~EMAC_MDIO_EN;
        writel(data, base + EMAC_NETWORK_CONTROL);
}


/** GEMAC reset function.
* @param[in] base	GEMAC base address
*/
void gemac_reset(void *base)
{  
}

/** GEMAC enable function.
* @param[in] base	GEMAC base address
*/
void gemac_enable(void *base)
{  
	writel (readl(base + EMAC_NETWORK_CONTROL) | EMAC_TX_ENABLE | EMAC_RX_ENABLE, base + EMAC_NETWORK_CONTROL);
}

/** GEMAC disable function.
* @param[in] base	GEMAC base address
*/
void gemac_disable(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONTROL) & ~(EMAC_TX_ENABLE | EMAC_RX_ENABLE), base + EMAC_NETWORK_CONTROL);
}

/** GEMAC TX disable function.
* @param[in] base	GEMAC base address
*/
void gemac_tx_disable(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONTROL) & ~(EMAC_TX_ENABLE), base + EMAC_NETWORK_CONTROL);
}

/** GEMAC set mac address configuration.
* @param[in] base	GEMAC base address
* @param[in] addr	MAC address to be configured
*/
void gemac_set_address(void *base, SPEC_ADDR *addr)
{ 
	writel(addr->one.bottom,	base + EMAC_SPEC1_ADD_BOT);
	writel(addr->one.top,		base + EMAC_SPEC1_ADD_TOP); 
	writel(addr->two.bottom,	base + EMAC_SPEC2_ADD_BOT);
	writel(addr->two.top,		base + EMAC_SPEC2_ADD_TOP);
	writel(addr->three.bottom,	base + EMAC_SPEC3_ADD_BOT);
	writel(addr->three.top,		base + EMAC_SPEC3_ADD_TOP);
	writel(addr->four.bottom,	base + EMAC_SPEC4_ADD_BOT);
	writel(addr->four.top,		base + EMAC_SPEC4_ADD_TOP);
} 

/** GEMAC get mac address configuration.
* @param[in] base	GEMAC base address
*
* @return		MAC addresses configured
*/
SPEC_ADDR gemac_get_address(void *base)
{
	SPEC_ADDR addr;
	
	addr.one.bottom = 	readl(base + EMAC_SPEC1_ADD_BOT);
	addr.one.top = 		readl(base + EMAC_SPEC1_ADD_TOP); 
	addr.two.bottom =	readl(base + EMAC_SPEC2_ADD_BOT);
	addr.two.top =		readl(base + EMAC_SPEC2_ADD_TOP);
	addr.three.bottom =	readl(base + EMAC_SPEC3_ADD_BOT);
	addr.three.top =	readl(base + EMAC_SPEC3_ADD_TOP);
	addr.four.bottom =	readl(base + EMAC_SPEC4_ADD_BOT);
	addr.four.top =		readl(base + EMAC_SPEC4_ADD_TOP);
	
	return addr;
}

/** Sets the hash register of the MAC. 
 * This register is used for matching unicast and multicast frames.
 *
 * @param[in] base	GEMAC base address.
 * @param[in] hash	64-bit hash to be configured.	
 */
void gemac_set_hash( void *base, MAC_ADDR *hash )
{
	writel(hash->bottom,		base + EMAC_HASH_BOT);
	writel(hash->top,		base + EMAC_HASH_TOP); 
}

/** Get the current value hash register of the MAC. 
 * This register is used for matching unicast and multicast frames.
 *
 * @param[in] base	GEMAC base address

 * @returns		64-bit hash.	
 */
MAC_ADDR gemac_get_hash( void *base )
{
	MAC_ADDR hash;

	hash.bottom	= readl(base + EMAC_HASH_BOT);
	hash.top	= readl(base + EMAC_HASH_TOP);

	return hash;
}

/** GEMAC set specific local addresses of the MAC.
* Rather than setting up all four specific addresses, this function sets them up individually.
*
* @param[in] base	GEMAC base address
* @param[in] addr	MAC address to be configured
*/
void gemac_set_laddr1(void *base, MAC_ADDR *address)
{
	writel(address->bottom,		base + EMAC_SPEC1_ADD_BOT);
	writel(address->top,		base + EMAC_SPEC1_ADD_TOP); 
}


void gemac_set_laddr2(void *base, MAC_ADDR *address)
{
	writel(address->bottom,		base + EMAC_SPEC2_ADD_BOT);
	writel(address->top,		base + EMAC_SPEC2_ADD_TOP); 
}


void gemac_set_laddr3(void *base, MAC_ADDR *address)
{
	writel(address->bottom,		base + EMAC_SPEC3_ADD_BOT);
	writel(address->top,		base + EMAC_SPEC3_ADD_TOP); 
}


void gemac_set_laddr4(void *base, MAC_ADDR *address)
{
	writel(address->bottom,		base + EMAC_SPEC4_ADD_BOT);
	writel(address->top,		base + EMAC_SPEC4_ADD_TOP); 
}

void gemac_set_laddrN(void *base, MAC_ADDR *address, unsigned int entry_index)
{
	if( (entry_index < 1) || (entry_index > EMAC_SPEC_ADDR_MAX) )
		return;

	entry_index = entry_index - 1;

	if (entry_index < 4)
	{	
		writel(address->bottom,		base + (entry_index * 8) + EMAC_SPEC1_ADD_BOT);
		writel(address->top,		base + (entry_index * 8) + EMAC_SPEC1_ADD_TOP);
	} 
	else 
	{
		writel(address->bottom,		base + ((entry_index - 4) * 8) + EMAC_SPEC5_ADD_BOT);
		writel(address->top,		base + ((entry_index - 4) * 8) + EMAC_SPEC5_ADD_TOP);
	}
}

/** Get specific local addresses of the MAC.
* This allows returning of a single specific address stored in the MAC.
* @param[in] base	GEMAC base address
*
* @return		Specific MAC address 1
* 
*/
MAC_ADDR gem_get_laddr1(void *base)
{
	MAC_ADDR addr;
	addr.bottom = readl(base + EMAC_SPEC1_ADD_BOT);
	addr.top = readl(base + EMAC_SPEC1_ADD_TOP);
	return addr;
}


MAC_ADDR gem_get_laddr2(void *base)
{
	MAC_ADDR addr;
	addr.bottom = readl(base + EMAC_SPEC2_ADD_BOT);
	addr.top = readl(base + EMAC_SPEC2_ADD_TOP);
	return addr;
}


MAC_ADDR gem_get_laddr3(void *base)
{
	MAC_ADDR addr;
	addr.bottom = readl(base + EMAC_SPEC3_ADD_BOT);
	addr.top = readl(base + EMAC_SPEC3_ADD_TOP);
	return addr;
}


MAC_ADDR gem_get_laddr4(void *base)
{
	MAC_ADDR addr;
	addr.bottom = readl(base + EMAC_SPEC4_ADD_BOT);
	addr.top = readl(base + EMAC_SPEC4_ADD_TOP);
	return addr;
}

MAC_ADDR gem_get_laddrN(void *base, unsigned int entry_index)
{
	MAC_ADDR addr = {0xffffffff, 0xffffffff};

	if( (entry_index < 1) || (entry_index > EMAC_SPEC_ADDR_MAX) )
		return addr;

	entry_index = entry_index - 1;

	if (entry_index < 4)
	{
		addr.bottom = readl(base + (entry_index * 8) + EMAC_SPEC1_ADD_BOT);
		addr.top = readl(base + (entry_index * 8) + EMAC_SPEC1_ADD_TOP);
	}
	else
	{
		addr.bottom = readl(base + ((entry_index - 4) * 8) + EMAC_SPEC5_ADD_BOT);
		addr.top = readl(base + ((entry_index - 4) * 8) + EMAC_SPEC5_ADD_TOP);
	}

	return addr;
}

/** Clear specific local addresses of the MAC.
 * @param[in] base       GEMAC base address
 */

void gemac_clear_laddr1(void *base)
{
	writel(0, base + EMAC_SPEC1_ADD_BOT);
}

void gemac_clear_laddr2(void *base)
{
	writel(0, base + EMAC_SPEC2_ADD_BOT);
}

void gemac_clear_laddr3(void *base)
{
	writel(0, base + EMAC_SPEC3_ADD_BOT);
}

void gemac_clear_laddr4(void *base)
{
	writel(0, base + EMAC_SPEC4_ADD_BOT);
}

void gemac_clear_laddrN(void *base, unsigned int entry_index)
{
	if( (entry_index < 1) || (entry_index > EMAC_SPEC_ADDR_MAX) )
		return;

	entry_index = entry_index - 1;

	if ( entry_index < 4 )
		writel(0, base + (entry_index * 8) + EMAC_SPEC1_ADD_BOT);
	else
		writel(0, base + ((entry_index - 4) * 8) + EMAC_SPEC5_ADD_BOT);
}

/** Set the loopback mode of the MAC.  This can be either no loopback for normal
 *   operation, local loopback through MAC internal loopback module or PHY
 *   loopback for external loopback through a PHY.  This asserts the external loop
 *   pin.
 * 
 * @param[in] base	GEMAC base address.
 * @param[in] gem_loop	Loopback mode to be enabled. LB_LOCAL - MAC Loopback, 
 *			LB_EXT - PHY Loopback.  
 */
void gemac_set_loop( void *base, MAC_LOOP gem_loop )
{
	switch (gem_loop) {
		case LB_LOCAL:
			writel(readl(base + EMAC_NETWORK_CONTROL) & (~EMAC_LB_PHY),
							base + EMAC_NETWORK_CONTROL);
			writel(readl(base + EMAC_NETWORK_CONTROL) | (EMAC_LB_MAC),
							base + EMAC_NETWORK_CONTROL);
			break;
		case LB_EXT:
			writel(readl(base + EMAC_NETWORK_CONTROL) & (~EMAC_LB_MAC),
							base + EMAC_NETWORK_CONTROL);
			writel(readl(base + EMAC_NETWORK_CONTROL) | (EMAC_LB_PHY),
							base + EMAC_NETWORK_CONTROL);
			break;
		default:
			writel(readl(base + EMAC_NETWORK_CONTROL) & (~(EMAC_LB_MAC | EMAC_LB_PHY)),
									base + EMAC_NETWORK_CONTROL);
	}
}

/** GEMAC allow frames
 * @param[in] base	GEMAC base address
 */
void gemac_enable_copy_all(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) | EMAC_ENABLE_COPY_ALL, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC do not allow frames
 * @param[in] base	GEMAC base address
*/
void gemac_disable_copy_all(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) & ~EMAC_ENABLE_COPY_ALL, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC allow broadcast function.
* @param[in] base	GEMAC base address
*/
void gemac_allow_broadcast(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) & ~EMAC_NO_BROADCAST, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC no broadcast function.
* @param[in] base	GEMAC base address
*/
void gemac_no_broadcast(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) | EMAC_NO_BROADCAST, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC enable unicast function.
* @param[in] base	GEMAC base address
*/
void gemac_enable_unicast(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) | EMAC_ENABLE_UNICAST, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC disable unicast function.
* @param[in] base	GEMAC base address
*/
void gemac_disable_unicast(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) & ~EMAC_ENABLE_UNICAST, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC enable multicast function.
* @param[in] base	GEMAC base address
*/
void gemac_enable_multicast(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) | EMAC_ENABLE_MULTICAST, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC disable multicast function.
* @param[in]	base	GEMAC base address
*/
void gemac_disable_multicast(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) & ~EMAC_ENABLE_MULTICAST, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC enable fcs rx function.
* @param[in]	base	GEMAC base address
*/
void gemac_enable_fcs_rx(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) | EMAC_ENABLE_FCS_RX, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC disable fcs rx function.
* @param[in]	base	GEMAC base address
*/
void gemac_disable_fcs_rx(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) & ~EMAC_ENABLE_FCS_RX, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC enable 1536 rx function.
* @param[in]	base	GEMAC base address
*/
void gemac_enable_1536_rx(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) | EMAC_ENABLE_1536_RX, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC disable 1536 rx function.
* @param[in]	base	GEMAC base address
*/
void gemac_disable_1536_rx(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) & ~EMAC_ENABLE_1536_RX, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC enable jumbo function.
* @param[in]	base	GEMAC base address
*/
void gemac_enable_rx_jmb(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) | EMAC_ENABLE_JUMBO_FRAME, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC disable jumbo function.
* @param[in]	base	GEMAC base address
*/
void gemac_disable_rx_jmb(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) & ~EMAC_ENABLE_JUMBO_FRAME, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC enable stacked vlan function.
* @param[in]	base	GEMAC base address
*/
void gemac_enable_stacked_vlan(void *base)
{
	writel (readl(base + EMAC_STACKED_VLAN_REG) | EMAC_ENABLE_STACKED_VLAN, base + EMAC_STACKED_VLAN_REG);
}

/** GEMAC enable stacked vlan function.
* @param[in]	base	GEMAC base address
*/
void gemac_disable_stacked_vlan(void *base)
{
	writel (readl(base + EMAC_STACKED_VLAN_REG) & ~EMAC_ENABLE_STACKED_VLAN, base + EMAC_STACKED_VLAN_REG);
}

/** GEMAC enable pause rx function.
* @param[in] base	GEMAC base address
*/
void gemac_enable_pause_rx(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) | EMAC_ENABLE_PAUSE_RX, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC disable pause rx function.
* @param[in] base	GEMAC base address
*/
void gemac_disable_pause_rx(void *base)
{
	writel (readl(base + EMAC_NETWORK_CONFIG) & ~EMAC_ENABLE_PAUSE_RX, base + EMAC_NETWORK_CONFIG);
}

/** GEMAC wol configuration
* @param[in] base	GEMAC base address
* @param[in] wol_conf	WoL register configuration
*/
void gemac_set_wol(void *base, u32 wol_conf)
{
	writel(wol_conf, base + EMAC_WOL);
}

/** Sets Gemac bus width to 64bit
 * @param[in] base       GEMAC base address
 * @param[in] width     gemac bus width to be set possible values are 32/64/128
 * */
void gemac_set_bus_width(void *base, int width)
{
	u32 val = readl(base + EMAC_NETWORK_CONFIG);
	switch(width)
	{
		/* break or fallthrough? */
		case 32:
			val = (val & ~EMAC_DATA_BUS_WIDTH_MASK) | EMAC_DATA_BUS_WIDTH_32;
			fallthrough;
		case 128:
			val = (val & ~EMAC_DATA_BUS_WIDTH_MASK) | EMAC_DATA_BUS_WIDTH_128;
			fallthrough;
		case 64:
			fallthrough;
		default:
			val = (val & ~EMAC_DATA_BUS_WIDTH_MASK) | EMAC_DATA_BUS_WIDTH_64;

	}
	writel (val, base + EMAC_NETWORK_CONFIG);
}

/** Sets Gemac configuration.
* @param[in] base	GEMAC base address
* @param[in] cfg	GEMAC configuration
*/
void gemac_set_config(void *base, GEMAC_CFG *cfg)
{
	gemac_set_mode(base, cfg->mode);

	gemac_set_speed(base, cfg->speed);

	gemac_set_duplex(base,cfg->duplex);
}


/**************************** GPI ***************************/

/** Initializes a GPI block.
* @param[in] base	GPI base address
* @param[in] cfg	GPI configuration
*/
void gpi_init(void *base, GPI_CFG *cfg)
{ 
	gpi_reset(base);

	gpi_disable(base);

	gpi_set_config(base, cfg);
}

/** Resets a GPI block.
* @param[in] base	GPI base address
*/
void gpi_reset(void *base)
{
	writel (CORE_SW_RESET, base + GPI_CTRL);
}

/** Enables a GPI block.
* @param[in] base	GPI base address
*/
void gpi_enable(void *base)
{
	writel (CORE_ENABLE, base + GPI_CTRL);
}

/** Disables a GPI block.
* @param[in] base	GPI base address
*/
void gpi_disable(void *base)
{
	writel (CORE_DISABLE, base + GPI_CTRL);
}


/** Sets the configuration of a GPI block.
* @param[in] base	GPI base address
* @param[in] cfg	GPI configuration
*/
void gpi_set_config(void *base, GPI_CFG *cfg)
{  
	writel (CBUS_VIRT_TO_PFE(BMU1_BASE_ADDR + BMU_ALLOC_CTRL),	base + GPI_LMEM_ALLOC_ADDR);
	writel (CBUS_VIRT_TO_PFE(BMU1_BASE_ADDR + BMU_FREE_CTRL),	base + GPI_LMEM_FREE_ADDR);
	writel (CBUS_VIRT_TO_PFE(BMU2_BASE_ADDR + BMU_ALLOC_CTRL),	base + GPI_DDR_ALLOC_ADDR);
	writel (CBUS_VIRT_TO_PFE(BMU2_BASE_ADDR + BMU_FREE_CTRL),	base + GPI_DDR_FREE_ADDR);
	writel (CBUS_VIRT_TO_PFE(CLASS_INQ_PKTPTR),			base + GPI_CLASS_ADDR);
 	writel (DDR_HDR_SIZE,						base + GPI_DDR_DATA_OFFSET);
	writel (LMEM_HDR_SIZE,						base + GPI_LMEM_DATA_OFFSET);
	writel (0,							base + GPI_LMEM_SEC_BUF_DATA_OFFSET);
	writel (0,							base + GPI_DDR_SEC_BUF_DATA_OFFSET);
	writel ((DDR_HDR_SIZE << 16) | LMEM_HDR_SIZE,			base + GPI_HDR_SIZE);
	writel ((DDR_BUF_SIZE << 16) | LMEM_BUF_SIZE,			base + GPI_BUF_SIZE);
	
	writel (((cfg->lmem_rtry_cnt << 16) | (GPI_DDR_BUF_EN << 1) | GPI_LMEM_BUF_EN),	base + GPI_RX_CONFIG);
	writel (cfg->tmlf_txthres,					base + GPI_TMLF_TX);
	writel (cfg->aseq_len,						base + GPI_DTX_ASEQ);
	writel (1,							base + GPI_TOE_CHKSUM_EN);
}

/**************************** CLASSIFIER ***************************/

/** Initializes CLASSIFIER block.
* @param[in] cfg	CLASSIFIER configuration
*/
void class_init(CLASS_CFG *cfg)
{
	class_reset();
	
	class_disable();
	
	class_set_config(cfg);
}

/** Resets CLASSIFIER block.
*
*/
void class_reset(void)
{
	writel(CORE_SW_RESET, CLASS_TX_CTRL);
}

/** Enables all CLASS-PE's cores.
*
*/
void class_enable(void)
{
	writel(CORE_ENABLE, CLASS_TX_CTRL);
}

/** Disables all CLASS-PE's cores.
*
*/
void class_disable(void)
{
	writel(CORE_DISABLE, CLASS_TX_CTRL); 
}

/** Sets the configuration of the CLASSIFIER block.
* @param[in] cfg	CLASSIFIER configuration
*/
void class_set_config(CLASS_CFG *cfg)
{
	u32 val;

	/* Initialize route table */
	if (!cfg->resume)
		memset(DDR_PHYS_TO_VIRT(cfg->route_table_baseaddr), 0, (1 << cfg->route_table_hash_bits) * CLASS_ROUTE_SIZE);

	writel(cfg->pe_sys_clk_ratio,	CLASS_PE_SYS_CLK_RATIO);

	writel((DDR_HDR_SIZE << 16) | LMEM_HDR_SIZE,	CLASS_HDR_SIZE);
	writel(LMEM_BUF_SIZE,				CLASS_LMEM_BUF_SIZE);
	writel(CLASS_ROUTE_ENTRY_SIZE(CLASS_ROUTE_SIZE) | CLASS_ROUTE_HASH_SIZE(cfg->route_table_hash_bits),	CLASS_ROUTE_HASH_ENTRY_SIZE);
	writel(HIF_PKT_CLASS_EN| HIF_PKT_OFFSET(sizeof(struct hif_hdr)),     CLASS_HIF_PARSE);

	val = HASH_CRC_PORT_IP | QB2BUS_LE;

#if defined(CONFIG_IP_ALIGNED)
	val |= IP_ALIGNED;
#endif

	/* Class PE packet steering will only work if TOE mode, bridge fetch or
	 * route fetch are enabled (see class/qb_fet.v). Route fetch would trigger
	 * additional memory copies (likely from DDR because of hash table size, which
	 * cannot be reduced because PE software still relies on hash value computed
	 *  in HW), so when not in TOE mode we simply enable HW bridge fetch even
	 * though we don't use it.
	 */
	if (cfg->toe_mode)
		val |= CLASS_TOE;
	else
	        val |= HW_BRIDGE_FETCH;

	writel(val, CLASS_ROUTE_MULTI);

	writel(cfg->route_table_baseaddr,		CLASS_ROUTE_TABLE_BASE);
	writel(CLASS_PE0_RO_DM_ADDR0_VAL,		CLASS_PE0_RO_DM_ADDR0);
	writel(CLASS_PE0_RO_DM_ADDR1_VAL,		CLASS_PE0_RO_DM_ADDR1);
	writel(CLASS_PE0_QB_DM_ADDR0_VAL,		CLASS_PE0_QB_DM_ADDR0);
	writel(CLASS_PE0_QB_DM_ADDR1_VAL,		CLASS_PE0_QB_DM_ADDR1);
	writel(CBUS_VIRT_TO_PFE(TMU_PHY_INQ_PKTPTR),	CLASS_TM_INQ_ADDR);

	writel(23, CLASS_AFULL_THRES);
	writel(23, CLASS_TSQ_FIFO_THRES);

	writel(24, CLASS_MAX_BUF_CNT);
	writel(24, CLASS_TSQ_MAX_CNT);
}

/**************************** TMU ***************************/

void tmu_reset(void)
{
	writel(SW_RESET, TMU_CTRL);
}

/** Initializes TMU block.
* @param[in] cfg	TMU configuration
*/
void tmu_init(TMU_CFG *cfg)
{
	int q, phyno;

	/* keep in soft reset */
	writel(SW_RESET,                     TMU_CTRL);
	writel(0x3,						TMU_SYS_GENERIC_CONTROL);
	writel(750,						TMU_INQ_WATERMARK);
	writel(CBUS_VIRT_TO_PFE(EGPI1_BASE_ADDR + GPI_INQ_PKTPTR),	TMU_PHY0_INQ_ADDR);
	writel(CBUS_VIRT_TO_PFE(EGPI2_BASE_ADDR + GPI_INQ_PKTPTR),	TMU_PHY1_INQ_ADDR);
	writel(CBUS_VIRT_TO_PFE(EGPI3_BASE_ADDR + GPI_INQ_PKTPTR),	TMU_PHY2_INQ_ADDR);
	writel(CBUS_VIRT_TO_PFE(HGPI_BASE_ADDR + GPI_INQ_PKTPTR),	TMU_PHY3_INQ_ADDR);
	writel(CBUS_VIRT_TO_PFE(HIF_NOCPY_RX_INQ0_PKTPTR),		TMU_PHY4_INQ_ADDR);
	writel(CBUS_VIRT_TO_PFE(UTIL_INQ_PKTPTR),			TMU_PHY5_INQ_ADDR);
	writel(CBUS_VIRT_TO_PFE(BMU2_BASE_ADDR + BMU_FREE_CTRL), 	TMU_BMU_INQ_ADDR);

	writel(0x3FF,	TMU_TDQ0_SCH_CTRL);	// enabling all 10 schedulers [9:0] of each TDQ 
	writel(0x3FF,	TMU_TDQ1_SCH_CTRL);
	writel(0x3FF,	TMU_TDQ2_SCH_CTRL);
	writel(0x3FF,	TMU_TDQ3_SCH_CTRL);

	writel(cfg->pe_sys_clk_ratio,	TMU_PE_SYS_CLK_RATIO);

	writel(cfg->llm_base_addr,	TMU_LLM_BASE_ADDR);	// Extra packet pointers will be stored from this address onwards
	
	writel(cfg->llm_queue_len,	TMU_LLM_QUE_LEN);
	writel(5,			TMU_TDQ_IIFG_CFG);
	writel(DDR_BUF_SIZE,		TMU_BMU_BUF_SIZE);

	writel(0x0,			TMU_CTRL);

	/* MEM init */
	printk(KERN_INFO "%s: mem init\n", __func__);
	writel(MEM_INIT,	TMU_CTRL);

	while(!(readl(TMU_CTRL) & MEM_INIT_DONE)) ;

	/* LLM init */
	printk(KERN_INFO "%s: lmem init\n", __func__);
	writel(LLM_INIT,	TMU_CTRL);

	while(!(readl(TMU_CTRL) & LLM_INIT_DONE)) ;

	// set up each queue for tail drop
	for (phyno = 0; phyno < 4; phyno++)
	{
		for (q = 0; q < 16; q++)
		{
			u32 qdepth;
			writel((phyno << 8) | q, TMU_TEQ_CTRL);
			writel(1 << 22, TMU_TEQ_QCFG); //Enable tail drop

			if (phyno == 3)
				qdepth = DEFAULT_TMU3_QDEPTH;
			else
				qdepth = (q == 0) ? DEFAULT_Q0_QDEPTH : DEFAULT_MAX_QDEPTH;

			// LOG: 68855
			// The following is a workaround for the reordered packet and BMU2 buffer leakage issue.
			if (CHIP_REVISION() == 0)
				qdepth = 31;

			writel(qdepth << 18, TMU_TEQ_HW_PROB_CFG2);
			writel(qdepth >> 14, TMU_TEQ_HW_PROB_CFG3);
		}
	}

#ifdef CFG_LRO
	/* Set TMU-3 queue 5 (LRO) in no-drop mode */
	writel((3 << 8) | TMU_QUEUE_LRO, TMU_TEQ_CTRL);
	writel(0, TMU_TEQ_QCFG);
#endif

	writel(0x05, TMU_TEQ_DISABLE_DROPCHK);

	writel(0x0, TMU_CTRL);
}

/** Enables TMU-PE cores.
* @param[in] pe_mask	TMU PE mask
*/
void tmu_enable(u32 pe_mask)
{
	writel(readl(TMU_TX_CTRL) | (pe_mask & 0xF), TMU_TX_CTRL);
}

/** Disables TMU cores.
* @param[in] pe_mask	TMU PE mask
*/
void tmu_disable(u32 pe_mask)
{
	writel(readl(TMU_TX_CTRL) & ~(pe_mask & 0xF), TMU_TX_CTRL);
}
/** This will return the tmu queue status
 * @param[in] if_id	gem interface id or TMU index
 * @return		returns the bit mask of busy queues, zero means all queues are empty
 */
u32 tmu_qstatus(u32 if_id)
{
	return cpu_to_be32(pe_dmem_read(TMU0_ID+if_id, PESTATUS_ADDR_TMU + offsetof(PE_STATUS, tmu_qstatus), 4));
}

u32 tmu_pkts_processed(u32 if_id)
{
	return cpu_to_be32(pe_dmem_read(TMU0_ID+if_id, PESTATUS_ADDR_TMU + offsetof(PE_STATUS, rx), 4));
}
/**************************** UTIL ***************************/

/** Resets UTIL block.
*/
void util_reset(void)
{
	writel(CORE_SW_RESET, UTIL_TX_CTRL);
}

/** Initializes UTIL block.
* @param[in] cfg	UTIL configuration
*/
void util_init(UTIL_CFG *cfg)
{
	writel(cfg->pe_sys_clk_ratio,   UTIL_PE_SYS_CLK_RATIO);
}

/** Enables UTIL-PE core.
*
*/
void util_enable(void)
{
	writel(CORE_ENABLE, UTIL_TX_CTRL);
}

/** Disables UTIL-PE core.
*
*/
void util_disable(void)
{
	writel(CORE_DISABLE, UTIL_TX_CTRL);
}

/**************************** HIF ***************************/

/** Initializes HIF no copy block.
*
*/
void hif_nocpy_init(void)
{
	writel(4,							HIF_NOCPY_TX_PORT_NO);
	writel(CBUS_VIRT_TO_PFE(BMU1_BASE_ADDR + BMU_ALLOC_CTRL),		HIF_NOCPY_LMEM_ALLOC_ADDR);
	writel(CBUS_VIRT_TO_PFE(CLASS_INQ_PKTPTR),	HIF_NOCPY_CLASS_ADDR);
	writel(CBUS_VIRT_TO_PFE(TMU_PHY_INQ_PKTPTR),	HIF_NOCPY_TMU_PORT0_ADDR);
	writel(HIF_RX_POLL_CTRL_CYCLE<<16|HIF_TX_POLL_CTRL_CYCLE, HIF_NOCPY_POLL_CTRL); 
}

/** Enable hif_nocpy tx DMA and interrupt
*
*/
void hif_nocpy_tx_enable(void)
{
	/*TODO not sure poll_cntrl_en is required or not */
	writel( HIF_CTRL_DMA_EN, HIF_NOCPY_TX_CTRL);
	//writel((readl(HIF_NOCPY_INT_ENABLE) | HIF_INT_EN | HIF_TXPKT_INT_EN), HIF_NOCPY_INT_ENABLE);
}

/** Disable hif_nocpy tx DMA and interrupt
*
*/
void hif_nocpy_tx_disable(void)
{
	u32     hif_int;

	writel(0, HIF_NOCPY_TX_CTRL);

	hif_int = readl(HIF_NOCPY_INT_ENABLE);
	hif_int &= HIF_TXPKT_INT_EN;
	writel(hif_int, HIF_NOCPY_INT_ENABLE);
}

/** Enable hif rx DMA and interrupt
*
*/
void hif_nocpy_rx_enable(void)
{
	hif_nocpy_rx_dma_start();
	writel((readl(HIF_NOCPY_INT_ENABLE) | HIF_INT_EN | HIF_RXPKT_INT_EN), HIF_NOCPY_INT_ENABLE);
}

/** Disable hif_nocpy rx DMA and interrupt
*
*/
void hif_nocpy_rx_disable(void)
{
	u32     hif_int;

	writel(0, HIF_NOCPY_RX_CTRL);

	hif_int = readl(HIF_NOCPY_INT_ENABLE);
	hif_int &= HIF_RXPKT_INT_EN;
	writel(hif_int, HIF_NOCPY_INT_ENABLE);

}
/** Initializes HIF copy block.
*
*/
void hif_init(void)
{
	/*Initialize HIF registers*/
	writel((HIF_RX_POLL_CTRL_CYCLE << 16) | HIF_TX_POLL_CTRL_CYCLE, HIF_POLL_CTRL); 
}

/** Enable hif tx DMA and interrupt
*
*/
void hif_tx_enable(void)
{
	/*TODO not sure poll_cntrl_en is required or not */
	writel(HIF_CTRL_DMA_EN, HIF_TX_CTRL);
	writel((readl(HIF_INT_ENABLE) | HIF_INT_EN | HIF_TXPKT_INT_EN), HIF_INT_ENABLE);
}

/** Disable hif tx DMA and interrupt
*
*/
void hif_tx_disable(void)
{
	u32	hif_int;

	writel(0, HIF_TX_CTRL);

	hif_int = readl(HIF_INT_ENABLE);
	hif_int &= HIF_TXPKT_INT_EN;
	writel(hif_int, HIF_INT_ENABLE);
}

/** Enable hif rx DMA and interrupt
*
*/
void hif_rx_enable(void)
{
	hif_rx_dma_start();
	writel((readl(HIF_INT_ENABLE) | HIF_INT_EN | HIF_RXPKT_INT_EN), HIF_INT_ENABLE);
}

/** Disable hif rx DMA and interrupt
*
*/
void hif_rx_disable(void)
{
	u32	hif_int;

	writel(0, HIF_RX_CTRL);

	hif_int = readl(HIF_INT_ENABLE);
	hif_int &= HIF_RXPKT_INT_EN;
	writel(hif_int, HIF_INT_ENABLE);

}
