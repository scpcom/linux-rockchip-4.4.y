/*
 *
 *  Copyright (C) 2007 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/** @file
 *  Contains all the functions to handle parsing and loading of PE firmware files.
 */
#include <linux/firmware.h>

#include "pfe_mod.h"
#include "pfe_firmware.h"
#include "pfe/pfe.h"

static Elf32_Shdr * get_elf_section_header(const struct firmware *fw, const char *section)
{
	Elf32_Ehdr *elf_hdr = (Elf32_Ehdr *)fw->data;
	Elf32_Shdr *shdr, *shdr_shstr;
	Elf32_Off e_shoff = be32_to_cpu(elf_hdr->e_shoff);
	Elf32_Half e_shentsize = be16_to_cpu(elf_hdr->e_shentsize);
	Elf32_Half e_shnum = be16_to_cpu(elf_hdr->e_shnum);
	Elf32_Half e_shstrndx = be16_to_cpu(elf_hdr->e_shstrndx);
	Elf32_Off shstr_offset;
	Elf32_Word sh_name;
	const char *name;
	int i;

	/* Section header strings */
	shdr_shstr = (Elf32_Shdr *)(fw->data + e_shoff + e_shstrndx * e_shentsize);
	shstr_offset = be32_to_cpu(shdr_shstr->sh_offset);

	for (i = 0; i < e_shnum; i++) {
		shdr = (Elf32_Shdr *)(fw->data + e_shoff + i * e_shentsize);

		sh_name = be32_to_cpu(shdr->sh_name);

		name = (const char *)(fw->data + shstr_offset + sh_name);

		if (!strcmp(name, section))
			return shdr;
	}

	printk(KERN_ERR "%s: didn't find section %s\n", __func__, section);

	return NULL;
}

static unsigned long get_elf_section(const struct firmware *fw, const char *section)
{
	Elf32_Shdr *shdr = get_elf_section_header(fw, section);

	if (shdr)
		return be32_to_cpu(shdr->sh_addr);
	else
		return -1;
}

#if defined(CFG_DIAGS)
static int pfe_get_diags_info(const struct firmware *fw, struct pfe_diags_info *diags_info)
{
	Elf32_Shdr *shdr;
	unsigned long offset, size;

	shdr = get_elf_section_header(fw, ".pfe_diags_str");
	if (shdr)
	{
		offset = be32_to_cpu(shdr->sh_offset);
		size = be32_to_cpu(shdr->sh_size);
		diags_info->diags_str_base = be32_to_cpu(shdr->sh_addr);
		diags_info->diags_str_size = size;
		diags_info->diags_str_array = pfe_kmalloc(size, GFP_KERNEL);
		memcpy(diags_info->diags_str_array, fw->data+offset, size);

		return 0;
	} else
	{
		return -1;
	}
}
#endif

static void pfe_check_version_info(const struct firmware *fw)
{
	static char *version = NULL;

	Elf32_Shdr *shdr = get_elf_section_header(fw, ".version");

	if (shdr)
	{
		if(!version)
		{
			/* this is the first fw we load, use its version string as reference (whatever it is) */
			version = (char *)(fw->data + be32_to_cpu(shdr->sh_offset));

			printk(KERN_INFO "PFE binary version: %s\n", version);
		}
		else
		{
			/* already have loaded at least one firmware, check sequence can start now */
			if(strcmp(version, (char *)(fw->data + be32_to_cpu(shdr->sh_offset))))
			{
				printk(KERN_INFO "WARNING: PFE firmware binaries from incompatible version\n");
			}
		}
	}
	else
	{
		/* version cannot be verified, a potential issue that should be reported */
		printk(KERN_INFO "WARNING: PFE firmware binaries from incompatible version\n");
	}
}

/** PFE elf firmware loader.
* Loads an elf firmware image into a list of PE's (specified using a bitmask)
*
* @param pe_mask	Mask of PE id's to load firmware to
* @param fw		Pointer to the firmware image
*
* @return		0 on sucess, a negative value on error
*
*/
static int pfe_load_elf(int pe_mask, const struct firmware *fw)
{
	Elf32_Ehdr *elf_hdr = (Elf32_Ehdr *)fw->data;
	Elf32_Half sections = be16_to_cpu(elf_hdr->e_shnum);
	Elf32_Shdr *shdr = (Elf32_Shdr *) (fw->data + be32_to_cpu(elf_hdr->e_shoff));
	int id, section;
	int rc;

	printk(KERN_INFO "%s\n", __func__);

	/* Some sanity checks */
	if (strncmp(&elf_hdr->e_ident[EI_MAG0], ELFMAG, SELFMAG))
	{
		printk(KERN_ERR "%s: incorrect elf magic number\n", __func__);
		return -EINVAL;
	}

	if (elf_hdr->e_ident[EI_CLASS] != ELFCLASS32)
	{
		printk(KERN_ERR "%s: incorrect elf class(%x)\n", __func__, elf_hdr->e_ident[EI_CLASS]);
		return -EINVAL;
	}

	if (elf_hdr->e_ident[EI_DATA] != ELFDATA2MSB)
	{
		printk(KERN_ERR "%s: incorrect elf data(%x)\n", __func__, elf_hdr->e_ident[EI_DATA]);
		return -EINVAL;
	}

	if (be16_to_cpu(elf_hdr->e_type) != ET_EXEC)
	{
		printk(KERN_ERR "%s: incorrect elf file type(%x)\n", __func__, be16_to_cpu(elf_hdr->e_type));
		return -EINVAL;
	}	

	for (section = 0; section < sections; section++, shdr++)
	{
		if (!(be32_to_cpu(shdr->sh_flags) & (SHF_WRITE | SHF_ALLOC | SHF_EXECINSTR)))
                        continue;
		
		for (id = 0; id < MAX_PE; id++)
			if (pe_mask & (1 << id))
			{
				rc = pe_load_elf_section(id, fw->data, shdr);
				if (rc < 0)
					goto err;
			}
	}

	pfe_check_version_info(fw);

	return 0;

err:
	return rc;
}


/** PFE firmware initialization.
* Loads different firmware files from filesystem.
* Initializes PE IMEM/DMEM and UTIL-PE DDR
* Initializes control path symbol addresses (by looking them up in the elf firmware files
* Takes PE's out of reset
*
* @return	0 on sucess, a negative value on error
*
*/
int pfe_firmware_init(struct pfe *pfe)
{
	const struct firmware *class_fw, *tmu_fw, *util_fw;
	int rc = 0;
#if !defined(CONFIG_UTIL_DISABLED)
	const char* util_fw_name;
#endif

	printk(KERN_INFO "%s\n", __func__);

	if (request_firmware(&class_fw, CLASS_FIRMWARE_FILENAME, pfe->dev)) {
		printk(KERN_ERR "%s: request firmware %s failed\n", __func__, CLASS_FIRMWARE_FILENAME);
		rc = -ETIMEDOUT;
		goto err0;
	}

	if (request_firmware(&tmu_fw, TMU_FIRMWARE_FILENAME, pfe->dev)) {
		printk(KERN_ERR "%s: request firmware %s failed\n", __func__,  TMU_FIRMWARE_FILENAME);
		rc = -ETIMEDOUT;
		goto err1;
        }
#if !defined(CONFIG_UTIL_DISABLED)
	util_fw_name = (system_rev == 0) ? UTIL_REVA0_FIRMWARE_FILENAME : UTIL_FIRMWARE_FILENAME;

	if (request_firmware(&util_fw, util_fw_name, pfe->dev)) {
		printk(KERN_ERR "%s: request firmware %s failed\n", __func__,  util_fw_name);
		rc = -ETIMEDOUT;
		goto err2;
	}
#endif
	rc = pfe_load_elf(CLASS_MASK, class_fw);
	if (rc < 0) {
		printk(KERN_ERR "%s: class firmware load failed\n", __func__);
		goto err3;
	}

	pfe->ctrl.class_dmem_sh = get_elf_section(class_fw, ".dmem_sh");
	pfe->ctrl.class_pe_lmem_sh = get_elf_section(class_fw, ".pe_lmem_sh");

#if defined(CFG_DIAGS)
	rc = pfe_get_diags_info(class_fw, &pfe->diags.class_diags_info);
	if (rc < 0) {
		printk (KERN_WARNING "PFE diags won't be available for class PEs\n");
		rc = 0;
	}
#endif

	printk(KERN_INFO "%s: class firmware loaded %#lx %#lx\n", __func__, pfe->ctrl.class_dmem_sh, pfe->ctrl.class_pe_lmem_sh);

	rc = pfe_load_elf(TMU_MASK, tmu_fw);
	if (rc < 0) {
		printk(KERN_ERR "%s: tmu firmware load failed\n", __func__);
		goto err3;
	}

	pfe->ctrl.tmu_dmem_sh = get_elf_section(tmu_fw, ".dmem_sh");

	printk(KERN_INFO "%s: tmu firmware loaded %#lx\n", __func__, pfe->ctrl.tmu_dmem_sh);

#if !defined(CONFIG_UTIL_DISABLED)
	rc = pfe_load_elf(UTIL_MASK, util_fw);
	if (rc < 0) {
		printk(KERN_ERR "%s: util firmware load failed\n", __func__);
		goto err3;
	}

	pfe->ctrl.util_dmem_sh = get_elf_section(util_fw, ".dmem_sh");
	pfe->ctrl.util_ddr_sh = get_elf_section(util_fw, ".ddr_sh");

#if defined(CFG_DIAGS)
	rc = pfe_get_diags_info(util_fw, &pfe->diags.util_diags_info);
	if (rc < 0) {
		printk(KERN_WARNING "PFE diags won't be available for util PE\n");
		rc = 0;
	}
#endif

	printk(KERN_INFO "%s: util firmware loaded %#lx\n", __func__, pfe->ctrl.util_dmem_sh);

	util_enable();
#endif

	tmu_enable(0xf);
	class_enable();

err3:
#if !defined(CONFIG_UTIL_DISABLED)
	release_firmware(util_fw);

err2:
#endif
	release_firmware(tmu_fw);

err1:
	release_firmware(class_fw);

err0:
	return rc;
}

/** PFE firmware cleanup
* Puts PE's in reset
*
*
*/
void pfe_firmware_exit(struct pfe *pfe)
{
	printk(KERN_INFO "%s\n", __func__);

	class_disable();
	tmu_disable(0xf);
#if !defined(CONFIG_UTIL_DISABLED)
	util_disable();
#endif
}
