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

#ifndef _PFE_MSPSYNC_H_
#define _PFE_MSPSYNC_H_

struct pfe_info {
	void *ddr_baseaddr;
	void *cbus_baseaddr;
	void *owner;
};

int pfe_msp_sync_init(struct pfe *pfe);
void pfe_msp_sync_exit(struct pfe *pfe);
#endif /* _PFE_MSPSYNC_H_ */
