/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>

#include <mach/hardware.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/mach-types.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>

#include <linux/fb.h>

#include "mdp.h"
#include "msm_fb.h"
#include "hdmi.h"

static uint32 mdp_last_dma_s_update_width;
static uint32 mdp_last_dma_s_update_height;
static uint32 mdp_curr_dma_s_update_width;
static uint32 mdp_curr_dma_s_update_height;

ktime_t mdp_dma_s_last_update_time = { 0 };

extern int mdp_lcd_rd_cnt_offset_slow;
extern int mdp_lcd_rd_cnt_offset_fast;
extern int mdp_vsync_usec_wait_line_too_short;
uint32 mdp_dma_s_update_time_in_usec;
uint32 mdp_total_vdopkts = 0;

extern u32 msm_fb_debug_enabled;
extern struct workqueue_struct *mdp_dma_wq;

ktime_t dma_s_wait_time;

static void mdp_dma_s_update_lcd(struct msm_fb_data_type *mfd)
{
	MDPIBUF *iBuf = &mfd->ibuf;
	int mddi_dest = FALSE;
	uint32 outBpp = iBuf->bpp;
	uint32 dma_s_cfg_reg;
	uint8 *src;
	struct msm_fb_panel_data *pdata =
	    (struct msm_fb_panel_data *)mfd->pdev->dev.platform_data;
	uint32 mddi_pkt_desc;

	dma_s_cfg_reg = DMA_PACK_ALIGN_LSB |
	    DMA_OUT_SEL_AHB | DMA_IBUF_NONCONTIGUOUS;

#ifdef CONFIG_FB_MSM_MDP22
	dma_s_cfg_reg |= DMA_PACK_TIGHT;
#endif

	if (mfd->fb_imgType == MDP_BGR_565)
		dma_s_cfg_reg |= DMA_PACK_PATTERN_BGR;
	else if (mfd->fb_imgType == MDP_RGBA_8888)
		dma_s_cfg_reg |= DMA_PACK_PATTERN_BGR;
	else
		dma_s_cfg_reg |= DMA_PACK_PATTERN_RGB;

	if (outBpp == 4){
		dma_s_cfg_reg |= DMA_IBUF_C3ALPHA_EN;
		dma_s_cfg_reg |= DMA_IBUF_FORMAT_xRGB8888_OR_ARGB8888;
}

	if (outBpp == 2)
		dma_s_cfg_reg |= DMA_IBUF_FORMAT_RGB565;

	if (mfd->panel_info.pdest != DISPLAY_2) {
		printk(KERN_ERR "error: non-secondary type through dma_s!\n");
		return;
	}

	if ((mfd->panel_info.type == MDDI_PANEL) ||
		(mfd->panel_info.type == EXT_MDDI_PANEL)) {
		dma_s_cfg_reg |= DMA_OUT_SEL_MDDI;
		mddi_dest = TRUE;

		if (mfd->panel_info.type == MDDI_PANEL) {
			mdp_total_vdopkts++;}
	} else {
		dma_s_cfg_reg |= DMA_AHBM_LCD_SEL_SECONDARY;
		outp32(MDP_EBI2_LCD1, mfd->data_port_phys);
	}

	dma_s_cfg_reg |= DMA_DITHER_EN;

	src = (uint8 *) iBuf->buf;
	
	src += (iBuf->dma_x + iBuf->dma_y * iBuf->ibuf_width) * outBpp;

	mdp_curr_dma_s_update_width = iBuf->dma_w;
	mdp_curr_dma_s_update_height = iBuf->dma_h;

	
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
	
	if (mfd->panel_info.type == MDDI_PANEL) {
		MDP_OUTP(MDP_BASE + 0xa0004,
			(iBuf->dma_h << 16 | iBuf->dma_w));
		MDP_OUTP(MDP_BASE + 0xa0008, src);	
		MDP_OUTP(MDP_BASE + 0xa000c,
			iBuf->ibuf_width * outBpp);
	} else {
		MDP_OUTP(MDP_BASE + 0xb0004,
			(iBuf->dma_h << 16 | iBuf->dma_w));
		MDP_OUTP(MDP_BASE + 0xb0008, src);	
		MDP_OUTP(MDP_BASE + 0xb000c,
			iBuf->ibuf_width * outBpp);
	}

	if (mfd->panel_info.bpp == 18) {
		mddi_pkt_desc = MDDI_VDO_PACKET_DESC;
		dma_s_cfg_reg |= DMA_DSTC0G_6BITS |	
		    DMA_DSTC1B_6BITS | DMA_DSTC2R_6BITS;
	} else if (mfd->panel_info.bpp == 24) {
		mddi_pkt_desc = MDDI_VDO_PACKET_DESC_24;
		dma_s_cfg_reg |= DMA_DSTC0G_8BITS |      
			DMA_DSTC1B_8BITS | DMA_DSTC2R_8BITS;
	} else {
		mddi_pkt_desc = MDDI_VDO_PACKET_DESC_16;
		dma_s_cfg_reg |= DMA_DSTC0G_6BITS |	
		    DMA_DSTC1B_5BITS | DMA_DSTC2R_5BITS;
	}

	if (mddi_dest) {
		if (mfd->panel_info.type == MDDI_PANEL) {
			MDP_OUTP(MDP_BASE + 0xa0010,
				(iBuf->dma_y << 16) | iBuf->dma_x);
			MDP_OUTP(MDP_BASE + 0x00090, 1);
		} else {
			MDP_OUTP(MDP_BASE + 0xb0010,
				(iBuf->dma_y << 16) | iBuf->dma_x);
			MDP_OUTP(MDP_BASE + 0x00090, 2);
		}
		MDP_OUTP(MDP_BASE + 0x00094,
				(mddi_pkt_desc << 16) |
				mfd->panel_info.mddi.vdopkt);
	} else {
		
		pdata->set_rect(iBuf->dma_x, iBuf->dma_y, iBuf->dma_w,
				iBuf->dma_h);
	}

	if (mfd->panel_info.type == MDDI_PANEL)
		MDP_OUTP(MDP_BASE + 0xa0000, dma_s_cfg_reg);
	else
		MDP_OUTP(MDP_BASE + 0xb0000, dma_s_cfg_reg);


	
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
	if (mfd->panel_info.type == MDDI_PANEL)
	{

	}
	else
		mdp_pipe_kickoff(MDP_DMA_E_TERM, mfd);

}

enum hrtimer_restart mdp_dma_s_vsync_hrtimer_handler(struct hrtimer *ht)
{
        struct msm_fb_data_type *mfd = NULL;

        mfd = container_of(ht, struct msm_fb_data_type, dma_hrtimer);

	mdp_pipe_kickoff(MDP_DMA_S_TERM, mfd);

        return HRTIMER_NORESTART;
}

static void mdp_dma_s_schedule(struct msm_fb_data_type *mfd, uint32 term)
{
	
	
	int32 mdp_lcd_rd_cnt;
	uint32 usec_wait_time;
	uint32 start_y;

	
	
	
	
	
	if (term == MDP_DMA_S_TERM)
		mdp_pipe_ctrl(MDP_DMA_S_BLOCK, MDP_BLOCK_POWER_ON, FALSE);

	mdp_dma_s_update_time_in_usec =
	    MDP_KTIME2USEC(mdp_dma_s_last_update_time);

	if ((!mfd->ibuf.vsync_enable) || (!mfd->panel_info.lcd.vsync_enable)
	    || (mfd->use_mdp_vsync)) {
		mdp_pipe_kickoff(term, mfd);
		return;
	}
	
	
	

	
	
	mdp_lcd_rd_cnt = 819; 
	
	if (mdp_dma_s_update_time_in_usec != 0) {
		uint32 num, den;

		
		

		num = mdp_last_dma_s_update_width * mdp_last_dma_s_update_height;
		den =
		    (((mfd->panel_info.lcd.refx100 * mfd->total_lcd_lines) /
		      1000) * (mdp_dma_s_update_time_in_usec / 100)) / 1000;

		if (den == 0)
			mfd->vsync_width_boundary[mdp_last_dma_s_update_width] =
			    mfd->panel_info.xres + 1;
		else
			mfd->vsync_width_boundary[mdp_last_dma_s_update_width] =
			    (int)(num / den);
	}

	if (mfd->vsync_width_boundary[mdp_last_dma_s_update_width] >
	    mdp_curr_dma_s_update_width) {
		
		mdp_lcd_rd_cnt += mdp_lcd_rd_cnt_offset_fast;
	} else {
		
		mdp_lcd_rd_cnt -= mdp_lcd_rd_cnt_offset_slow;
	}

	if (mdp_lcd_rd_cnt < 0)
		mdp_lcd_rd_cnt = mfd->total_lcd_lines + mdp_lcd_rd_cnt;
	else if (mdp_lcd_rd_cnt > mfd->total_lcd_lines)
		mdp_lcd_rd_cnt = mdp_lcd_rd_cnt - mfd->total_lcd_lines - 1;

	
	start_y = mfd->ibuf.dma_y;

	
	if (start_y > mdp_lcd_rd_cnt) {
		
		

		if ((start_y - mdp_lcd_rd_cnt) <=
		    mdp_vsync_usec_wait_line_too_short)
			usec_wait_time = 0;
		else
			usec_wait_time =
			    ((start_y -
			      mdp_lcd_rd_cnt) * 1000000) /
			    ((mfd->total_lcd_lines *
			      mfd->panel_info.lcd.refx100) / 100);
	} else {
		if ((start_y + (mfd->total_lcd_lines - mdp_lcd_rd_cnt)) <=
		    mdp_vsync_usec_wait_line_too_short)
			usec_wait_time = 0;
		else
			usec_wait_time =
			    ((start_y +
			      (mfd->total_lcd_lines -
			       mdp_lcd_rd_cnt)) * 1000000) /
			    ((mfd->total_lcd_lines *
			      mfd->panel_info.lcd.refx100) / 100);
	}

	mdp_last_dma_s_update_width = mdp_curr_dma_s_update_width;
	mdp_last_dma_s_update_height = mdp_curr_dma_s_update_height;

	dma_s_wait_time.tv.sec = 0;
	dma_s_wait_time.tv.nsec = usec_wait_time * 1000;

	enable_irq(mfd->channel_irq);
}

void mdp_dma_s_update(struct msm_fb_data_type *mfd)
{
	down(&mfd->dma->mutex);
	if ((mfd) && (!mfd->dma->busy) && (mfd->panel_power_on)) {
		down(&mfd->sem);
		mfd->ibuf_flushed = TRUE;
		mdp_dma_s_update_lcd(mfd);

		if (mfd->panel_info.type == MDDI_PANEL)
			mdp_enable_irq(MDP_DMA_S_TERM);
		else
			mdp_enable_irq(MDP_DMA_E_TERM);
		mfd->dma->busy = TRUE;
		INIT_COMPLETION(mfd->dma->comp);
		mdp_dma_s_schedule(mfd, MDP_DMA_S_TERM);
		up(&mfd->sem);

		
		wait_for_completion_killable(&mfd->dma->comp);
		if (mfd->panel_info.type == MDDI_PANEL)
			mdp_disable_irq(MDP_DMA_S_TERM);
		else
			mdp_disable_irq(MDP_DMA_E_TERM);

	
		if (mfd->pan_waiting) {
			mfd->pan_waiting = FALSE;
			complete(&mfd->pan_comp);
		}
	}
	up(&mfd->dma->mutex);
}
