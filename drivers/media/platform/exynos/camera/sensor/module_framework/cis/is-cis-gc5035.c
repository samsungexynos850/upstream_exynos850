/*
 * Samsung Exynos5 SoC series Sensor driver
 *
 *
 * Copyright (c) 2018 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>
#include <videodev2_exynos_camera.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include <exynos-is-sensor.h>
#include "is-hw.h"
#include "is-core.h"
#include "is-param.h"
#include "is-device-sensor.h"
#include "is-device-sensor-peri.h"
#include "is-resourcemgr.h"
#include "is-dt.h"
#include "is-cis-gc5035.h"
#include "is-cis-gc5035-setA.h"
#include "is-cis-gc5035-setB.h"

#include "is-helper-i2c.h"
#include "is-vender-specific.h"

#define SENSOR_NAME "GC5035"
#define GET_CLOSEST(x1, x2, x3) (x3 - x1 >= x2 - x3 ? x2 : x1)
#define MULTIPLE_OF_4(val) ((val >> 2) << 2)

#define POLL_TIME_MS (1)
#define POLL_TIME_US (1000)
#define STREAM_OFF_POLL_TIME_MS (500)
#define STREAM_ON_POLL_TIME_MS (500)

static const struct v4l2_subdev_ops subdev_ops;

#if defined(CONFIG_VENDER_MCD_V2)
//extern const struct is_vender_rom_addr *vender_rom_addr[SENSOR_POSITION_MAX];
#endif

static const u32 *sensor_gc5035_global;
static u32 sensor_gc5035_global_size;
static const u32 **sensor_gc5035_setfiles;
static const u32 *sensor_gc5035_setfile_sizes;
static const struct sensor_pll_info_compact **sensor_gc5035_pllinfos;
static u32 sensor_gc5035_max_setfile_num;
static const u32 *sensor_gc5035_fsync_master;
static u32 sensor_gc5035_fsync_master_size;
static const u32 *sensor_gc5035_fsync_slave;
static u32 sensor_gc5035_fsync_slave_size;
static const u32 *sensor_gc5035_dpc_init_setting;
static u32 sensor_gc5035_dpc_init_setting_size;
static const u32 *sensor_gc5035_dpc_function_enable;
static u32 sensor_gc5035_dpc_function_enable_size;
static const u32 *sensor_gc5035_image_direction_setting;
static u32 sensor_gc5035_image_direction_setting_size;

#if USE_GROUP_PARAM_HOLD
static int sensor_gc5035_group_param_hold_func(struct v4l2_subdev *subdev, unsigned int hold)
{
	int ret = 0;
	struct is_cis *cis = NULL;
	struct i2c_client *client = NULL;

	FIMC_BUG(!subdev);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	if (hold == cis->cis_data->group_param_hold) {
		pr_debug("already group_param_hold (%d)\n", cis->cis_data->group_param_hold);
		goto p_err;
	}

	ret = is_sensor_write8(client, 0x0104, hold);
	if (ret < 0)
		goto p_err;

	cis->cis_data->group_param_hold = hold;
	ret = 1;
p_err:
	return ret;
}
#else
static inline int sensor_gc5035_group_param_hold_func(struct v4l2_subdev *subdev, unsigned int hold)
{
	return 0;
}
#endif

static bool sensor_gc5035_check_master_stream_off(struct is_core *core)
{
	if (test_bit(IS_SENSOR_OPEN, &(core->sensor[0].state)) &&	/* Dual mode and master stream off */
			!test_bit(IS_SENSOR_FRONT_START, &(core->sensor[0].state)))
		return true;
	else
		return false;
}

static void sensor_gc5035_data_calculation(const struct sensor_pll_info_compact *pll_info, cis_shared_data *cis_data)
{
	u32 vt_pix_clk_hz = 0;
	u32 frame_rate = 0, max_fps = 0, frame_valid_us = 0;

	FIMC_BUG_VOID(!pll_info);

	/* 1. get pclk value from pll info */
	vt_pix_clk_hz = pll_info->pclk;

	/* 2. the time of processing one frame calculation (us) */
	cis_data->min_frame_us_time = ((pll_info->frame_length_lines * pll_info->line_length_pck)
								/ (vt_pix_clk_hz / (1000 * 1000)));
	cis_data->cur_frame_us_time = cis_data->min_frame_us_time;

	/* 3. FPS calculation */
	frame_rate = vt_pix_clk_hz / (pll_info->frame_length_lines * pll_info->line_length_pck);
	dbg_sensor(2, "frame_rate (%d) = vt_pix_clk_hz(%d) / "
		KERN_CONT "(pll_info->frame_length_lines(%d) * pll_info->line_length_pck(%d))\n",
		frame_rate, vt_pix_clk_hz, pll_info->frame_length_lines, pll_info->line_length_pck);

	/* calculate max fps */
	max_fps = (vt_pix_clk_hz * 10) / (pll_info->frame_length_lines * pll_info->line_length_pck);
	max_fps = (max_fps % 10 >= 5 ? frame_rate + 1 : frame_rate);

	cis_data->pclk = vt_pix_clk_hz;
	cis_data->max_fps = max_fps;
	cis_data->frame_length_lines = pll_info->frame_length_lines;
	cis_data->line_length_pck = pll_info->line_length_pck;
	cis_data->line_readOut_time = sensor_cis_do_div64((u64)cis_data->line_length_pck * (u64)(1000 * 1000 * 1000), cis_data->pclk);
	cis_data->rolling_shutter_skew = (cis_data->cur_height - 1) * cis_data->line_readOut_time;
	cis_data->stream_on = false;

	/* Frame valid time calcuration */
	frame_valid_us = sensor_cis_do_div64((u64)cis_data->cur_height * (u64)cis_data->line_length_pck * (u64)(1000 * 1000), cis_data->pclk);
	cis_data->frame_valid_us_time = (int)frame_valid_us;

	dbg_sensor(2, "%s\n", __func__);
	dbg_sensor(2, "Sensor size(%d x %d) setting: SUCCESS!\n", cis_data->cur_width, cis_data->cur_height);
	dbg_sensor(2, "Frame Valid(us): %d\n", frame_valid_us);
	dbg_sensor(2, "rolling_shutter_skew: %lld\n", cis_data->rolling_shutter_skew);

	dbg_sensor(2, "Fps: %d, max fps(%d)\n", frame_rate, cis_data->max_fps);
	dbg_sensor(2, "min_frame_time(%d us)\n", cis_data->min_frame_us_time);
	dbg_sensor(2, "Pixel rate(Mbps): %d\n", cis_data->pclk / 1000000);

	/* Frame period calculation */
	cis_data->frame_time = (cis_data->line_readOut_time * cis_data->cur_height / 1000);
	cis_data->rolling_shutter_skew = (cis_data->cur_height - 1) * cis_data->line_readOut_time;

	dbg_sensor(2, "[%s] frame_time(%d), rolling_shutter_skew(%lld)\n", __func__,
	cis_data->frame_time, cis_data->rolling_shutter_skew);

	/* Constant values */
	cis_data->min_fine_integration_time = SENSOR_GC5035_FINE_INTEGRATION_TIME_MIN;
	cis_data->max_fine_integration_time = SENSOR_GC5035_FINE_INTEGRATION_TIME_MAX;
	cis_data->min_coarse_integration_time = SENSOR_GC5035_COARSE_INTEGRATION_TIME_MIN;
	cis_data->max_margin_coarse_integration_time = SENSOR_GC5035_COARSE_INTEGRATION_TIME_MAX_MARGIN;
	info("%s: done", __func__);
}

static int sensor_gc5035_wait_stream_off_status(cis_shared_data *cis_data)
{
	int ret = 0;
	u32 timeout = 0;

	FIMC_BUG(!cis_data);

#define STREAM_OFF_WAIT_TIME 250
	while (timeout < STREAM_OFF_WAIT_TIME) {
		if (cis_data->is_active_area == false &&
				cis_data->stream_on == false) {
			pr_debug("actual stream off\n");
			break;
		}
		timeout++;
	}

	if (timeout == STREAM_OFF_WAIT_TIME) {
		pr_err("actual stream off wait timeout\n");
		ret = -1;
	}

	return ret;
}

int sensor_gc5035_check_rev(struct is_cis *cis)
{
	int ret = 0;
	u8 rev = 0;
	struct i2c_client *client;

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		return ret;
	}
	probe_info("gc5035 cis_check_rev start\n");

	/* Init setting for otp */
	ret = sensor_cis_set_registers_addr8(cis->subdev, sensor_gc5035_dpc_init_setting, sensor_gc5035_dpc_init_setting_size);
	if (ret < 0) {
		err("sensor_gc5035_set_registers fail!!");
		goto p_err;
	}

	/* read chip id */
	ret = is_sensor_addr8_write8(client, 0xfe, 0x02);
	if (ret < 0) {
		err("sensor_gc5035_set_registers fail to write page select");
		goto p_err;
	}
	ret = is_sensor_addr8_write8(client, 0x69, 0x00);
	if (ret < 0) {
		err("sensor_gc5035_set_registers fail to write access address high");
		goto p_err;
	}
	ret = is_sensor_addr8_write8(client, 0x6a, 0x08);
	if (ret < 0) {
		err("sensor_gc5035_set_registers fail to write access address low");
		goto p_err;
	}
	ret = is_sensor_addr8_write8(client, 0xf3, 0x20);
	if (ret < 0) {
		err("sensor_gc5035_set_registers fail to write pulse");
		goto p_err;
	}

	ret = is_sensor_addr8_read8(client, 0x6c, &rev);
	if (ret < 0) {
		err("sensor_gc5035_set_registers fail to read rev value");
		goto p_err;
	}

	cis->cis_data->cis_rev = rev;
	probe_info("gc5035 rev: 0x%02x", rev);

p_err:
	return ret;
}

//For finding the nearest value in the gain table 
u32 sensor_gc5035_calc_again_closest(u32 permile)
{
	int i;

	if (permile <= sensor_gc5035_analog_gain[CODE_GAIN_INDEX][PERMILE_GAIN_INDEX])
		return sensor_gc5035_analog_gain[CODE_GAIN_INDEX][PERMILE_GAIN_INDEX];
	if (permile >= sensor_gc5035_analog_gain[MAX_GAIN_INDEX-1][PERMILE_GAIN_INDEX])
		return sensor_gc5035_analog_gain[MAX_GAIN_INDEX-1][PERMILE_GAIN_INDEX];

	for (i = 0; i < MAX_GAIN_INDEX; i++)
	{
		if (sensor_gc5035_analog_gain[i][PERMILE_GAIN_INDEX] == permile)
			return sensor_gc5035_analog_gain[i][PERMILE_GAIN_INDEX];

		if ((int)(permile - sensor_gc5035_analog_gain[i][PERMILE_GAIN_INDEX]) < 0)
			return sensor_gc5035_analog_gain[i-1][PERMILE_GAIN_INDEX];
	}

	return sensor_gc5035_analog_gain[MAX_GAIN_INDEX][PERMILE_GAIN_INDEX];
}

u32 sensor_gc5035_calc_again_permile(u8 code)
{
	u32 ret = 0;
	int i;

	for (i = 0; i < MAX_GAIN_INDEX; i++)
	{
		if (sensor_gc5035_analog_gain[i][0] == code) {
			ret = sensor_gc5035_analog_gain[i][1];
			break;
		}
	}

	return ret;
}

u32 sensor_gc5035_calc_again_code(u32 permile)
{
	u32 ret = 0, nearest_val = 0;
	int i;

	dbg_sensor(2, "[%s] permile %d\n", __func__, permile);
	nearest_val = sensor_gc5035_calc_again_closest(permile);
	dbg_sensor(2, "[%s] nearest_val %d\n", __func__, nearest_val);

	for (i = 0; i < MAX_GAIN_INDEX; i++)
	{
		if (sensor_gc5035_analog_gain[i][1] == nearest_val) {
			ret = sensor_gc5035_analog_gain[i][0];
			break;
		}
	}

	return ret;
}

u32 sensor_gc5035_calc_dgain_code(u32 input_gain, u32 permile)
{
	u32 calc_value = 0;
	u8 digital_gain = 0;

	calc_value = input_gain * 1000 / permile;
	digital_gain = (calc_value * 256) / 1000;

	dbg_sensor(2, "[%s] input_gain : %d, calc_value : %d, digital_gain : %d \n",
        __func__, input_gain, calc_value, digital_gain);

	return digital_gain;
}

int sensor_gc5035_set_exposure_time(struct v4l2_subdev *subdev, struct ae_param *target_exposure)
{
	int ret = 0;
	int hold = 0;
	struct is_cis *cis;
	struct i2c_client *client;
	cis_shared_data *cis_data;

	u32 vt_pic_clk_freq_mhz = 0;
	u16 long_coarse_int = 0;
	u16 short_coarse_int = 0;
	u32 line_length_pck = 0;
	u32 min_fine_int = 0;

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		return ret;
	}

	if ((target_exposure->long_val <= 0) || (target_exposure->short_val <= 0)) {
		err("[%s] invalid target exposure(%d, %d)\n", __func__,
				target_exposure->long_val, target_exposure->short_val);
		ret = -EINVAL;
		goto p_err;
	}

	cis_data = cis->cis_data;

	dbg_sensor(2, "[MOD:D:%d] %s, vsync_cnt(%d), target long(%d), short(%d)\n", cis->id, __func__,
			cis_data->sen_vsync_count, target_exposure->long_val, target_exposure->short_val);

	vt_pic_clk_freq_mhz = cis_data->pclk / (1000 * 1000);
	line_length_pck = cis_data->line_length_pck;
	min_fine_int = cis_data->min_fine_integration_time;

	dbg_sensor(2, "[MOD:D:%d] %s, vt_pic_clk_freq_mhz (%d), line_length_pck(%d), min_fine_int (%d)\n",
		cis->id, __func__, vt_pic_clk_freq_mhz, line_length_pck, min_fine_int);

	long_coarse_int = ((target_exposure->long_val * vt_pic_clk_freq_mhz) - min_fine_int) / line_length_pck;
	short_coarse_int = ((target_exposure->short_val * vt_pic_clk_freq_mhz) - min_fine_int) / line_length_pck;

	if (long_coarse_int > cis_data->max_coarse_integration_time) {
		long_coarse_int = cis_data->max_coarse_integration_time;
		dbg_sensor(2, "[MOD:D:%d] %s, vsync_cnt(%d), long coarse(%d) max\n", cis->id, __func__,
			cis_data->sen_vsync_count, long_coarse_int);
	}

	if (short_coarse_int > cis_data->max_coarse_integration_time) {
		short_coarse_int = cis_data->max_coarse_integration_time;
		dbg_sensor(2, "[MOD:D:%d] %s, vsync_cnt(%d), short coarse(%d) max\n", cis->id, __func__,
			cis_data->sen_vsync_count, short_coarse_int);
	}

	if (long_coarse_int < cis_data->min_coarse_integration_time) {
		long_coarse_int = cis_data->min_coarse_integration_time;
		dbg_sensor(2, "[MOD:D:%d] %s, vsync_cnt(%d), long coarse(%d) min\n", cis->id, __func__,
			cis_data->sen_vsync_count, long_coarse_int);
	}

	if (short_coarse_int < cis_data->min_coarse_integration_time) {
		short_coarse_int = cis_data->min_coarse_integration_time;
		dbg_sensor(2, "[MOD:D:%d] %s, vsync_cnt(%d), short coarse(%d) min\n", cis->id, __func__,
			cis_data->sen_vsync_count, short_coarse_int);
	}

	short_coarse_int = (short_coarse_int / 4)*4;

	dbg_sensor(2, "[MOD:D:%d] %s, frame_length_lines(%#x), long_coarse_int %#x, short_coarse_int %#x\n",
		cis->id, __func__, cis_data->frame_length_lines, long_coarse_int, short_coarse_int);

	I2C_MUTEX_LOCK(cis->i2c_lock);

	hold = sensor_gc5035_group_param_hold_func(subdev, 0x01);
	if (hold < 0) {
		ret = hold;
		goto p_err;
	}

	/* Page Selection */
	ret = is_sensor_addr8_write8(client, 0xfe, 0x00);
	if (ret < 0)
		 goto p_err;

	/* Short exposure */
	ret = is_sensor_addr8_write8(client, 0x03, (short_coarse_int >> 8) & 0x3f);
	if (ret < 0)
		goto p_err;
	ret = is_sensor_addr8_write8(client, 0x04, (short_coarse_int & 0xff));
	if (ret < 0)
		goto p_err;

p_err:
	if (hold > 0) {
		hold = sensor_gc5035_group_param_hold_func(subdev, 0x00);
		if (hold < 0)
			ret = hold;
	}

	I2C_MUTEX_UNLOCK(cis->i2c_lock);

	return ret;
}

int sensor_gc5035_set_analog_gain(struct v4l2_subdev *subdev, struct ae_param *again)
{
	int ret = 0;
	int hold = 0;
	struct is_cis *cis;
	struct i2c_client *client;
	cis_shared_data *cis_data;

	u32 analog_gain = 0;
	u32 analog_permile = 0;
	u8 digital_gain = 0;
	u8 digital_gain_b1 = 0;

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		return ret;
	}

	cis_data = cis->cis_data;

	analog_gain = sensor_gc5035_calc_again_code(again->val);
	analog_permile = sensor_gc5035_calc_again_permile(analog_gain);
	digital_gain = sensor_gc5035_calc_dgain_code(again->val, analog_permile);
	if (analog_gain < cis->cis_data->min_analog_gain[0]) {
		analog_gain = cis->cis_data->min_analog_gain[0];
	}

	if (analog_gain > cis->cis_data->max_analog_gain[0]) {
		analog_gain = cis->cis_data->max_analog_gain[0];
	}

	dbg_sensor(2, "[MOD:D:%d] %s, input_again = %d us, analog_gain(%#x), analog_permile %d, digital_gain %d  Aminmax (%d, %d)   Dminmax (%d %d)\n",
			cis->id, __func__, again->val, analog_gain, analog_permile, digital_gain,
			cis->cis_data->min_analog_gain[0], analog_gain > cis->cis_data->max_analog_gain[0],
			cis->cis_data->min_digital_gain[0], analog_gain > cis->cis_data->max_digital_gain[0]);

	I2C_MUTEX_LOCK(cis->i2c_lock);

	hold = sensor_gc5035_group_param_hold_func(subdev, 0x01);
	if (hold < 0) {
		ret = hold;
		goto p_err;
	}

	ret = is_sensor_addr8_write8(client, 0xfe, 0x00);
	if (ret < 0)
		 goto p_err;

	/* Analog gain */
	ret = is_sensor_addr8_write8(client, 0xb6, analog_gain);
	if (ret < 0)
		goto p_err;

	digital_gain_b1 = again->val/analog_permile;
	if(digital_gain_b1 < 1)
		digital_gain_b1 = 1;

	/* Digital gain int*/
	ret = is_sensor_addr8_write8(client, 0xb1, digital_gain_b1);
	if (ret < 0)
		goto p_err;

	/* Digital gain decimal*/
	ret = is_sensor_addr8_write8(client, 0xb2, digital_gain);
		if (ret < 0)
			goto p_err;

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	if (hold > 0) {
		hold = sensor_gc5035_group_param_hold_func(subdev, 0x00);
		if (hold < 0)
			ret = hold;
	}

	I2C_MUTEX_UNLOCK(cis->i2c_lock);

	return ret;
}

#if USE_OTP_AWB_CAL_DATA
// Do nothing ! Digital gains are used to compensate for the AWB M2M (module to mudule) variation
int sensor_gc5035_set_digital_gain(struct v4l2_subdev *subdev, struct ae_param *dgain)
{
	return 0;
}
#else
int sensor_gc5035_set_digital_gain(struct v4l2_subdev *subdev, struct ae_param *dgain)
{
	int ret = 0;
	int hold = 0;
	struct is_cis *cis;
	struct i2c_client *client;
	cis_shared_data *cis_data;

	u16 long_gain = 0;
	u16 short_gain = 0;
	u8 dgains[2] = {0};

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);
	FIMC_BUG(!dgain);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		return ret;
	}

	/*skip to set dgain when use_dgain is false */
	if (cis->use_dgain == false) {
		return 0;
	};

	cis_data = cis->cis_data;

	long_gain = (u16)sensor_cis_calc_dgain_code(dgain->long_val);
	short_gain = (u16)sensor_cis_calc_dgain_code(dgain->short_val);

	if (long_gain < cis->cis_data->min_digital_gain[0]) {
		long_gain = cis->cis_data->min_digital_gain[0];
	}
	if (long_gain > cis->cis_data->max_digital_gain[0]) {
		long_gain = cis->cis_data->max_digital_gain[0];
	}

	if (short_gain < cis->cis_data->min_digital_gain[0]) {
		short_gain = cis->cis_data->min_digital_gain[0];
	}
	if (short_gain > cis->cis_data->max_digital_gain[0]) {
		short_gain = cis->cis_data->max_digital_gain[0];
	}

	dbg_sensor(2, "[MOD:D:%d] %s, input_dgain = %d/%d us, long_gain(%#x), short_gain(%#x)\n",
			cis->id, __func__, dgain->long_val, dgain->short_val, long_gain, short_gain);

	I2C_MUTEX_LOCK(cis->i2c_lock);

	hold = sensor_gc5035_group_param_hold_func(subdev, 0x01);
	if (hold < 0) {
		ret = hold;
		goto p_err;
	}

	dgains[0] = dgains[1] = short_gain;

	ret = is_sensor_addr8_write8(client, 0xfe, 0x00);
	if (ret < 0)
		 goto p_err;

	/* Digital gain int*/
	ret = is_sensor_addr8_write8(client, 0xb1, (short_gain >> 8) & 0x0f);
	if (ret < 0)
		goto p_err;

	/* Digital gain decimal*/
	ret = is_sensor_addr8_write8(client, 0xb2, short_gain & 0xfc);
		if (ret < 0)
			goto p_err;

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	if (hold > 0) {
		hold = sensor_gc5035_group_param_hold_func(subdev, 0x00);
		if (hold < 0)
			ret = hold;
	}

	I2C_MUTEX_UNLOCK(cis->i2c_lock);
	return ret;
}
#endif

/* CIS OPS */
int sensor_gc5035_cis_init(struct v4l2_subdev *subdev)
{
	int ret = 0;
	struct is_cis *cis;
	u32 setfile_index = 0;
	cis_setting_info setinfo;

#if USE_OTP_AWB_CAL_DATA
	struct i2c_client *client = NULL;
	u8 selected_page;
	u16 data16[4];
	u8 cal_map_ver[4];
	bool skip_cal_write = false;
#endif

	setinfo.param = NULL;
	setinfo.return_value = 0;

	FIMC_BUG(!subdev);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);
	if (!cis) {
		err("cis is NULL");
		ret = -EINVAL;
		goto p_err;
	}

#if USE_OTP_AWB_CAL_DATA
	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		goto p_err;
	}
#endif

	FIMC_BUG(!cis->cis_data);
	memset(cis->cis_data, 0, sizeof(cis_shared_data));
	cis->rev_flag = false;

	info("[%s] gc5035 init\n", __func__);

	cis->cis_data->cur_width = SENSOR_GC5035_MAX_WIDTH;
	cis->cis_data->cur_height = SENSOR_GC5035_MAX_HEIGHT;
	cis->cis_data->low_expo_start = 33000;
	cis->need_mode_change = false;

	sensor_gc5035_data_calculation(sensor_gc5035_pllinfos[setfile_index], cis->cis_data);

	setinfo.return_value = 0;
	CALL_CISOPS(cis, cis_get_min_exposure_time, subdev, &setinfo.return_value);
	dbg_sensor(2, "[%s] min exposure time : %d\n", __func__, setinfo.return_value);
	setinfo.return_value = 0;
	CALL_CISOPS(cis, cis_get_max_exposure_time, subdev, &setinfo.return_value);
	dbg_sensor(2, "[%s] max exposure time : %d\n", __func__, setinfo.return_value);
	setinfo.return_value = 0;
	CALL_CISOPS(cis, cis_get_min_analog_gain, subdev, &setinfo.return_value);
	dbg_sensor(2, "[%s] min again : %d\n", __func__, setinfo.return_value);
	setinfo.return_value = 0;
	CALL_CISOPS(cis, cis_get_max_analog_gain, subdev, &setinfo.return_value);
	dbg_sensor(2, "[%s] max again : %d\n", __func__, setinfo.return_value);
	setinfo.return_value = 0;
	CALL_CISOPS(cis, cis_get_min_digital_gain, subdev, &setinfo.return_value);
	dbg_sensor(2, "[%s] min dgain : %d\n", __func__, setinfo.return_value);
	setinfo.return_value = 0;
	CALL_CISOPS(cis, cis_get_max_digital_gain, subdev, &setinfo.return_value);
	dbg_sensor(2, "[%s] max dgain : %d\n", __func__, setinfo.return_value);

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	return ret;
}

int sensor_gc5035_cis_log_status(struct v4l2_subdev *subdev)
{
	int ret = 0;
	struct is_cis *cis;
	struct i2c_client *client = NULL;

	FIMC_BUG(!subdev);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);
	if (!cis) {
		err("cis is NULL");
		return -EINVAL;
	}

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		return -EINVAL;
	}

	I2C_MUTEX_LOCK(cis->i2c_lock);
	sensor_cis_dump_registers(subdev, sensor_gc5035_setfiles[0], sensor_gc5035_setfile_sizes[0]);

	pr_err("[SEN:DUMP] *******************************\n");
	I2C_MUTEX_UNLOCK(cis->i2c_lock);

	return ret;
}

/* Input
 *	hold : true - hold, flase - no hold
 * Output
 *      return: 0 - no effect(already hold or no hold)
 *		positive - setted by request
 *		negative - ERROR value
 */
int sensor_gc5035_cis_group_param_hold(struct v4l2_subdev *subdev, bool hold)
{
	int ret = 0;
	struct is_cis *cis = NULL;

	FIMC_BUG(!subdev);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	I2C_MUTEX_LOCK(cis->i2c_lock);
	ret = sensor_gc5035_group_param_hold_func(subdev, hold);
	I2C_MUTEX_UNLOCK(cis->i2c_lock);

	return ret;
}

static int sensor_gc5035_cis_dpc_enable(struct v4l2_subdev *subdev) {
	int ret = 0;
	u8 num_defect_1;
	u8 num_defect_2;
	u8 num_defect_total;
	char const *setfile;

	struct is_cis *cis;
	struct i2c_client *client;
	struct device_node *dnode;
	struct device *dev;

	FIMC_BUG(!subdev);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);
	if (!cis) {
		err("cis is NULL");
		return -EINVAL;
	}

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		return -EINVAL;
	}

	dev = &client->dev;
	dnode = dev->of_node;

	/* Step 1. Basic setting for OTP and Check the chip id */
	ret = sensor_gc5035_check_rev(cis);
	if (ret < 0) {
		err("Failed to read rev");
		return ret;
	}

	if (cis->cis_data->cis_rev <= SENSOR_GC5035_CHIP_ID_WC1XA) {
		warn("[%s] Disable DPC", __func__);

		return 0;
	} else {
		info("[%s] Enable DPC", __func__);

		ret = of_property_read_string(dnode, "setfile", &setfile);
		if (ret) {
			err("setfile index read fail(%d), take default setfile!!", ret);
			setfile = "default";
		}

		if (strcmp(setfile, "default") == 0 || strcmp(setfile, "setA") == 0) {
			info("%s dpc_setfile_A\n", __func__);
			sensor_gc5035_setfiles = sensor_gc5035_dpc_setfiles_A;
			sensor_gc5035_setfile_sizes = sensor_gc5035_dpc_setfile_A_sizes;
		} else if (strcmp(setfile, "setB") == 0) {
			info("%s dpc_setfile_B\n", __func__);
			sensor_gc5035_setfiles = sensor_gc5035_dpc_setfiles_B;
			sensor_gc5035_setfile_sizes = sensor_gc5035_dpc_setfile_B_sizes;
		} else {
			err("%s setfile index out of bound, take default (dpc_setfile_A)", __func__);
			sensor_gc5035_setfiles = sensor_gc5035_dpc_setfiles_A;
			sensor_gc5035_setfile_sizes = sensor_gc5035_dpc_setfile_A_sizes;
		}
	}

	/* Step 2. To prepare for checking how many Static DPC in OTP */
	/* Page Selection */
	ret = is_sensor_addr8_write8(client, 0xfe, 0x02);
	if (ret < 0) {
		err("sensor_gc5035_set_registers fail!!");
		goto p_err;
	}

	ret = is_sensor_addr8_write8(client, 0xbe, 0x00);
	if (ret < 0) {
		goto p_err;
	}
	ret = is_sensor_addr8_write8(client, 0xa9, 0x01);
	if (ret < 0) {
		goto p_err;
	}

	/* Step 3. DPC Table Auto Load Prepare setting 2 (To check the number of OTP DPC) */
	/* Read OTP 0x0070 */
	ret = is_sensor_addr8_write8(client, 0x69, 0x00);
	if (ret < 0)
		goto p_err;
	ret = is_sensor_addr8_write8(client, 0x6a, 0x70);
	if (ret < 0)
		goto p_err;
	ret = is_sensor_addr8_write8(client, 0xf3, 0x20);
	if (ret < 0)
		goto p_err;

	ret = is_sensor_addr8_read8(client, 0x6c, &num_defect_1);
	if (ret < 0)
		goto p_err;
	ret = is_sensor_addr8_write8(client, 0x69, 0x00);
	if (ret < 0)
		goto p_err;

	/* Read OTP 0x0078 */
	ret = is_sensor_addr8_write8(client, 0x6a, 0x78);
	if (ret < 0)
		goto p_err;
	ret = is_sensor_addr8_write8(client, 0xf3, 0x20);
	if (ret < 0)
		goto p_err;

	ret = is_sensor_addr8_read8(client, 0x6c, &num_defect_2);
	if (ret < 0)
		goto p_err;

	num_defect_total = num_defect_1 + num_defect_2;
	if (num_defect_total > 64) {
		warn("Total defect number is over than 64");
		num_defect_total = 64;
	}
	info("DPC defect num1:%d num2:%d total:%d", num_defect_1, num_defect_2, num_defect_total);

	/* Step 4. DPC Table Auto Load Prepare setting 3 (Write the number of OTP DPC on SRAM) */
	/* Set DD total Number (including error number) (P2:0x01 & P2:0x02) */
	ret = is_sensor_addr8_write8(client, 0x01, 0x00);
	if (ret < 0)
		goto p_err;
	ret = is_sensor_addr8_write8(client, 0x02, num_defect_total);
	if (ret < 0)
		goto p_err;
	ret = is_sensor_addr8_write8(client, 0x03, 0x00);
	if (ret < 0)
		goto p_err;
	ret = is_sensor_addr8_write8(client, 0x04, 0x80);
	if (ret < 0)
		goto p_err;

	/* Step 5. DPC Table Auto Load */
	ret = is_sensor_addr8_write8(client, 0x09, 0x33);
	if (ret < 0)
		goto p_err;

	/* Set 0xf3[7] = 1 to start automatic load*/
	ret = is_sensor_addr8_write8(client, 0xf3, 0x80);
	if (ret < 0)
		goto p_err;

	/* Step 6. Wait 4ms for auto loading time */
	mdelay(4);

	/* Step 7. Static DPC function Enable */
	sensor_cis_set_registers_addr8(subdev, sensor_gc5035_dpc_function_enable, sensor_gc5035_dpc_function_enable_size);

	dbg_sensor(2, "[%s] DPC enable done\n", __func__);

p_err:
	return ret;
}

int sensor_gc5035_cis_set_global_setting(struct v4l2_subdev *subdev)
{
	int ret = 0;
	char const *setfile;
	struct is_cis *cis = NULL;
	struct i2c_client *client;
	struct device_node *dnode;
	struct device *dev;

	FIMC_BUG(!subdev);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);
	FIMC_BUG(!cis);

	client = cis->client;
	FIMC_BUG(!client);

	dev = &client->dev;
	dnode = dev->of_node;

	info("[%s] global setting start\n", __func__);

	I2C_MUTEX_LOCK(cis->i2c_lock);

	/* setfile global setting is at camera entrance */
	ret = sensor_cis_set_registers_addr8(subdev, sensor_gc5035_global, sensor_gc5035_global_size);
	if (ret < 0) {
		err("sensor_gc5035_set_registers fail!!");
		goto p_i2c_err;
	}

	dbg_sensor(2, "[%s] global setting done\n", __func__);

	/* Add Image Direction Settings before dpc */
	ret = of_property_read_string(dnode, "setfile", &setfile);
	if (ret) {
		err("setfile index read fail(%d), take default setfile!!", ret);
		setfile = "default";
	}

	if (strcmp(setfile, "setB") == 0) {
		ret = sensor_cis_set_registers_addr8(subdev, sensor_gc5035_image_direction_setting, sensor_gc5035_image_direction_setting_size);
		if (ret < 0) {
			err("sensor_gc5035_image_direction_setting fail!!");
			goto p_i2c_err;
		}
	}

	/* DPC Enable */
	ret = sensor_gc5035_cis_dpc_enable(subdev);
	if (ret < 0) {
		err("sensor_gc5035_cis_dpc_enable fail!!");
		goto p_i2c_err;
	}

p_i2c_err:
	I2C_MUTEX_UNLOCK(cis->i2c_lock);

	return ret;
}

int sensor_gc5035_cis_mode_change(struct v4l2_subdev *subdev, u32 mode)
{
	int ret = 0;
	struct is_cis *cis = NULL;

	FIMC_BUG(!subdev);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);
	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	if (mode > sensor_gc5035_max_setfile_num) {
		err("invalid mode(%d)!!", mode);
		ret = -EINVAL;
		return ret;
	}

#if 0
	I2C_MUTEX_LOCK(cis->i2c_lock);
	/* If check_rev fail when cis_init, one more check_rev in mode_change */
	if (cis->rev_flag == true) {
		cis->rev_flag = false;
		ret = sensor_cis_check_rev(cis);
		if (ret < 0) {
			err("sensor_gc5035_check_rev is fail");
			goto p_i2c_err;
		}
	}
	I2C_MUTEX_UNLOCK(cis->i2c_lock);
#endif

	sensor_gc5035_data_calculation(sensor_gc5035_pllinfos[mode], cis->cis_data);

	/* This delay restrains critical issues. If entry time issue comes up, this delay should be removed */
	//msleep(50);

	I2C_MUTEX_LOCK(cis->i2c_lock);
	dbg_sensor(2, "[%s] mode changed start(%d)\n", __func__, mode);

	ret = sensor_cis_set_registers_addr8(subdev, sensor_gc5035_setfiles[mode], sensor_gc5035_setfile_sizes[mode]);
	if (ret < 0) {
		err("sensor_gc5035_set_registers fail!!");
		goto p_i2c_err;
	}

	cis->cis_data->frame_time = (cis->cis_data->line_readOut_time * cis->cis_data->cur_height / 1000);
	cis->cis_data->rolling_shutter_skew = (cis->cis_data->cur_height - 1) * cis->cis_data->line_readOut_time;
	dbg_sensor(2, "[%s] frame_time(%d), rolling_shutter_skew(%lld)\n", __func__,
		cis->cis_data->frame_time, cis->cis_data->rolling_shutter_skew);

	dbg_sensor(2, "[%s] mode changed end(%d)\n", __func__, mode);

p_i2c_err:
	I2C_MUTEX_UNLOCK(cis->i2c_lock);

	return ret;
}

/* TODO: Sensor set size sequence(sensor done, sensor stop, 3AA done in FW case */
int sensor_gc5035_cis_set_size(struct v4l2_subdev *subdev, cis_shared_data *cis_data)
{
	int ret = 0;
	bool binning = false;
	u32 ratio_w = 0, ratio_h = 0, start_x = 0, start_y = 0, end_x = 0, end_y = 0;
	struct i2c_client *client = NULL;
	struct is_cis *cis = NULL;
#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif
	FIMC_BUG(!subdev);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);
	FIMC_BUG(!cis);

	dbg_sensor(2, "[MOD:D:%d] %s\n", cis->id, __func__);

	if (unlikely(!cis_data)) {
		err("cis data is NULL");
		if (unlikely(!cis->cis_data)) {
			ret = -EINVAL;
			return ret;
		} else {
			cis_data = cis->cis_data;
		}
	}

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		return ret;
	}

	/* Wait actual stream off */
	ret = sensor_gc5035_wait_stream_off_status(cis_data);
	if (ret) {
		err("Must stream off\n");
		ret = -EINVAL;
		return ret;
	}

	binning = cis_data->binning;
	if (binning) {
		ratio_w = (SENSOR_GC5035_MAX_WIDTH / cis_data->cur_width);
		ratio_h = (SENSOR_GC5035_MAX_HEIGHT / cis_data->cur_height);
	} else {
		ratio_w = 1;
		ratio_h = 1;
	}

	if (((cis_data->cur_width * ratio_w) > SENSOR_GC5035_MAX_WIDTH) ||
		((cis_data->cur_height * ratio_h) > SENSOR_GC5035_MAX_HEIGHT)) {
		err("Config max sensor size over~!!\n");
		ret = -EINVAL;
		return ret;
	}

	/* 2. pixel address region setting */
	start_x = ((SENSOR_GC5035_MAX_WIDTH - cis_data->cur_width * ratio_w) / 2) & (~0x1);
	start_y = ((SENSOR_GC5035_MAX_HEIGHT - cis_data->cur_height * ratio_h) / 2) & (~0x1);
	end_x = start_x + (cis_data->cur_width * ratio_w - 1);
	end_y = start_y + (cis_data->cur_height * ratio_h - 1);

	if (!(end_x & (0x1)) || !(end_y & (0x1))) {
		err("Sensor pixel end address must odd\n");
		ret = -EINVAL;
		return ret;
	}

	I2C_MUTEX_LOCK(cis->i2c_lock);

	/* 1. page_select */
	ret = is_sensor_addr8_write8(client, 0xfe, 0x00);
	if (ret < 0)
		 goto p_err;

	/*2 byte address for writing the start_x*/
	ret = is_sensor_addr8_write8(client, 0x0b, (start_x >> 8));
	if (ret < 0)
		 goto p_err;

	ret = is_sensor_addr8_write8(client, 0x0c, (start_x & 0xff));
	if (ret < 0)
		 goto p_err;

	/*2 byte address for writing the start_y*/
	ret = is_sensor_addr8_write8(client, 0x09, (start_y >> 8));
	if (ret < 0)
		 goto p_err;

	ret = is_sensor_addr8_write8(client, 0x0a, (start_y & 0xff));
	if (ret < 0)
		 goto p_err;

	/*2 byte address for writing the end_x*/
	ret = is_sensor_addr8_write8(client, 0x0f, (end_x >> 8));
	if (ret < 0)
		 goto p_err;
	ret = is_sensor_addr8_write8(client, 0x10, (end_x & 0xff));
	if (ret < 0)
		 goto p_err;

	/*2 byte address for writing the end_y*/
	ret = is_sensor_addr8_write8(client, 0x0d, (end_y >> 8));
	if (ret < 0)
		 goto p_err;
	ret = is_sensor_addr8_write8(client, 0x0e, (end_y & 0xff));
	if (ret < 0)
		 goto p_err;

	/* 3. page_select */
	ret = is_sensor_addr8_write8(client, 0xfe, 0x01);
	if (ret < 0)
		 goto p_err;

	/* 4. output address setting width*/
	ret = is_sensor_addr8_write8(client, 0x97, (cis_data->cur_width >> 8));
	if (ret < 0)
		 goto p_err;
	ret = is_sensor_addr8_write8(client, 0x98, (cis_data->cur_width & 0xff));
	if (ret < 0)
		 goto p_err;

	/* 4. output address setting height*/
	ret = is_sensor_addr8_write8(client, 0x95, (cis_data->cur_height >> 8));
	if (ret < 0)
		 goto p_err;
	ret = is_sensor_addr8_write8(client, 0x96, (cis_data->cur_height & 0xff));
	if (ret < 0)
		 goto p_err;

	/* If not use to binning, sensor image should set only crop */
	if (!binning) {
		dbg_sensor(2, "Sensor size set is not binning\n");
		goto p_err;
	}

	/* 5. binnig setting */
	ret = is_sensor_write8(client, 0x33, binning);	/* 1:  binning enable, 0: disable */
	if (ret < 0)
		goto p_err;

	//TODO:scaling type address
	/* 6. scaling setting: but not use */
	/* scaling_mode (0: No scaling, 1: Horizontal, 2: Full) */

	cis_data->frame_time = (cis_data->line_readOut_time * cis_data->cur_height / 1000);
	cis->cis_data->rolling_shutter_skew = (cis->cis_data->cur_height - 1) * cis->cis_data->line_readOut_time;
	dbg_sensor(2, "[%s] frame_time(%d), rolling_shutter_skew(%lld)\n", __func__,
		cis->cis_data->frame_time, cis->cis_data->rolling_shutter_skew);

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec) * 1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	I2C_MUTEX_UNLOCK(cis->i2c_lock);

	return ret;
}

int sensor_gc5035_cis_stream_on(struct v4l2_subdev *subdev)
{
	int ret = 0;
	struct is_core *core = NULL;
	struct is_cis *cis = NULL;
	struct i2c_client *client = NULL;
	cis_shared_data *cis_data = NULL;
	struct is_device_sensor *this_device = NULL;
	bool single_mode = true; /* default single */

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	this_device = (struct is_device_sensor *)v4l2_get_subdev_hostdata(subdev);
	FIMC_BUG(!this_device);

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		return ret;
	}

	core = (struct is_core *)dev_get_drvdata(is_dev);
	if (!core) {
		err("The core device is null");
		ret = -EINVAL;
		return ret;
	}

	cis_data = cis->cis_data;

#if !defined(DISABLE_DUAL_SYNC)
	if ((this_device != &core->sensor[0]) && test_bit(IS_SENSOR_OPEN, &(core->sensor[0].state))) {
		single_mode = false;
	}
#endif

	I2C_MUTEX_LOCK(cis->i2c_lock);

	/* Sensor Dual sync on/off */
	if (single_mode) {
		/* Delay for single mode */
		msleep(50);

		info("[%s] gc5035 start (single mode)\n", __func__);
	} else {
		info("[%s] gc5035 start (dual slave mode)\n", __func__);

		ret = sensor_cis_set_registers_addr8(subdev, sensor_gc5035_fsync_slave, sensor_gc5035_fsync_slave_size);
		if (ret < 0) {
			err("[%s] sensor_gc5035_fsync_slave fail\n", __func__);
			goto p_i2c_err;
		}
		/* The delay which can change the frame-length of first frame was removed here*/
	}

	/* Page Selection */
	ret = is_sensor_addr8_write8(client, 0xFE, 0x00);
	if (ret < 0) {
		err("i2c treansfer fail addr(%x), val(%x), ret(%d)\n", 0xFE, 0x00, ret);
		goto p_i2c_err;
	}

	/* Sensor stream on */
	ret = is_sensor_addr8_write8(client, 0x3E, 0x91);
	if (ret < 0) {
		err("i2c treansfer fail addr(%x), val(%x), ret(%d)\n", 0x3e, 0x91, ret);
		goto p_i2c_err;
	}

	if (single_mode) {
		/* Delay for single mode */
		msleep(50);
	}

	cis_data->stream_on = true;
	dbg_sensor(2, "[MOD:D:%d] %s Done.\n", cis->id, __func__);

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

p_i2c_err:
	I2C_MUTEX_UNLOCK(cis->i2c_lock);

	return ret;
}

int sensor_gc5035_cis_stream_off(struct v4l2_subdev *subdev)
{
	int ret = 0;
	struct is_cis *cis;
	struct i2c_client *client;
	cis_shared_data *cis_data;

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		return ret;
	}

	cis_data = cis->cis_data;

	info("[gc5035] stop (dev %d)\n", cis->device);

	I2C_MUTEX_LOCK(cis->i2c_lock);

	/* Page Selection */
	ret = is_sensor_addr8_write8(client, 0xfe, 0x00);
	if (ret < 0)
		 goto p_err;

	/* Sensor stream off */
	ret = is_sensor_addr8_write8(client, 0x3e, 0x00);
	if (ret < 0) {
		err("i2c treansfer fail addr(%x), val(%x), ret(%d)\n", 0x3e, 0x00, ret);
		goto p_err;
	}

	cis_data->stream_on = false;
	dbg_sensor(2, "[MOD:D:%d] %s done.\n", cis->id, __func__);

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	I2C_MUTEX_UNLOCK(cis->i2c_lock);

	return ret;
}

int sensor_gc5035_cis_get_min_exposure_time(struct v4l2_subdev *subdev, u32 *min_expo)
{
	int ret = 0;
	struct is_cis *cis = NULL;
	cis_shared_data *cis_data = NULL;
	u32 min_integration_time = 0;
	u32 min_coarse = 0;
	u32 min_fine = 0;
	u32 vt_pix_clk_freq_mhz = 0;
	u32 line_length_pck = 0;

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);
	FIMC_BUG(!min_expo);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	cis_data = cis->cis_data;

	vt_pix_clk_freq_mhz = cis_data->pclk / (1000 * 1000);
	if (vt_pix_clk_freq_mhz == 0) {
		pr_err("[MOD:D:%d] %s, Invalid vt_pix_clk_freq_mhz(%d)\n", cis->id, __func__, vt_pix_clk_freq_mhz);
		goto p_err;
	}
	line_length_pck = cis_data->line_length_pck;
	min_coarse = cis_data->min_coarse_integration_time;
	min_fine = cis_data->min_fine_integration_time;

	min_integration_time = ((line_length_pck * min_coarse) + min_fine) / vt_pix_clk_freq_mhz;
	*min_expo = min_integration_time;

	dbg_sensor(2, "[%s] min integration time %d\n", __func__, min_integration_time);

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	return ret;
}

int sensor_gc5035_cis_get_max_exposure_time(struct v4l2_subdev *subdev, u32 *max_expo)
{
	int ret = 0;
	struct is_cis *cis;
	cis_shared_data *cis_data;
	u32 max_integration_time = 0;
	u32 max_coarse_margin = 0;
	u32 max_fine_margin = 0;
	u32 max_coarse = 0;
	u32 max_fine = 0;
	u32 vt_pix_clk_freq_mhz = 0;
	u32 line_length_pck = 0;
	u32 frame_length_lines = 0;

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);
	FIMC_BUG(!max_expo);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	cis_data = cis->cis_data;

	vt_pix_clk_freq_mhz = cis_data->pclk / (1000 * 1000);
	if (vt_pix_clk_freq_mhz == 0) {
		pr_err("[MOD:D:%d] %s, Invalid vt_pix_clk_freq_mhz(%d)\n", cis->id, __func__, vt_pix_clk_freq_mhz);
		goto p_err;
	}
	line_length_pck = cis_data->line_length_pck;
	frame_length_lines = cis_data->frame_length_lines;

	max_coarse_margin = cis_data->max_margin_coarse_integration_time;
	max_fine_margin = line_length_pck - cis_data->min_fine_integration_time;
	max_coarse = frame_length_lines - max_coarse_margin;
	max_fine = cis_data->max_fine_integration_time;

	max_integration_time = ((line_length_pck * max_coarse) + max_fine) / vt_pix_clk_freq_mhz;

	*max_expo = max_integration_time;

	/* TODO: Is this values update hear? */
	cis_data->max_margin_fine_integration_time = max_fine_margin;
	cis_data->max_coarse_integration_time = max_coarse;

	dbg_sensor(2, "[%s] max integration time %d, max margin fine integration %d, max coarse integration %d\n",
			__func__, max_integration_time, cis_data->max_margin_fine_integration_time, cis_data->max_coarse_integration_time);

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	return ret;
}

int sensor_gc5035_cis_adjust_frame_duration(struct v4l2_subdev *subdev,
						u32 input_exposure_time,
						u32 *target_duration)
{
	int ret = 0;
	struct is_cis *cis;
	cis_shared_data *cis_data;

	u32 vt_pix_clk_freq_mhz = 0;
	u32 line_length_pck = 0;
	u32 frame_length_lines = 0;
	u32 frame_duration = 0;
	u32 max_frame_us_time = 0;

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);
	FIMC_BUG(!target_duration);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	cis_data = cis->cis_data;

	vt_pix_clk_freq_mhz = cis_data->pclk / (1000 * 1000);
	line_length_pck = cis_data->line_length_pck;
	frame_length_lines = ((vt_pix_clk_freq_mhz * input_exposure_time) / line_length_pck);
	frame_length_lines += cis_data->max_margin_coarse_integration_time;

	max_frame_us_time = 1000000/cis->min_fps;

	frame_duration = (frame_length_lines * line_length_pck) / vt_pix_clk_freq_mhz;

	dbg_sensor(2, "[%s](vsync cnt = %d) input exp(%d), adj duration - frame duraion(%d), min_frame_us(%d)\n",
			__func__, cis_data->sen_vsync_count, input_exposure_time, frame_duration, cis_data->min_frame_us_time);

	*target_duration = MAX(frame_duration, cis_data->min_frame_us_time);
	if(cis->min_fps == cis->max_fps) {
		*target_duration = MIN(frame_duration, max_frame_us_time);
	}

	dbg_sensor(2, "[%s] requested min_fps(%d), max_fps(%d) from HAL, calculated frame_duration(%d), adjusted frame_duration(%d)\n",
		__func__, cis->min_fps, cis->max_fps, frame_duration, *target_duration);

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

	return ret;
}

int sensor_gc5035_cis_set_frame_duration(struct v4l2_subdev *subdev, u32 frame_duration)
{
	int ret = 0;
	int hold = 0;
	struct is_cis *cis;
	struct i2c_client *client;
	cis_shared_data *cis_data;
	struct is_core *core;

	u32 vt_pix_clk_freq_mhz = 0;
	u32 line_length_pck = 0;
	u16 frame_length_lines = 0;

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		return ret;
	}

	cis_data = cis->cis_data;

	core = (struct is_core *)dev_get_drvdata(is_dev);
	if (!core) {
		err("core device is null");
		ret = -EINVAL;
		return ret;
	}

	if (sensor_gc5035_check_master_stream_off(core)) {
		dbg_sensor(2, "%s: Master cam did not enter in stream_on yet. Stop updating frame_length_lines", __func__);
		return ret;
	}

	if (frame_duration < cis_data->min_frame_us_time) {
		dbg_sensor(2, "frame duration is less than min(%d)\n", frame_duration);
		frame_duration = cis_data->min_frame_us_time;
	}

	vt_pix_clk_freq_mhz = cis_data->pclk / (1000 * 1000);
	line_length_pck = cis_data->line_length_pck;

	frame_length_lines = (u16)((vt_pix_clk_freq_mhz * frame_duration) / line_length_pck);

	/* Frame length lines should be a multiple of 4 */
	frame_length_lines = MULTIPLE_OF_4(frame_length_lines);
	if (frame_length_lines > 0x3ffc) {
		warn("%s: frame_length_lines is above the maximum value : 0x%04x (should be lower than 0x3ffc)\n", __func__, frame_length_lines);
		frame_length_lines = 0x3ffc;
	}

	dbg_sensor(2, "[MOD:D:%d] %s, vt_pix_clk_freq_mhz(%#x) frame_duration = %d us,"
		KERN_CONT "line_length_pck(%#x), frame_length_lines(%#x)\n",
		cis->id, __func__, vt_pix_clk_freq_mhz, frame_duration, line_length_pck, frame_length_lines);

	I2C_MUTEX_LOCK(cis->i2c_lock);

	hold = sensor_gc5035_group_param_hold_func(subdev, 0x01);
	if (hold < 0) {
		ret = hold;
		goto p_err;
	}

	ret = is_sensor_addr8_write8(client, 0xfe, 0x00);
	if (ret < 0)
		goto p_err;

	ret = is_sensor_addr8_write8(client, 0x41, (frame_length_lines >> 8) & 0x3f);
	if (ret < 0)
		goto p_err;

	ret = is_sensor_addr8_write8(client, 0x42, (frame_length_lines & 0xfc));
	if (ret < 0)
		goto p_err;

	cis_data->cur_frame_us_time = frame_duration;
	cis_data->frame_length_lines = frame_length_lines;
	cis_data->max_coarse_integration_time = cis_data->frame_length_lines - cis_data->max_margin_coarse_integration_time;

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	if (hold > 0) {
		hold = sensor_gc5035_group_param_hold_func(subdev, 0x00);
		if (hold < 0)
			ret = hold;
	}

	I2C_MUTEX_UNLOCK(cis->i2c_lock);

	return ret;
}

int sensor_gc5035_cis_set_frame_rate(struct v4l2_subdev *subdev, u32 min_fps)
{
	int ret = 0;
	struct is_cis *cis;
	cis_shared_data *cis_data;

	u32 frame_duration = 0;

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	cis_data = cis->cis_data;

	if (min_fps > cis_data->max_fps) {
		err("[MOD:D:%d] %s, request FPS is too high(%d), set to max(%d)\n",
			cis->id, __func__, min_fps, cis_data->max_fps);
		min_fps = cis_data->max_fps;
	}

	if (min_fps == 0) {
		err("[MOD:D:%d] %s, request FPS is 0, set to min FPS(1)\n",
			cis->id, __func__);
		min_fps = 1;
	}

	frame_duration = (1 * 1000 * 1000) / min_fps;

	dbg_sensor(2, "[MOD:D:%d] %s, set FPS(%d), frame duration(%d)\n",
			cis->id, __func__, min_fps, frame_duration);

	ret = sensor_gc5035_cis_set_frame_duration(subdev, frame_duration);
	if (ret < 0) {
		err("[MOD:D:%d] %s, set frame duration is fail(%d)\n",
			cis->id, __func__, ret);
		goto p_err;
	}

	cis_data->min_frame_us_time = frame_duration;

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	return ret;
}

int sensor_gc5035_cis_adjust_analog_gain(struct v4l2_subdev *subdev, u32 input_again, u32 *target_permile)
{
	int ret = 0;
	struct is_cis *cis;
	cis_shared_data *cis_data;

	u32 again_code = 0;
	u32 again_permile = 0;

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);
	FIMC_BUG(!target_permile);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	cis_data = cis->cis_data;

	again_code = sensor_cis_calc_again_code(input_again);

	if (again_code > cis_data->max_analog_gain[0]) {
		again_code = cis_data->max_analog_gain[0];
	} else if (again_code < cis_data->min_analog_gain[0]) {
		again_code = cis_data->min_analog_gain[0];
	}

	again_permile = sensor_cis_calc_again_permile(again_code);

	dbg_sensor(2, "[%s] min again(%d), max(%d), input_again(%d), code(%d), permile(%d)\n", __func__,
			cis_data->max_analog_gain[0],
			cis_data->min_analog_gain[0],
			input_again,
			again_code,
			again_permile);

	*target_permile = again_permile;

	return ret;
}

int sensor_gc5035_cis_get_analog_gain(struct v4l2_subdev *subdev, u32 *again)
{
	int ret = 0;
	int hold = 0;
	struct is_cis *cis;
	struct i2c_client *client;

	u8 analog_gain = 0;

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);
	FIMC_BUG(!again);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		return ret;
	}

	I2C_MUTEX_LOCK(cis->i2c_lock);

	hold = sensor_gc5035_group_param_hold_func(subdev, 0x01);
	if (hold < 0) {
		ret = hold;
		goto p_err;
	}

	/*page_select */
	ret = is_sensor_addr8_write8(client, 0xfe, 0x00);
	if (ret < 0)
		 goto p_err;

	ret = is_sensor_addr8_read8(client, 0xb6, &analog_gain);
	if (ret < 0)
		goto p_err;

	*again = sensor_gc5035_calc_again_permile(analog_gain);

	dbg_sensor(2, "[MOD:D:%d] %s, cur_again = %d us, analog_gain(%#x)\n",
			cis->id, __func__, *again, analog_gain);

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	if (hold > 0) {
		hold = sensor_gc5035_group_param_hold_func(subdev, 0x00);
		if (hold < 0)
			ret = hold;
	}

	I2C_MUTEX_UNLOCK(cis->i2c_lock);

	return ret;
}

int sensor_gc5035_cis_get_min_analog_gain(struct v4l2_subdev *subdev, u32 *min_again)
{
	int ret = 0;
	struct is_cis *cis;
	struct i2c_client *client;
	cis_shared_data *cis_data;

	u16 read_value = 0;

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);
	FIMC_BUG(!min_again);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	cis_data = cis->cis_data;

	read_value = SENSOR_GC5035_MIN_ANALOG_GAIN_SET_VALUE;

	cis_data->min_analog_gain[0] = read_value;

	cis_data->min_analog_gain[1] = sensor_gc5035_calc_again_permile(read_value);

	*min_again = cis_data->min_analog_gain[1];

	dbg_sensor(2, "[%s] code %d, permile %d\n", __func__,
		cis_data->min_analog_gain[0], cis_data->min_analog_gain[1]);

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	return ret;
}

int sensor_gc5035_cis_get_max_analog_gain(struct v4l2_subdev *subdev, u32 *max_again)
{
	int ret = 0;
	struct is_cis *cis;
	struct i2c_client *client;
	cis_shared_data *cis_data;

	u8 read_value = 0;

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);
	FIMC_BUG(!max_again);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	cis_data = cis->cis_data;

	read_value = SENSOR_GC5035_MAX_ANALOG_GAIN_SET_VALUE;

	cis_data->max_analog_gain[0] = read_value;

	cis_data->max_analog_gain[1] = sensor_gc5035_calc_again_permile(read_value);

	*max_again = cis_data->max_analog_gain[1];

	dbg_sensor(2, "[%s] code %d, permile %d\n", __func__,
		cis_data->max_analog_gain[0], cis_data->max_analog_gain[1]);

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	return ret;
}

int sensor_gc5035_cis_get_digital_gain(struct v4l2_subdev *subdev, u32 *dgain)
{
	int ret = 0;
	int hold = 0;
	struct is_cis *cis;
	struct i2c_client *client;

	u8 digital_gain = 0;
	u8 digital_gain_dec = 0;

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);
	FIMC_BUG(!dgain);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		return ret;
	}

	I2C_MUTEX_LOCK(cis->i2c_lock);

	hold = sensor_gc5035_group_param_hold_func(subdev, 0x01);
	if (hold < 0) {
		ret = hold;
		goto p_err;
	}

	ret = is_sensor_addr8_write8(client, 0xfe, 0x00);
	if (ret < 0)
		 goto p_err;

	/* Digital gain int*/
	ret = is_sensor_addr8_read8(client, 0xb1, &digital_gain);
	if (ret < 0)
		goto p_err;

	/* Digital gain decimal*/
	ret = is_sensor_addr8_read8(client, 0xb2, &digital_gain_dec);
	if (ret < 0)
		goto p_err;

	*dgain = sensor_cis_calc_dgain_permile(digital_gain);

	dbg_sensor(2, "[MOD:D:%d] %s, cur_dgain = %d us, digital_gain(%#x)\n",
			cis->id, __func__, *dgain, digital_gain);

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	if (hold > 0) {
		hold = sensor_gc5035_group_param_hold_func(subdev, 0x00);
		if (hold < 0)
			ret = hold;
	}

	I2C_MUTEX_UNLOCK(cis->i2c_lock);

	return ret;
}

int sensor_gc5035_cis_get_min_digital_gain(struct v4l2_subdev *subdev, u32 *min_dgain)
{
	int ret = 0;
	struct is_cis *cis;
	struct i2c_client *client;
	cis_shared_data *cis_data;

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);
	FIMC_BUG(!min_dgain);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	cis_data = cis->cis_data;

	cis_data->min_digital_gain[0] = SENSOR_GC5035_MIN_DIGITAL_GAIN_SET_VALUE;
	cis_data->min_digital_gain[1] = sensor_cis_calc_dgain_permile(cis_data->min_digital_gain[0]);

	*min_dgain = cis_data->min_digital_gain[1];

	dbg_sensor(2, "[%s] code %d, permile %d\n", __func__,
		cis_data->min_digital_gain[0], cis_data->min_digital_gain[1]);

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	return ret;
}

int sensor_gc5035_cis_get_max_digital_gain(struct v4l2_subdev *subdev, u32 *max_dgain)
{
	int ret = 0;
	struct is_cis *cis;
	struct i2c_client *client;
	cis_shared_data *cis_data;

#ifdef DEBUG_SENSOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);
	FIMC_BUG(!max_dgain);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);

	FIMC_BUG(!cis);
	FIMC_BUG(!cis->cis_data);

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	cis_data = cis->cis_data;

	cis_data->max_digital_gain[0] = SENSOR_GC5035_MAX_DIGITAL_GAIN_SET_VALUE;
	cis_data->max_digital_gain[1] = sensor_cis_calc_dgain_permile(cis_data->max_digital_gain[0]);

	*max_dgain = cis_data->max_digital_gain[1];

	dbg_sensor(2, "[%s] code %d, permile %d\n", __func__,
		cis_data->max_digital_gain[0], cis_data->max_digital_gain[1]);

#ifdef DEBUG_SENSOR_TIME
	do_gettimeofday(&end);
	dbg_sensor(2, "[%s] time %lu us\n", __func__, (end.tv_sec - st.tv_sec)*1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	return ret;
}

int sensor_gc5035_cis_wait_streamoff(struct v4l2_subdev *subdev)
{
	int ret = 0;
	u32 poll_time_ms = 0;
	struct is_cis *cis;
	struct i2c_client *client;
	cis_shared_data *cis_data;

	FIMC_BUG(!subdev);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);
	FIMC_BUG(!cis);

	cis_data = cis->cis_data;
	FIMC_BUG(!cis_data);

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		return ret;
	}

	I2C_MUTEX_LOCK(cis->i2c_lock);
	/* Checking stream off */
	do {
		u8 read_value_3E = 0;
		u8 read_value_2A = 0;

		/* Page Selection */
		ret = is_sensor_addr8_write8(client, 0xfd, 0x00);
		if (ret < 0)
			 goto p_err;

		ret = is_sensor_addr8_write8(client, 0xfe, 0x00);
		if (ret < 0)
			 goto p_err;

		/* Sensor stream off */
		ret = is_sensor_addr8_read8(client, 0x3e, &read_value_3E);
		if (ret < 0) {
			err("i2c transfer fail addr(%x) ret = %d\n", 0x3e, ret);
			goto p_err;
		}

		ret = is_sensor_addr8_read8(client, 0x2A, &read_value_2A);
		if (ret < 0) {
			err("i2c transfer fail addr(%x) ret = %d\n", 0x2A, ret);
			goto p_err;
		}

#if 1
		if (read_value_3E == 0x00 && read_value_2A == 0x00)
			break;
#else
		if (read_value_3E == 0x00) {
			if (read_value_2A == 0x01) {
				warn("%s: sensor is not stream off yet!  0x2A=%#x\n", __func__ , read_value_2A);
			} else if (read_value_2A == 0x00) {
				info("%s: sensor is stream off! 0x3E=%#x, 0x2A=%#x\n", __func__ , read_value_3E, read_value_2A);
				break;
			} else {
				warn("%s: stream off check value is wrong 0x2A=0x%#x\n", __func__, read_value_2A);
			}
		} else if (read_value_3E == 0x91) {
			warn("%s: sensor is not stream off yet!  0x3E=%#x\n", __func__ , read_value_3E);
		} else {
			warn("%s: stream off check value is wrong 0x3E=%#x\n", __func__, read_value_3E);
		}
#endif

		usleep_range(POLL_TIME_US, POLL_TIME_US);
		poll_time_ms += POLL_TIME_MS;
	} while (poll_time_ms < STREAM_OFF_POLL_TIME_MS);

	if (poll_time_ms < STREAM_OFF_POLL_TIME_MS)
		info("%s: finished after %d ms\n", __func__, poll_time_ms);
	else
		warn("%s: finished : polling timeout occured after %d ms\n", __func__, poll_time_ms);

p_err:
	I2C_MUTEX_UNLOCK(cis->i2c_lock);

	return ret;
}

int sensor_gc5035_cis_wait_streamon(struct v4l2_subdev *subdev)
{
	int ret = 0;

#if 0
	u32 poll_time_ms = 0;
	struct is_cis *cis;
	struct i2c_client *client;
	cis_shared_data *cis_data;

	FIMC_BUG(!subdev);

	cis = (struct is_cis *)v4l2_get_subdevdata(subdev);
	if (unlikely(!cis)) {
		err("cis is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	cis_data = cis->cis_data;
	if (unlikely(!cis_data)) {
		err("cis_data is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	client = cis->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	do {
		u8 read_value_3E = 0;
		u8 read_value_2A = 0;

		/* Page Selection */
		ret = is_sensor_addr8_write8(client, 0xfd, 0x00);
		if (ret < 0)
			 goto p_err;

		ret = is_sensor_addr8_write8(client, 0xfe, 0x00);
		if (ret < 0)
			 goto p_err;

		/* Sensor stream on*/
		ret = is_sensor_addr8_read8(client, 0x3e, &read_value_3E);
		if (ret < 0) {
			err("i2c transfer fail addr(%x) ret = %d\n", 0x3e, ret);
			goto p_err;
		}

		/* Sensor stream on */
		ret = is_sensor_addr8_read8(client, 0x2A, &read_value_2A);
		if (ret < 0) {
			err("i2c transfer fail addr(%x) ret = %d\n", 0x2A, ret);
			goto p_err;
		}

		if (read_value_3E == 0x00) {
			warn("%s: sensor is not stream on yet! 0x3E=%#x\n", __func__, read_value_3E);
		} else if (read_value_3E == 0x91) {
			info("%s: sensor is stream on! 0x3E=%#x, 0x2A=%#x\n", __func__, read_value_3E, read_value_2A);
			break;
		} else {
			warn("%s: stream register vaule is wrong 0x3E=%#x\n", __func__, read_value_3E);
		}

		usleep_range(POLL_TIME_US, POLL_TIME_US);
		poll_time_ms += POLL_TIME_MS;
	} while (poll_time_ms < STREAM_ON_POLL_TIME_MS);

	if (poll_time_ms < STREAM_ON_POLL_TIME_MS)
		info("%s: finished after %d ms\n", __func__, poll_time_ms);
	else
		warn("%s: finished : polling timeout occured after %d ms\n", __func__, poll_time_ms);

p_err:
#endif

	return ret;
}

static struct is_cis_ops cis_ops = {
	.cis_init = sensor_gc5035_cis_init,
	.cis_log_status = sensor_gc5035_cis_log_status,
	.cis_group_param_hold = sensor_gc5035_cis_group_param_hold,
	.cis_set_global_setting = sensor_gc5035_cis_set_global_setting,
	.cis_mode_change = sensor_gc5035_cis_mode_change,
	.cis_set_size = sensor_gc5035_cis_set_size,
	.cis_stream_on = sensor_gc5035_cis_stream_on,
	.cis_stream_off = sensor_gc5035_cis_stream_off,
	.cis_set_exposure_time = sensor_gc5035_set_exposure_time,
	.cis_get_min_exposure_time = sensor_gc5035_cis_get_min_exposure_time,
	.cis_get_max_exposure_time = sensor_gc5035_cis_get_max_exposure_time,
	.cis_adjust_frame_duration = sensor_gc5035_cis_adjust_frame_duration,
	.cis_set_frame_duration = sensor_gc5035_cis_set_frame_duration,
	.cis_set_frame_rate = sensor_gc5035_cis_set_frame_rate,
	.cis_adjust_analog_gain = sensor_gc5035_cis_adjust_analog_gain,
	.cis_set_analog_gain = sensor_gc5035_set_analog_gain,
	.cis_get_analog_gain = sensor_gc5035_cis_get_analog_gain,
	.cis_get_min_analog_gain = sensor_gc5035_cis_get_min_analog_gain,
	.cis_get_max_analog_gain = sensor_gc5035_cis_get_max_analog_gain,
	.cis_set_digital_gain = sensor_gc5035_set_digital_gain,
	.cis_get_digital_gain = sensor_gc5035_cis_get_digital_gain,
	.cis_get_min_digital_gain = sensor_gc5035_cis_get_min_digital_gain,
	.cis_get_max_digital_gain = sensor_gc5035_cis_get_max_digital_gain,
	.cis_compensate_gain_for_extremely_br = sensor_cis_compensate_gain_for_extremely_br,
	.cis_wait_streamoff = sensor_gc5035_cis_wait_streamoff,
	.cis_wait_streamon = sensor_gc5035_cis_wait_streamon,
	.cis_set_initial_exposure = sensor_cis_set_initial_exposure,
};

int cis_gc5035_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;
	struct is_core *core = NULL;
	struct v4l2_subdev *subdev_cis = NULL;
	struct is_cis *cis = NULL;
	struct is_device_sensor *device = NULL;
	struct is_device_sensor_peri *sensor_peri = NULL;
	struct device *dev;
	struct device_node *dnode;
	u32 sensor_id = 0;
	char const *setfile;

	FIMC_BUG(!client);
	FIMC_BUG(!is_dev);

	core = (struct is_core *)dev_get_drvdata(is_dev);
	if (!core) {
		probe_info("core device is not yet probed");
		return -EPROBE_DEFER;
	}

	dev = &client->dev;
	dnode = dev->of_node;

	ret = of_property_read_u32(dnode, "id", &sensor_id);
	if (ret) {
		err("sensor_id read is fail(%d)", ret);
		goto p_err;
	}

	probe_info("%s sensor_id %d\n", __func__, sensor_id);

	device = &core->sensor[sensor_id];

	sensor_peri = find_peri_by_cis_id(device, SENSOR_NAME_GC5035);
	if (!sensor_peri) {
		probe_info("sensor peri is not yet probed");
		return -EPROBE_DEFER;
	}

	cis = &sensor_peri->cis;
	if (!cis) {
		err("cis is NULL");
		ret = -ENOMEM;
		goto p_err;
	}

	subdev_cis = kzalloc(sizeof(struct v4l2_subdev), GFP_KERNEL);
	if (!subdev_cis) {
		probe_err("subdev_cis is NULL");
		ret = -ENOMEM;
		goto p_err;
	}
	sensor_peri->subdev_cis = subdev_cis;

	cis->id = SENSOR_NAME_GC5035;
	cis->subdev = subdev_cis;
	cis->device = sensor_id;
	cis->client = client;
	sensor_peri->module->client = cis->client;

	cis->ctrl_delay = N_PLUS_TWO_FRAME;

	cis->cis_data = kzalloc(sizeof(cis_shared_data), GFP_KERNEL);
	if (!cis->cis_data) {
		err("cis_data is NULL");
		ret = -ENOMEM;
		goto p_err;
	}

	cis->cis_ops = &cis_ops;

	/* belows are depend on sensor cis. MUST check sensor spec */
	cis->bayer_order = OTF_INPUT_ORDER_BAYER_RG_GB;

	probe_info("%s bayer_order %d\n", __func__, cis->bayer_order);

	if (of_property_read_bool(dnode, "sensor_f_number")) {
		ret = of_property_read_u32(dnode, "sensor_f_number", &cis->aperture_num);
		if (ret) {
			warn("f-number read is fail(%d)",ret);
		}
	} else {
		cis->aperture_num = F2_2;
	}

	probe_info("%s f-number %d\n", __func__, cis->aperture_num);

	cis->use_dgain = false;
	cis->hdr_ctrl_by_again = false;

	cis->use_initial_ae = of_property_read_bool(dnode, "use_initial_ae");
	probe_info("%s use initial_ae(%d)\n", __func__, cis->use_initial_ae);

	ret = of_property_read_string(dnode, "setfile", &setfile);
	if (ret) {
		err("setfile index read fail(%d), take default setfile!!", ret);
		setfile = "default";
	}

	if (strcmp(setfile, "default") == 0 || strcmp(setfile, "setA") == 0) {
		probe_info("%s setfile_A\n", __func__);
		sensor_gc5035_global = sensor_gc5035_setfile_A_Global;
		sensor_gc5035_global_size = ARRAY_SIZE(sensor_gc5035_setfile_A_Global);
		sensor_gc5035_setfiles = sensor_gc5035_setfiles_A;
		sensor_gc5035_setfile_sizes = sensor_gc5035_setfile_A_sizes;
		sensor_gc5035_pllinfos = sensor_gc5035_pllinfos_A;
		sensor_gc5035_max_setfile_num = ARRAY_SIZE(sensor_gc5035_setfiles_A);
		sensor_gc5035_fsync_master = sensor_gc5035_setfile_A_Fsync_Master;
		sensor_gc5035_fsync_master_size = ARRAY_SIZE(sensor_gc5035_setfile_A_Fsync_Master);
		sensor_gc5035_fsync_slave = sensor_gc5035_setfile_A_Fsync_Slave;
		sensor_gc5035_fsync_slave_size = ARRAY_SIZE(sensor_gc5035_setfile_A_Fsync_Slave);
		sensor_gc5035_dpc_init_setting = sensor_gc5035_setfile_A_Otp_Read_Initial_Setting;
		sensor_gc5035_dpc_init_setting_size = ARRAY_SIZE(sensor_gc5035_setfile_A_Otp_Read_Initial_Setting);
		sensor_gc5035_dpc_function_enable = sensor_gc5035_setfile_A_DPC_Function_Enable;
		sensor_gc5035_dpc_function_enable_size = ARRAY_SIZE(sensor_gc5035_setfile_A_DPC_Function_Enable);
	} else if (strcmp(setfile, "setB") == 0) {
		probe_info("%s setfile_B\n", __func__);
		sensor_gc5035_global = sensor_gc5035_setfile_B_Global;
		sensor_gc5035_global_size = ARRAY_SIZE(sensor_gc5035_setfile_B_Global);
		sensor_gc5035_setfiles = sensor_gc5035_setfiles_B;
		sensor_gc5035_setfile_sizes = sensor_gc5035_setfile_B_sizes;
		sensor_gc5035_pllinfos = sensor_gc5035_pllinfos_B;
		sensor_gc5035_max_setfile_num = ARRAY_SIZE(sensor_gc5035_setfiles_B);
		sensor_gc5035_fsync_master = sensor_gc5035_setfile_B_Fsync_Master;
		sensor_gc5035_fsync_master_size = ARRAY_SIZE(sensor_gc5035_setfile_B_Fsync_Master);
		sensor_gc5035_fsync_slave = sensor_gc5035_setfile_B_Fsync_Slave;
		sensor_gc5035_fsync_slave_size = ARRAY_SIZE(sensor_gc5035_setfile_B_Fsync_Slave);
		sensor_gc5035_dpc_init_setting = sensor_gc5035_setfile_B_Otp_Read_Initial_Setting;
		sensor_gc5035_dpc_init_setting_size = ARRAY_SIZE(sensor_gc5035_setfile_B_Otp_Read_Initial_Setting);
		sensor_gc5035_dpc_function_enable = sensor_gc5035_setfile_B_DPC_Function_Enable;
		sensor_gc5035_dpc_function_enable_size = ARRAY_SIZE(sensor_gc5035_setfile_B_DPC_Function_Enable);
		sensor_gc5035_image_direction_setting = sensor_gc5035_setfile_B_Image_Direction_Setting;
		sensor_gc5035_image_direction_setting_size = ARRAY_SIZE(sensor_gc5035_setfile_B_Image_Direction_Setting);
		/* setfile B is for B_First Bayer Order so update*/
		probe_info("%s update bayer order to B_FIRST\n", __func__);
		cis->bayer_order = OTF_INPUT_ORDER_BAYER_BG_GR;
	} else {
		err("%s setfile index out of bound, take default (setfile_A)", __func__);
		sensor_gc5035_global = sensor_gc5035_setfile_A_Global;
		sensor_gc5035_global_size = ARRAY_SIZE(sensor_gc5035_setfile_A_Global);
		sensor_gc5035_setfiles = sensor_gc5035_setfiles_A;
		sensor_gc5035_setfile_sizes = sensor_gc5035_setfile_A_sizes;
		sensor_gc5035_pllinfos = sensor_gc5035_pllinfos_A;
		sensor_gc5035_max_setfile_num = ARRAY_SIZE(sensor_gc5035_setfiles_A);
		sensor_gc5035_fsync_master = sensor_gc5035_setfile_A_Fsync_Master;
		sensor_gc5035_fsync_master_size = ARRAY_SIZE(sensor_gc5035_setfile_A_Fsync_Master);
		sensor_gc5035_fsync_slave = sensor_gc5035_setfile_A_Fsync_Slave;
		sensor_gc5035_fsync_slave_size = ARRAY_SIZE(sensor_gc5035_setfile_A_Fsync_Slave);
		sensor_gc5035_dpc_init_setting = sensor_gc5035_setfile_A_Otp_Read_Initial_Setting;
		sensor_gc5035_dpc_init_setting_size = ARRAY_SIZE(sensor_gc5035_setfile_A_Otp_Read_Initial_Setting);
		sensor_gc5035_dpc_function_enable = sensor_gc5035_setfile_A_DPC_Function_Enable;
		sensor_gc5035_dpc_function_enable_size = ARRAY_SIZE(sensor_gc5035_setfile_A_DPC_Function_Enable);
	}

	v4l2_i2c_subdev_init(subdev_cis, client, &subdev_ops);
	v4l2_set_subdevdata(subdev_cis, cis);
	v4l2_set_subdev_hostdata(subdev_cis, device);
	snprintf(subdev_cis->name, V4L2_SUBDEV_NAME_SIZE, "cis-subdev.%d", cis->id);

	probe_info("%s done\n", __func__);

p_err:
	return ret;
}

static const struct of_device_id sensor_cis_gc5035_match[] = {
	{
		.compatible = "samsung,exynos-is-cis-gc5035",
	},
	{
		.compatible = "samsung,exynos-is-cis-gc5035-macro",
	},
};
MODULE_DEVICE_TABLE(of, sensor_cis_gc5035_match);

static const struct i2c_device_id sensor_cis_gc5035_idt[] = {
	{ SENSOR_NAME, 0 },
	{},
};

static struct i2c_driver sensor_cis_gc5035_driver = {
	.probe	= cis_gc5035_probe,
	.driver = {
		.name	= SENSOR_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = sensor_cis_gc5035_match,
		.suppress_bind_attrs = true,
	},
	.id_table = sensor_cis_gc5035_idt,
};

static int __init sensor_cis_gc5035_init(void)
{
	int ret;

	ret = i2c_add_driver(&sensor_cis_gc5035_driver);
	if (ret)
		err("failed to add %s driver: %d\n",
			sensor_cis_gc5035_driver.driver.name, ret);

	return ret;
}
late_initcall_sync(sensor_cis_gc5035_init);
