/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#include <linux/string.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/fb.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/wait.h>
#include <linux/types.h>

#include "disp_drv_platform.h"
#include "m4u_priv.h"
#include "mtkfb.h"
#include "debug.h"
#include "lcm_drv.h"
#include "ddp_path.h"
#include "fbconfig_kdebug.h"
#include "primary_display.h"
#include "ddp_ovl.h"
#include "ddp_dsi.h"
#include "ddp_irq.h"

/* #include "disp_drv.h" */
/* #include "lcd_drv.h" */

/* **************************************************************************** */
/* This part is for customization parameters of D-IC and DSI . */
/* **************************************************************************** */
bool fbconfig_start_LCM_config;
#define FBCONFIG_MDELAY(n)	(PM_lcm_utils_dsi0.mdelay((n)))
#define SET_RESET_PIN(v)	(PM_lcm_utils_dsi0.set_reset_pin((v)))
#define dsi_set_cmdq(pdata, queue_size, force_update) PM_lcm_utils_dsi0.dsi_set_cmdq(pdata, queue_size, force_update)
#define FBCONFIG_KEEP_NEW_SETTING 1
#define FBCONFIG_DEBUG 0

#define FBCONFIG_IOW(num, dtype)     _IOW('X', num, dtype)
#define FBCONFIG_IOR(num, dtype)     _IOR('X', num, dtype)
#define FBCONFIG_IOWR(num, dtype)    _IOWR('X', num, dtype)
#define FBCONFIG_IO(num)             _IO('X', num)

#define GET_DSI_ID	   FBCONFIG_IOW(43, unsigned int)
#define SET_DSI_ID	   FBCONFIG_IOW(44, unsigned int)
#define LCM_GET_ID     FBCONFIG_IOR(45, unsigned int)
#define LCM_GET_ESD    FBCONFIG_IOWR(46, unsigned int)
#define DRIVER_IC_CONFIG    FBCONFIG_IOR(47, unsigned int)
#define DRIVER_IC_CONFIG_DONE  FBCONFIG_IO(0)
#define DRIVER_IC_RESET    FBCONFIG_IOR(48, unsigned int)


#define MIPI_SET_CLK     FBCONFIG_IOW(51, unsigned int)
#define MIPI_SET_LANE    FBCONFIG_IOW(52, unsigned int)
#define MIPI_SET_TIMING  FBCONFIG_IOW(53, unsigned int)
#define MIPI_SET_VM      FBCONFIG_IOW(54, unsigned int)	/* mipi video mode timing setting */
#define MIPI_SET_CC	 FBCONFIG_IOW(55, unsigned int)	/* mipi non-continuous clock */
#define MIPI_SET_SSC	 FBCONFIG_IOW(56, unsigned int)	/* spread frequency */
#define MIPI_SET_CLK_V2  FBCONFIG_IOW(57, unsigned int)	/* For div1,div2,fbk_div case */


#define TE_SET_ENABLE  FBCONFIG_IOW(61, unsigned int)
#define FB_LAYER_DUMP  FBCONFIG_IOW(62, unsigned int)
#define FB_LAYER_GET_INFO FBCONFIG_IOW(63, unsigned int)
#define FB_LAYER_GET_EN FBCONFIG_IOW(64, unsigned int)
#define LCM_GET_ESD_RET    FBCONFIG_IOR(65, unsigned int)

#define LCM_GET_DSI_CONTINU    FBCONFIG_IOR(71, unsigned int)
#define LCM_GET_DSI_CLK   FBCONFIG_IOR(72, unsigned int)
#define LCM_GET_DSI_TIMING   FBCONFIG_IOR(73, unsigned int)
#define LCM_GET_DSI_LANE_NUM    FBCONFIG_IOR(74, unsigned int)
#define LCM_GET_DSI_TE    FBCONFIG_IOR(75, unsigned int)
#define LCM_GET_DSI_SSC    FBCONFIG_IOR(76, unsigned int)
#define LCM_GET_DSI_CLK_V2    FBCONFIG_IOR(77, unsigned int)
#define LCM_TEST_DSI_CLK    FBCONFIG_IOR(78, unsigned int)
#define FB_GET_MISC FBCONFIG_IOR(80, unsigned int)

#define DP_COLOR_BITS_PER_PIXEL(color)    ((0x0003FF00 & color) >>  8)
static int global_layer_id = -1;

struct dentry *ConfigPara_dbgfs = NULL;
CONFIG_RECORD_LIST head_list;
LCM_REG_READ reg_read;

/* int esd_check_addr; */
/* int esd_check_para_num; */
/* int esd_check_type; */
/* char * esd_check_buffer =NULL; */
/* extern void fbconfig_disp_set_mipi_timing(MIPI_TIMING timing); */
/* extern unsigned int fbconfig_get_layer_info(FBCONFIG_LAYER_INFO *layers); */
/* extern unsigned int fbconfig_get_layer_vaddr(int layer_id,int * layer_size,int * enable); */
/* unsigned int fbconfig_get_layer_height(int layer_id,int * layer_size,int * enable,int* height ,int * fmt); */
typedef struct PM_TOOL_ST {
	DSI_INDEX dsi_id;
	LCM_REG_READ reg_read;
	LCM_PARAMS *pLcm_params;
	LCM_DRIVER *pLcm_drv;
} PM_TOOL_T;
static PM_TOOL_T pm_params = {
	.dsi_id = PM_DSI0,
	.pLcm_params = NULL,
	.pLcm_drv = NULL,
};

static void *pm_get_handle(void)
{
	return (void *)&pm_params;
}

static DISP_MODULE_ENUM pm_get_dsi_handle(DSI_INDEX dsi_id)
{
	if (dsi_id == PM_DSI0)
		return DISP_MODULE_DSI0;
	else if (dsi_id == PM_DSI1)
		return DISP_MODULE_DSI1;
	else if (dsi_id == PM_DSI_DUAL)
		return DISP_MODULE_DSIDUAL;
	else
		return DISP_MODULE_UNKNOWN;
}

int fbconfig_get_esd_check(DSI_INDEX dsi_id, uint32_t cmd, uint8_t *buffer, uint32_t num)
{
	int array[4];
	int ret;
	/* set max return packet size */
	/* array[0] = 0x00013700; */
	array[0] = 0x3700 + (num << 16);
	dsi_set_cmdq(array, 1, 1);
	atomic_set(&ESDCheck_byCPU , 1);
	ret = DSI_dcs_read_lcm_reg_v2(pm_get_dsi_handle(dsi_id), NULL, cmd, buffer, num);
	atomic_set(&ESDCheck_byCPU , 0);
	if (ret == 0)
		return -1;

	return 0;
}

/* RECORD_CMD = 0, */
/* RECORD_MS = 1, */
/* RECORD_PIN_SET        = 2, */

void Panel_Master_DDIC_config(void)
{

	struct list_head *p;
	CONFIG_RECORD_LIST *node;

	list_for_each_prev(p, &head_list.list) {
		node = list_entry(p, CONFIG_RECORD_LIST, list);
		switch (node->record.type) {
		case RECORD_CMD:
			dsi_set_cmdq(node->record.ins_array, node->record.ins_num, 1);
			break;
		case RECORD_MS:
			FBCONFIG_MDELAY(node->record.ins_array[0]);
			break;
		case RECORD_PIN_SET:
			SET_RESET_PIN(node->record.ins_array[0]);
			break;
		default:
			pr_debug("sxk=>No such Type!!!!!\n");
		}

	}

}

/*static void print_from_head_to_tail(void)
{
	int i;
	struct list_head *p;
	CONFIG_RECORD_LIST *print;
	pr_debug("DDIC=====>:print_from_head_to_tail  START\n");

	list_for_each_prev(p, &head_list.list) {
		print = list_entry(p, CONFIG_RECORD_LIST, list);
		pr_debug("type:%d num %d value:\r\n", print->record.type, print->record.ins_num);
		for (i = 0; i < print->record.ins_num; i++)
			pr_debug("0x%x\t", print->record.ins_array[i]);
		pr_debug("\r\n");
	}
	pr_debug("DDIC=====>:print_from_head_to_tail  END\n");

}*/

static void free_list_memory(void)
{
	struct list_head *p, *n;
	CONFIG_RECORD_LIST *print;

	list_for_each_safe(p, n, &head_list.list) {
		print = list_entry(p, CONFIG_RECORD_LIST, list);
		list_del(&print->list);
		kfree(print);
	}
	/* test here : head->next == head ?? */
	if (list_empty(&head_list.list))
		pr_debug("*****list is empty!!\n");
	else
		pr_debug("*****list is NOT empty!!\n");

}

static int fbconfig_open(struct inode *inode, struct file *file)
{
	PM_TOOL_T *pm_params;

	file->private_data = inode->i_private;
	pm_params = (PM_TOOL_T *) pm_get_handle();
	PanelMaster_set_PM_enable(1);
	pm_params->pLcm_drv = DISP_GetLcmDrv();
	pm_params->pLcm_params = DISP_GetLcmPara();

	return 0;
}


static char fbconfig_buffer[2048];

static ssize_t fbconfig_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	const int debug_bufmax = sizeof(fbconfig_buffer) - 1;	/* 2047 */
	int n = 0;

	n += scnprintf(fbconfig_buffer + n, debug_bufmax - n, "sxkhome");
	fbconfig_buffer[n++] = 0;
	/* n = 5 ; */
	/* memcpy(fbconfig_buffer,"sxkhome",6); */
	return simple_read_from_buffer(ubuf, count, ppos, fbconfig_buffer, n);
}

static ssize_t fbconfig_write(struct file *file,
			      const char __user *ubuf, size_t count, loff_t *ppos)
{
	return 0;
}


static long fbconfig_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	void __user *argp = (void __user *)arg;
	PM_TOOL_T *pm = (PM_TOOL_T *) pm_get_handle();
	uint32_t dsi_id = pm->dsi_id;
	LCM_DSI_PARAMS *pParams = get_dsi_params_handle(dsi_id);

#ifdef FBCONFIG_SHOULD_KICK_IDLEMGR
	primary_display_idlemgr_kick(__func__, 1);
#endif
	switch (cmd) {
	case GET_DSI_ID:
	{
		put_user(dsi_id, (unsigned long *)argp);
		return 0;
	}
	case SET_DSI_ID:
	{
		if (arg > PM_DSI_DUAL)
			return -EINVAL;
		pm->dsi_id = arg;
		pr_debug("fbconfig=>SET_DSI_ID:%d\n", dsi_id);

		return 0;
	}
	case LCM_TEST_DSI_CLK:
	{
		LCM_TYPE_FB lcm_fb;
		LCM_PARAMS *pLcm_params = pm->pLcm_params;

		lcm_fb.clock = pLcm_params->dsi.PLL_CLOCK;
		lcm_fb.lcm_type = pLcm_params->dsi.mode;

		pr_debug("fbconfig=>LCM_TEST_DSI_CLK:%d\n", ret);
		return copy_to_user(argp, &lcm_fb, sizeof(lcm_fb)) ? -EFAULT : 0;
	}
	case LCM_GET_ID:
	{
/* LCM_DRIVER*pLcm_drv=pm->pLcm_drv; */
		unsigned int lcm_id = 0;
#if 0
		if (pLcm_drv != NULL)
			lcm_id = pLcm_drv->get_lcm_id();
		else
			pr_debug("fbconfig=>LCM_GET_ID:%x\n", lcm_id);
#endif
		return copy_to_user(argp, &lcm_id, sizeof(lcm_id)) ? -EFAULT : 0;
	}
	case DRIVER_IC_CONFIG:
	{
		CONFIG_RECORD_LIST *record_tmp_list = kmalloc(sizeof(*record_tmp_list), GFP_KERNEL);

		if (copy_from_user(&record_tmp_list->record, (void __user *)arg, sizeof(CONFIG_RECORD))) {
			pr_debug("list_add: copy_from_user failed! line:%d\n", __LINE__);
			kfree(record_tmp_list);
			record_tmp_list = NULL;
			return -EFAULT;
		}
		list_add(&record_tmp_list->list, &head_list.list);
		return 0;
	}
	case DRIVER_IC_CONFIG_DONE:
	{
		/* print_from_head_to_tail(); */
		Panel_Master_dsi_config_entry("PM_DDIC_CONFIG", NULL);
		/*free the memory ..... */
		free_list_memory();
		return 0;
	}
	case MIPI_SET_CC:
	{
		uint32_t enable = 0;

		if (get_user(enable, (uint32_t __user *) argp)) {
			pr_debug("[MIPI_SET_CC]: copy_from_user failed! line:%d\n",
				 __LINE__);
			return -EFAULT;
		}

		PanelMaster_set_CC(dsi_id, enable);
		return 0;
	}
	case LCM_GET_DSI_CONTINU:
	{
		uint32_t ret = PanelMaster_get_CC(dsi_id);
		/* need to improve ,0 now means nothing but one parameter.... */
		pr_debug("LCM_GET_DSI_CONTINU=>DSI: %d\n", ret);
		return put_user(ret, (unsigned long *)argp);
	}
	case MIPI_SET_CLK:
	{
		uint32_t clk = 0;

		if (get_user(clk, (uint32_t __user *) argp)) {
			pr_debug("[MIPI_SET_CLK]: copy_from_user failed! line:%d\n",
				 __LINE__);
			return -EFAULT;
		}

		pr_debug("LCM_GET_DSI_CLK=>dsi:%d\n", clk);
		Panel_Master_dsi_config_entry("PM_CLK", &clk);
		return 0;
	}
	case LCM_GET_DSI_CLK:
	{
		uint32_t clk = pParams->PLL_CLOCK;

		pr_debug("LCM_GET_DSI_CLK=>dsi:%d\n", clk);
		return put_user(clk, (unsigned long *)argp);
	}
	case MIPI_SET_SSC:
	{
		DSI_RET dsi_ssc;

		if (copy_from_user(&dsi_ssc, (void __user *)argp, sizeof(dsi_ssc))) {
			pr_debug("[MIPI_SET_SSC]: copy_from_user failed! line:%d\n",
				 __LINE__);
			return -EFAULT;
		}

		pr_debug("Pmaster:set mipi ssc line:%d\n", __LINE__);
		Panel_Master_dsi_config_entry("PM_SSC", &dsi_ssc);
		return 0;
	}
	case LCM_GET_DSI_SSC:
	{
		uint32_t ssc = pParams->ssc_range;

		if (pParams->ssc_disable)
			ssc = 0;
		return put_user(ssc, (unsigned long *)argp);
	}
	case LCM_GET_DSI_LANE_NUM:
	{
		uint32_t lane_num = pParams->LANE_NUM;

		pr_debug("Panel Master=>LCM_GET_DSI_Lane_num=>dsi:%d\r\n", lane_num);
		return put_user(lane_num, (unsigned long *)argp);
	}
	case LCM_GET_DSI_TE:
	{
		int ret;

		ret = PanelMaster_get_TE_status(dsi_id);
		pr_debug("fbconfig=>LCM_GET_DSI_TE:%d\n", ret);
		return put_user(ret, (unsigned long *)argp);
	}
	case LCM_GET_DSI_TIMING:
	{
		uint32_t ret;
		MIPI_TIMING timing;

		if (copy_from_user(&timing, (void __user *)argp, sizeof(timing))) {
			pr_debug("[MIPI_GET_TIMING]: copy_from_user failed! line:%d\n",
				 __LINE__);
			return -EFAULT;
		}
		ret = PanelMaster_get_dsi_timing(dsi_id, timing.type);
		pr_debug("fbconfig=>LCM_GET_DSI_TIMING:%d\n", ret);
		timing.value = ret;
		return copy_to_user(argp, &timing, sizeof(timing)) ? -EFAULT : 0;
	}
	case MIPI_SET_TIMING:
	{
		MIPI_TIMING timing;

		if (primary_display_is_sleepd())
			return -EFAULT;
		if (copy_from_user(&timing, (void __user *)argp, sizeof(timing))) {
			pr_debug("[MIPI_SET_TIMING]: copy_from_user failed! line:%d\n",
				 __LINE__);
			return -EFAULT;
		}

		PanelMaster_DSI_set_timing(dsi_id, timing);
		return 0;
	}
	case FB_LAYER_GET_EN:
	{
		PM_LAYER_EN layers;
		OVL_BASIC_STRUCT ovl_all[TOTAL_OVL_LAYER_NUM];

#ifdef PRIMARY_THREE_OVL_CASCADE
		int i;

		ovl_get_info(DISP_MODULE_OVL0_2L, ovl_all);
		ovl_get_info(DISP_MODULE_OVL0, &ovl_all[2]);
		ovl_get_info(DISP_MODULE_OVL1_2L, &ovl_all[6]);
		for (i = 0; i < TOTAL_OVL_LAYER_NUM; ++i)
			layers.layer_en[i] = (ovl_all[i].layer_en ? 1 : 0);
#else
		ovl_get_info(DISP_MODULE_OVL0, ovl_all);
		layers.layer_en[0] = (ovl_all[0].layer_en ? 1 : 0);
		layers.layer_en[1] = (ovl_all[1].layer_en ? 1 : 0);
		layers.layer_en[2] = (ovl_all[2].layer_en ? 1 : 0);
		layers.layer_en[3] = (ovl_all[3].layer_en ? 1 : 0);
#ifdef OVL_CASCADE_SUPPORT
		layers.layer_en[4] = (ovl_all[4].layer_en ? 1 : 0);
		layers.layer_en[5] = (ovl_all[5].layer_en ? 1 : 0);
		layers.layer_en[6] = (ovl_all[6].layer_en ? 1 : 0);
		layers.layer_en[7] = (ovl_all[7].layer_en ? 1 : 0);
#endif
#endif
		pr_debug("[LAYER_GET_EN]:L0:%d L1:%d L2:%d L3:%d\n", ovl_all[0].layer_en,
			 ovl_all[1].layer_en, ovl_all[2].layer_en, ovl_all[3].layer_en);
		return copy_to_user(argp, &layers, sizeof(layers)) ? -EFAULT : 0;
	}
	case FB_LAYER_GET_INFO:
	{
		PM_LAYER_INFO layer_info;
		OVL_BASIC_STRUCT ovl_all[TOTAL_OVL_LAYER_NUM];

		if (copy_from_user(&layer_info, (void __user *)argp, sizeof(layer_info))) {
			pr_debug("[TE_SET_ENABLE]: copy_from_user failed! line:%d\n",
				 __LINE__);
			return -EFAULT;
		}
		global_layer_id = layer_info.index;
#ifdef PRIMARY_THREE_OVL_CASCADE
		ovl_get_info(DISP_MODULE_OVL0_2L, ovl_all);
		ovl_get_info(DISP_MODULE_OVL0, &ovl_all[2]);
		ovl_get_info(DISP_MODULE_OVL1_2L, &ovl_all[6]);
#else
		ovl_get_info(DISP_MODULE_OVL0, ovl_all);
#endif
		layer_info.height = ovl_all[layer_info.index].src_h;
		layer_info.width = ovl_all[layer_info.index].src_w;
		layer_info.fmt = DP_COLOR_BITS_PER_PIXEL(ovl_all[layer_info.index].fmt);
		layer_info.layer_size =
			ovl_all[layer_info.index].src_pitch * ovl_all[layer_info.index].src_h;
		pr_debug("===>: layer_size:0x%x height:%d\n", layer_info.layer_size,
			 layer_info.height);
		pr_debug("===>: width:%d src_pitch:%d\n", layer_info.width,
			 ovl_all[layer_info.index].src_pitch);
		pr_debug("===>: layer_id:%d fmt:%d\n", global_layer_id, layer_info.fmt);
		pr_debug("===>: layer_en:%d\n", (ovl_all[layer_info.index].layer_en));
		if ((layer_info.height == 0) || (layer_info.width == 0)
		    || (ovl_all[layer_info.index].layer_en == 0)) {
			pr_debug("===> is 000 Errorrrr!!\n");
			return -2;
		} else
			return copy_to_user(argp, &layer_info,
					    sizeof(layer_info)) ? -EFAULT : 0;

	}
	case FB_LAYER_DUMP:
	{
		int layer_size;
		int ret = 0;
		unsigned long kva = 0;
		unsigned int mva;
		unsigned int mapped_size = 0;
		unsigned int real_mva = 0;
		unsigned int real_size = 0;
		OVL_BASIC_STRUCT ovl_all[TOTAL_OVL_LAYER_NUM];

#ifdef PRIMARY_THREE_OVL_CASCADE
		ovl_get_info(DISP_MODULE_OVL0_2L, ovl_all);
		ovl_get_info(DISP_MODULE_OVL0, &ovl_all[2]);
		ovl_get_info(DISP_MODULE_OVL1_2L, &ovl_all[6]);
#else
		ovl_get_info(DISP_MODULE_OVL0, ovl_all);
#endif
		layer_size =
			ovl_all[global_layer_id].src_pitch * ovl_all[global_layer_id].src_h;
		mva = ovl_all[global_layer_id].addr;
		pr_debug("layer_size=%d, src_pitch=%d, h=%d, mva=0x%x,\n",
			 layer_size, ovl_all[global_layer_id].src_pitch,
			 ovl_all[global_layer_id].src_h, mva);

		if ((layer_size != 0) && (ovl_all[global_layer_id].layer_en != 0)) {
			/*pr_debug("sxk==>FB_LAYER_DUMP==>layer_size is %d   mva is 0x%x\n",layer_size,mva); */
			ret = m4u_query_mva_info(mva, layer_size, &real_mva, &real_size);
			if (ret < 0) {
				pr_debug
					("m4u_query_mva_info error:mva is 0x%x layer size is %d\n",
					 mva, layer_size);
				return ret;
			}
			ret = m4u_mva_map_kernel(real_mva, real_size, &kva, &mapped_size);
			if (ret < 0) {
				pr_debug("m4u_mva_map_kernel error: ret=%d 0x%x %d\r\n", ret,
					 real_mva, real_size);
				return ret;
			}
			if (layer_size > mapped_size) {
				pr_debug("==>layer size(0x%x)>mapped size(0x%x)!!!",
					 layer_size, mapped_size);
				return -EFAULT;
			}
			pr_debug("==> addr from user space is 0x%p\n", argp);
			pr_debug("==> kva is 0x%lx real_mva %x mva %x mmaped size is %dlayer_size is %d\n",
				 kva, real_mva, mva, mapped_size, layer_size);
			ret =
				copy_to_user(argp, (void *)kva + (mva - real_mva),
					     layer_size - (mva - real_mva)) ? -EFAULT : 0;
			m4u_mva_unmap_kernel(real_mva, real_size, kva);
			return ret;
		} else
			return -2;
	}
	case LCM_GET_ESD:
	{
		ESD_PARA esd_para;
		uint8_t *buffer;

		if (copy_from_user(&esd_para, (void __user *)arg, sizeof(esd_para))) {
			pr_debug("[LCM_GET_ESD]: copy_from_user failed! line:%d\n",
				 __LINE__);
			return -EFAULT;
		}
		buffer = kzalloc(esd_para.para_num + 6, GFP_KERNEL);
		if (!buffer)
			return -ENOMEM;

		ret =
			fbconfig_get_esd_check_test(dsi_id, esd_para.addr, buffer,
						    esd_para.para_num + 6);
		if (ret < 0) {
			kfree(buffer);
			return -EFAULT;
		}
		ret = copy_to_user(esd_para.esd_ret_buffer, buffer, esd_para.para_num);
		kfree(buffer);
		return ret;
	}
	case TE_SET_ENABLE:
	{
		uint32_t te_enable = 0;

		if (get_user(te_enable, (unsigned long *)argp))
			return -EFAULT;

		return 0;
	}
	case DRIVER_IC_RESET:
	{
		Panel_Master_dsi_config_entry("DRIVER_IC_RESET", NULL);
		return 0;
	}
	case FB_GET_MISC:
	{
		struct misc_property misc = { 0 };

		if (pm->pLcm_params->lcm_if == LCM_INTERFACE_DSI_DUAL)
			misc.dual_port = 1;
		misc.overall_layer_num = TOTAL_OVL_LAYER_NUM;
		ret = copy_to_user(argp, &misc,  sizeof(misc));
		return 0;
	}
	default:
		return ret;
	}
}

static int fbconfig_release(struct inode *inode, struct file *file)
{
	PanelMaster_set_PM_enable(0);

	return 0;
}

/* compat-ioctl */
#ifdef CONFIG_COMPAT

#define COMPAT_GET_DSI_ID	   FBCONFIG_IOW(43, compat_uint_t)
#define COMPAT_SET_DSI_ID	   FBCONFIG_IOW(44, compat_uint_t)
#define COMPAT_LCM_GET_ID     FBCONFIG_IOR(45, compat_uint_t)
#define COMPAT_LCM_GET_ESD    FBCONFIG_IOWR(46, compat_uint_t)
#define COMPAT_DRIVER_IC_CONFIG    FBCONFIG_IOR(47, compat_uint_t)
#define COMPAT_DRIVER_IC_CONFIG_DONE  FBCONFIG_IO(0)
#define COMPAT_DRIVER_IC_RESET    FBCONFIG_IOR(48, compat_uint_t)

#define COMPAT_MIPI_SET_CLK     FBCONFIG_IOW(51, compat_uint_t)
#define COMPAT_MIPI_SET_LANE    FBCONFIG_IOW(52, compat_uint_t)
#define COMPAT_MIPI_SET_TIMING  FBCONFIG_IOW(53, compat_uint_t)
#define COMPAT_MIPI_SET_VM      FBCONFIG_IOW(54, compat_uint_t)
#define COMPAT_MIPI_SET_CC	 FBCONFIG_IOW(55, compat_uint_t)
#define COMPAT_MIPI_SET_SSC	 FBCONFIG_IOW(56, compat_uint_t)
#define COMPAT_MIPI_SET_CLK_V2  FBCONFIG_IOW(57, compat_uint_t)

#define COMPAT_TE_SET_ENABLE  FBCONFIG_IOW(61, compat_uint_t)
#define COMPAT_FB_LAYER_DUMP  FBCONFIG_IOW(62, compat_uint_t)
#define COMPAT_FB_LAYER_GET_INFO FBCONFIG_IOW(63, compat_uint_t)
#define COMPAT_FB_LAYER_GET_EN FBCONFIG_IOW(64, compat_uint_t)
#define COMPAT_LCM_GET_ESD_RET    FBCONFIG_IOR(65, compat_uint_t)

#define COMPAT_LCM_GET_DSI_CONTINU    FBCONFIG_IOR(71, compat_uint_t)
#define COMPAT_LCM_GET_DSI_CLK   FBCONFIG_IOR(72, compat_uint_t)
#define COMPAT_LCM_GET_DSI_TIMING   FBCONFIG_IOR(73, compat_uint_t)
#define COMPAT_LCM_GET_DSI_LANE_NUM    FBCONFIG_IOR(74, compat_uint_t)
#define COMPAT_LCM_GET_DSI_TE    FBCONFIG_IOR(75, compat_uint_t)
#define COMPAT_LCM_GET_DSI_SSC    FBCONFIG_IOR(76, compat_uint_t)
#define COMPAT_LCM_GET_DSI_CLK_V2    FBCONFIG_IOR(77, compat_uint_t)
#define COMPAT_LCM_TEST_DSI_CLK    FBCONFIG_IOR(78, compat_uint_t)
#define COMPAT_FB_GET_MISC    FBCONFIG_IOR(80, compat_uint_t)

static int compat_get_lcm_type_fb(struct compat_lcm_type_fb __user *data32,
				  struct LCM_TYPE_FB __user *data)
{
	compat_int_t i;
	int err;

	err = get_user(i, &data32->clock);
	err |= put_user(i, &data->clock);
	err |= get_user(i, &data32->lcm_type);
	err |= put_user(i, &data->lcm_type);

	return err;
}

static int compat_put_lcm_type_fb(struct compat_lcm_type_fb __user *data32,
				  struct LCM_TYPE_FB __user *data)
{
	compat_int_t i;
	int err;

	err = get_user(i, &data->clock);
	err |= put_user(i, &data32->clock);
	err |= get_user(i, &data->lcm_type);
	err |= put_user(i, &data32->lcm_type);

	return err;
}

static int compat_get_config_record(struct compat_config_record *data32,
				    struct CONFIG_RECORD *data)
{
	compat_int_t i;
	int err;
	int n = 0;

	err = get_user(i, &data32->ins_num);
	err |= put_user(i, &data->ins_num);
	err |= get_user(i, &data32->type);
	err |= put_user(i, &data->type);

	for (n = 0; n < MAX_INSTRUCTION; ++n) {
		err |= get_user(i, &data32->ins_array[n]);
		err |= put_user(i, &data->ins_array[n]);
	}
	return err;
}

static int compat_put_config_record(struct compat_config_record *data32,
				    struct CONFIG_RECORD *data)
{
	compat_int_t i;
	int err;
	int n = 0;

	err = get_user(i, &data->ins_num);
	err |= put_user(i, &data32->ins_num);
	err |= get_user(i, &data->type);
	err |= put_user(i, &data32->type);

	for (n = 0; n < MAX_INSTRUCTION; ++n) {
		err |= get_user(i, &data->ins_array[n]);
		err |= put_user(i, &data32->ins_array[n]);
	}
	return err;
}

static int compat_get_dsi_ret(struct compat_dsi_ret *data32,
			      struct DSI_RET *data)
{
	compat_int_t i;
	int err = 0;
	int n = 0;

	for (n = 0; n < NUM_OF_DSI; ++n) {
		err |= get_user(i, &data32->dsi[n]);
		err |= put_user(i, &data->dsi[n]);
	}
	return err;
}

static int compat_put_dsi_ret(struct compat_dsi_ret *data32,
			      struct DSI_RET *data)
{
	compat_int_t i;
	int err = 0;
	int n = 0;

	for (n = 0; n < NUM_OF_DSI; ++n) {
		err |= get_user(i, &data->dsi[n]);
		err |= put_user(i, &data32->dsi[n]);
	}
	return err;
}

static int compat_get_mipi_timing(struct compat_mipi_timing *data32,
				    struct MIPI_TIMING *data)
{
	compat_int_t i;
	compat_uint_t d;
	int err;

	err = get_user(i, &data32->type);
	err |= put_user(i, &data->type);
	err |= get_user(d, &data32->value);
	err |= put_user(d, &data->value);
	return err;
}

static int compat_put_mipi_timing(struct compat_mipi_timing *data32,
				    struct MIPI_TIMING *data)
{
	compat_int_t i;
	compat_uint_t d;
	int err;

	err = get_user(i, &data->type);
	err |= put_user(i, &data32->type);
	err |= get_user(d, &data->value);
	err |= put_user(d, &data32->value);
	return err;
}

static int compat_get_pm_layer_en(struct compat_pm_layer_en *data32,
				  struct PM_LAYER_EN *data)
{
	compat_int_t i;
	int err = 0;
	int n = 0;

	for (n = 0; n < TOTAL_OVL_LAYER_NUM; ++n) {
		err |= get_user(i, &data32->layer_en[n]);
		err |= put_user(i, &data->layer_en[n]);
	}
	return err;
}

static int compat_put_pm_layer_en(struct compat_pm_layer_en *data32,
				  struct PM_LAYER_EN *data)
{
	compat_int_t i;
	int err = 0;
	int n = 0;

	for (n = 0; n < TOTAL_OVL_LAYER_NUM; ++n) {
		err |= get_user(i, &data->layer_en[n]);
		err |= put_user(i, &data32->layer_en[n]);
	}
	return err;
}

static int compat_get_pm_layer_info(struct compat_pm_layer_info *data32,
				    struct PM_LAYER_INFO *data)
{
	compat_int_t i;
	compat_uint_t d;
	int err;

	err = get_user(i, &data32->index);
	err |= put_user(i, &data->index);
	err |= get_user(i, &data32->height);
	err |= put_user(i, &data->height);
	err |= get_user(i, &data32->width);
	err |= put_user(i, &data->width);
	err |= get_user(i, &data32->fmt);
	err |= put_user(i, &data->fmt);
	err |= get_user(d, &data32->layer_size);
	err |= put_user(d, &data->layer_size);

	return err;
}

static int compat_put_pm_layer_info(struct compat_pm_layer_info *data32,
				    struct PM_LAYER_INFO *data)
{
	compat_int_t i;
	compat_uint_t d;
	int err;

	err = get_user(i, &data->index);
	err |= put_user(i, &data32->index);
	err |= get_user(i, &data->height);
	err |= put_user(i, &data32->height);
	err |= get_user(i, &data->width);
	err |= put_user(i, &data32->width);
	err |= get_user(i, &data->fmt);
	err |= put_user(i, &data32->fmt);
	err |= get_user(d, &data->layer_size);
	err |= put_user(d, &data32->layer_size);

	return err;
}

static int compat_get_esd_para(struct compat_esd_para *data32,
			       struct ESD_PARA *data)
{
	compat_int_t i;
	compat_uint_t d;
	int err;

	err = get_user(i, &data32->addr);
	err |= put_user(i, &data->addr);
	err |= get_user(i, &data32->type);
	err |= put_user(i, &data->type);
	err |= get_user(i, &data32->para_num);
	err |= put_user(i, &data->para_num);
	err |= get_user(d, &data32->esd_ret_buffer);
	err |= put_user((unsigned char *)(unsigned long)d, &data->esd_ret_buffer);

	return err;
}

static int compat_put_esd_para(struct compat_esd_para *data32,
			       struct ESD_PARA *data)
{
	compat_int_t i;
	compat_uint_t d;
	int err;

	err = get_user(i, &data->addr);
	err |= put_user(i, &data32->addr);
	err |= get_user(i, &data->type);
	err |= put_user(i, &data32->type);
	err |= get_user(i, &data->para_num);
	err |= put_user(i, &data32->para_num);
	err |= get_user(d, (compat_uint_t *)&data->esd_ret_buffer);
	err |= put_user(d, &data32->esd_ret_buffer);

	return err;
}

static long compat_fbconfig_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;

	if (!file->f_op || !file->f_op->unlocked_ioctl)
		return -ENOTTY;

	switch (cmd) {
	case COMPAT_GET_DSI_ID:
	{
		compat_uint_t __user *data32;
		unsigned int __user *data;
		int err;
		compat_uint_t d;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = get_user(d, data32);
		err |= put_user(d, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, GET_DSI_ID, (unsigned long)data);
		err |= get_user(d, data);
		err |= put_user(d, data32);
		return ret ? ret : err;
	}
	case COMPAT_SET_DSI_ID:
	{
		compat_uint_t __user *data32;
		unsigned int __user *data;
		int err;
		compat_uint_t d;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = get_user(d, data32);
		err |= put_user(d, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, SET_DSI_ID, (unsigned long)data);

		err |= get_user(d, data);
		err |= put_user(d, data32);
		return ret ? ret : err;
	}
	case COMPAT_LCM_TEST_DSI_CLK:
	{
		struct compat_lcm_type_fb __user *data32;
		struct LCM_TYPE_FB __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = compat_get_lcm_type_fb(data32, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, LCM_TEST_DSI_CLK, (unsigned long)data);
		err = compat_put_lcm_type_fb(data32, data);
		return ret ? ret : err;
	}
	case COMPAT_LCM_GET_ID:
	{
		compat_uint_t __user *data32;
		unsigned int __user *data;
		int err;
		compat_uint_t d;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = get_user(d, data32);
		err |= put_user(d, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, LCM_GET_ID, (unsigned long)data);
		err |= get_user(d, data);
		err |= put_user(d, data32);
		return err ? err : 0;
	}
	case COMPAT_DRIVER_IC_CONFIG:
	{
		struct compat_config_record __user *data32;
		struct CONFIG_RECORD __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = compat_get_config_record(data32, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, DRIVER_IC_CONFIG, (unsigned long)data);
		err = compat_put_config_record(data32, data);
		return ret ? ret : err;
	}
	case COMPAT_DRIVER_IC_CONFIG_DONE:
	{
		compat_uint_t __user *data32;
		unsigned int __user *data;
		int err;
		compat_uint_t d;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = get_user(d, data32);
		err |= put_user(d, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, DRIVER_IC_CONFIG_DONE, (unsigned long)data);
		err |= get_user(d, data);
		err |= put_user(d, data32);
		return ret ? ret : err;
	}
	case COMPAT_MIPI_SET_CC:
	{
		compat_uint_t __user *data32;
		unsigned int __user *data;
		int err;
		compat_uint_t d;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = get_user(d, data32);
		err |= put_user(d, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, MIPI_SET_CC, (unsigned long)data);
		err |= get_user(d, data);
		err |= put_user(d, data32);
		return ret ? ret : err;
	}

	case COMPAT_LCM_GET_DSI_CONTINU:
	{
		compat_uint_t __user *data32;
		unsigned int __user *data;
		int err;
		compat_uint_t d;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = get_user(d, data32);
		err |= put_user(d, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, LCM_GET_DSI_CONTINU, (unsigned long)data);
		err |= get_user(d, data);
		err |= put_user(d, data32);
		return ret ? ret : err;
	}
	case COMPAT_MIPI_SET_CLK:
	{
		compat_uint_t __user *data32;
		unsigned int __user *data;
		int err;
		compat_uint_t d;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = get_user(d, data32);
		err |= put_user(d, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, MIPI_SET_CLK, (unsigned long)data);
		err |= get_user(d, data);
		err |= put_user(d, data32);
		return ret ? ret : err;
	}
	case COMPAT_LCM_GET_DSI_CLK:
	{
		compat_uint_t __user *data32;
		unsigned int __user *data;
		int err;
		compat_uint_t d;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = get_user(d, data32);
		err |= put_user(d, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, LCM_GET_DSI_CLK, (unsigned long)data);
		err |= get_user(d, data);
		err |= put_user(d, data32);
		return ret ? ret : err;
	}
	case COMPAT_MIPI_SET_SSC:
	{
		struct compat_dsi_ret *data32;
		struct DSI_RET __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = compat_get_dsi_ret(data32, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, MIPI_SET_SSC, (unsigned long)data);
		err = compat_put_dsi_ret(data32, data);
		return ret ? ret : err;
	}

	case COMPAT_LCM_GET_DSI_SSC:
	{
		compat_uint_t __user *data32;
		unsigned int __user *data;
		int err;
		compat_uint_t d;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = get_user(d, data32);
		err |= put_user(d, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, LCM_GET_DSI_SSC, (unsigned long)data);
		err |= get_user(d, data);
		err |= put_user(d, data32);
		return ret ? ret : err;
	}
	case COMPAT_LCM_GET_DSI_LANE_NUM:
	{
		compat_uint_t __user *data32;
		unsigned int __user *data;
		int err;
		compat_uint_t d;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = get_user(d, data32);
		err |= put_user(d, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, LCM_GET_DSI_LANE_NUM, (unsigned long)data);
		err |= get_user(d, data);
		err |= put_user(d, data32);
		return ret ? ret : err;
	}
	case COMPAT_LCM_GET_DSI_TE:
	{
		compat_uint_t __user *data32;
		unsigned int __user *data;
		int err;
		compat_int_t i;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = get_user(i, data32);
		err |= put_user(i, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, LCM_GET_DSI_TE, (unsigned long)data);
		err |= get_user(i, data);
		err |= put_user(i, data32);
		return ret ? ret : err;
	}
	case COMPAT_LCM_GET_DSI_TIMING:
	{
		struct compat_mipi_timing *data32;
		struct MIPI_TIMING __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = compat_get_mipi_timing(data32, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, LCM_GET_DSI_TIMING, (unsigned long)data);
		err = compat_put_mipi_timing(data32, data);
		return ret ? ret : err;
	}
	case COMPAT_MIPI_SET_TIMING:
	{
		struct compat_mipi_timing *data32;
		struct MIPI_TIMING __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = compat_get_mipi_timing(data32, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, MIPI_SET_TIMING, (unsigned long)data);
		err = compat_put_mipi_timing(data32, data);
		return ret ? ret : err;
	}
	case COMPAT_FB_LAYER_GET_EN:
	{
		struct compat_pm_layer_en *data32;
		struct PM_LAYER_EN __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = compat_get_pm_layer_en(data32, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, FB_LAYER_GET_EN, (unsigned long)data);
		err = compat_put_pm_layer_en(data32, data);
		return ret ? ret : err;
	}
	case COMPAT_FB_LAYER_GET_INFO:
	{
		struct compat_pm_layer_info *data32;
		struct PM_LAYER_INFO __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = compat_get_pm_layer_info(data32, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, FB_LAYER_GET_INFO, (unsigned long)data);
		err = compat_put_pm_layer_info(data32, data);
		return ret ? ret : err;
	}
	case COMPAT_FB_LAYER_DUMP:
	{
		ret = file->f_op->unlocked_ioctl(file, FB_LAYER_DUMP, (unsigned long)arg);
		return ret;
	}
	case COMPAT_LCM_GET_ESD:
	{
		struct compat_esd_para *data32;
		struct ESD_PARA __user *data;
		int err;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = compat_get_esd_para(data32, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, LCM_GET_ESD, (unsigned long)data);
		err = compat_put_esd_para(data32, data);
		return ret ? ret : err;
	}
	case COMPAT_TE_SET_ENABLE:
	{
		compat_uint_t __user *data32;
		unsigned int __user *data;
		int err;
		compat_uint_t d;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = get_user(d, data32);
		err |= put_user(d, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, TE_SET_ENABLE, (unsigned long)data);
		err |= get_user(d, data);
		err |= put_user(d, data32);
		return ret ? ret : err;
	}
	case COMPAT_DRIVER_IC_RESET:
	{
		compat_uint_t __user *data32;
		unsigned int __user *data;
		int err;
		compat_uint_t d;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = get_user(d, data32);
		err |= put_user(d, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, DRIVER_IC_RESET, (unsigned long)data);
		err |= get_user(d, data);
		err |= put_user(d, data32);
		return ret ? ret : err;
	}
	case COMPAT_FB_GET_MISC:
	{
		compat_uint_t __user *data32;
		unsigned int __user *data;
		int err;
		compat_uint_t d;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (NULL == data)
			return -EFAULT;

		err = get_user(d, data32);
		err |= put_user(d, data);
		if (err)
			return err;

		ret = file->f_op->unlocked_ioctl(file, FB_GET_MISC, (unsigned long)data);
		err |= get_user(d, data);
		err |= put_user(d, data32);
		return ret ? ret : err;
	}
	default:
		return ret;
	}
}

#endif
/* end CONFIG_COMPAT */

static const struct file_operations fbconfig_fops = {
	.read = fbconfig_read,
	.write = fbconfig_write,
	.open = fbconfig_open,
	.unlocked_ioctl = fbconfig_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_fbconfig_ioctl,
#endif
	.release = fbconfig_release,
};

void PanelMaster_Init(void)
{
	ConfigPara_dbgfs = debugfs_create_file("fbconfig",
					       S_IFREG | S_IRUGO, NULL, (void *)0, &fbconfig_fops);

	INIT_LIST_HEAD(&head_list.list);
}

void PanelMaster_Deinit(void)
{
	debugfs_remove(ConfigPara_dbgfs);
}
