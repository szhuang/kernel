/*
 * A V4L2 driver for OmniVision OV5647 cameras.
 * Copyright (C) 2018 YeaCreate Co., Ltd.
 * Simon Chen <simon@yeacreate.com>
 *
 * Notice: ov5648 maybe runs unstable in some platforms at 420Mbps/lane model
 * so, there are 2 models you can chose in this code.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
//#include <linux/init.h>  //leave for "TODO"
//#include <linux/io.h>		//leave for "TODO"
//#include <linux/of_graph.h> //leave for "TODO"
//#include <linux/slab.h>	//leave for "TODO"
//#include <linux/videodev2.h> //leave for "TODO"
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
//#include <media/media-entity.h> //leave for "TODO"
//#include <media/v4l2-common.h> //leave for "TODO"
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
//#include <media/v4l2-ioctl.h> //leave for "TODO"
//#include <media/v4l2-event.h> //leave for "TODO"
//#include <media/v4l2-device.h> //leave for "TODO"
//#include <media/v4l2-fwnode.h> //leave for "TODO"
//#include <media/v4l2-image-sizes.h> //leave for "TODO"
//#include <media/v4l2-mediabus.h> //leave for "TODO"
//#include <media/v4l2-of.h> //leave for "TODO"


#define _DEBUG_   //debut switch
//#define _210M_	//for 210Mbps mipi per lane
#define _420M_		//for 420Mbps mipi per lane
#define SENSOR_NAME "OV5648"
#define CHIP_ID	0x5648
#define REG_NULL 0xFFFF   //
#define OV5648_SW_RESET	0x0103  //software reset
#define OV5648_SW_STANDBY 0x0100 //software standby(0)/streaming(1)
#define OV5648_REG_CHIPID	0x300A
#define OV5648_REG_GAIN	0x350A
#define OV5648_REG_LINE	0x3500
#define OV5648_REG_VTS		0x380e
#define OV5648_VTS_MAX			0x7fff
#define OV5648_EXPOSURE_MIN	0x000000
#define OV5648_EXPOSURE_MAX	0x0fffff
#define OV5648_EXPOSURE_STEP	0x01
#define OV5648_EXPOSURE_DEFAULT	0x001000
#define OV5648_ANALOG_GAIN_MIN	0x0000
#define OV5648_ANALOG_GAIN_MAX	0x03ff
#define OV5648_ANALOG_GAIN_STEP	0x01
#define OV5648_ANALOG_GAIN_DEFAULT 0x020
/*
https://lkml.org/lkml/2017/2/22/676
> The Mbps-per-lane value would then be link_freq * 2, and then
> Mbps-per-lane could be used to lookup the correct "hsfreqsel"
> value to program the D-PHY

https://www.kernel.org/doc/html/v4.10/media/kapi/csi2.html
pixel rate = link frequency * 2bits * lanes / BITS_PER_SAMPLE

at some docs, is that:
pixel_rate = mode->vts_def * mode->hts_def * mode->max_fps;
*/
#ifdef _420M_
#define OV5648_PIXEL_RATE_84MHZ 	84000000   // for 420Mbps/lane
#define OV5648_LINK_FREQ_210MHZ		210000000  // for 420Mbps/lane
#endif

#ifdef _210M_
#define OV5648_PIXEL_RATE_42MHZ 	42000000   // for 210Mbps/lane
#define OV5648_LINK_FREQ_105MHZ		105000000  // for 210Mbps/lane
#endif

#define OV5648_XVCLK_FREQ			24000000
#define OV5648_REG_VALUE_08BIT		1
#define OV5648_REG_VALUE_16BIT		2
#define OV5648_REG_VALUE_24BIT		3

static const char * const ov5648_supply_names[] = {
	"dvp",		/*main supply*/
	"dovdd",	/* Digital I/O power */
	"avdd",		/* Analog power */
	"dvdd",		/* Digital core power */
	};

#define OV5648_NUM_SUPPLIES ARRAY_SIZE(ov5648_supply_names)

struct regval {
	u16 addr;
	u8 val;
	};

struct ov5648_mode {
	u32 width;
	u32 height;
	u32 max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
	};

struct ov5648 {
	struct i2c_client	*client;
	struct clk			*xvclk;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV5648_NUM_SUPPLIES];
	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct mutex		mutex;
	bool			streaming;
	const struct ov5648_mode *cur_mode;
	};

#define to_ov5648(sd) container_of(sd, struct ov5648, subdev)

#ifdef _420M_
/*
 * 2592x1944 15fps 2 lane MIPI 420Mbps/lane
 */
static const struct regval ov5648_global_regs[] = {
		{OV5648_SW_RESET,0x01},		//software reset
		{OV5648_SW_STANDBY,0x00},	// Software Standy
		{OV5648_SW_STANDBY,0x00},	// Software Standy
		{OV5648_SW_STANDBY,0x00},	// Software Standy
		{OV5648_SW_STANDBY,0x00},	// Software Standy
		{OV5648_SW_STANDBY,0x00},	// Software Standy
		{OV5648_SW_STANDBY,0x00},	// Software Standy
		{OV5648_SW_STANDBY,0x00},	// Software Standy
		{0x3001,0x00},
		{0x3002,0x00},
		{0x3011,0x02},
	//{0x3016,0x08},	//sc_comm_mipi_phy  mipi_pad_enable
		{0x3017,0x05},	//sc_comm_mipi_phy  ??
		{0x3018,0x4c},	// MIPI 2 lane
		{0x301c,0xd2},	//?? 	OTP
		{0x3022,0x00},
		{0x3034,0x1a},	// 10-bit mode
		{0x3035,0x21},
		{0x3036,0x69},	/* MCLK=24MHz MIPI=420MBs*2 Lanes FPS=30 */
		{0x3037,0x03},
		{0x3038,0x00},
		{0x3039,0x00},
		{0x303a,0x00},
		{0x303b,0x19},
		{0x303c,0x11},
		{0x303d,0x30},
		{0x3105,0x11},
		{0x3106,0x05},
		{0x3304,0x28},
		{0x3305,0x41},
		{0x3306,0x30},
		{0x3308,0x00},
		{0x3309,0xc8},
		{0x330a,0x01},
		{0x330b,0x90},
		{0x330c,0x02},
		{0x330d,0x58},
		{0x330e,0x03},
		{0x330f,0x20},
		{0x3300,0x00},
		{0x3500,0x00},	// exposure [19:16]
		{0x3501,0x3d},	// exposure [15:8]
		{0x3502,0x00},	// exposure [7:0], exposure = 0x3d0 = 976
		{0x3503,0x00},	/* exposure set to auto */
		{0x350a,0x00},	// gain[9:8]
		{0x350b,0x20},	// gain[7:0], gain = 2x
		{0x3601,0x33},	// analog control
		{0x3602,0x00},	// analog control
		{0x3611,0x0e},	// analog control
		{0x3612,0x2b},	// analog control
		{0x3614,0x50},	// analog control
		{0x3620,0x33},	// analog control
		{0x3622,0x00},	// analog control
		{0x3630,0xad},	// analog control
		{0x3631,0x00},	// analog control
		{0x3632,0x94},	// analog control
		{0x3633,0x17},	// analog control
		{0x3634,0x14},	// analog control
		{0x3704,0xc0},	// analog control
		{0x3705,0x2a},	// analog control
		{0x3708,0x66},	// analog control
		{0x3709,0x52},	// analog control
		{0x370b,0x23},	// analog control
		{0x370c,0xc3},	// analog control
		{0x370d,0x00},	// analog control
		{0x370e,0x00},	// analog control
		{0x371c,0x07},	// analog control
		{0x3739,0xd2},	// analog control
		{0x373c,0x00},
		{0x3800,0x00},	// xstart = 0
		{0x3801,0x00},	// xstart
		{0x3802,0x00},	// ystart = 0
		{0x3803,0x00},	// ystart
		{0x3804,0x0a},	// xend = 2623
		{0x3805,0x3f},	// yend
		{0x3806,0x07},	// yend = 1955
		{0x3807,0xa3},	// yend
		{0x3808,0x05},	/* x output size = 1296 */
		{0x3809,0x10},	/* x output size */
		{0x380a,0x03},	/* y output size = 972 */
		{0x380b,0xcc},	/* y output size  */
		{0x380c,0x0b},	/* hts = 2861 */
		{0x380d,0x00},	/* hts */
		{0x380e,0x03},	/* vts = 992  30fps */
		{0x380f,0xe0},	/* vts*/
		{0x3810,0x00},	// isp x win = 8
		{0x3811,0x08},	// isp x win
		{0x3812,0x00},	// isp y win = 4
		{0x3813,0x04},	// isp y win
		{0x3814,0x31},	// x inc
		{0x3815,0x31},	// y inc
		{0x3817,0x00},	// hsync start
		{0x3820,0x08},	// flip off, v bin off
		{0x3821,0x07},	// mirror on, h bin on
		{0x3826,0x03},
		{0x3829,0x00},
		{0x382b,0x0b},
		{0x3830,0x00},
		{0x3836,0x00},
		{0x3837,0x00},
		{0x3838,0x00},
		{0x3839,0x04},
		{0x383a,0x00},
		{0x383b,0x01},
		{0x3b00,0x00},	// strobe off
		{0x3b02,0x08},	// shutter delay
		{0x3b03,0x00},	// shutter delay
		{0x3b04,0x04},	// frex_exp
		{0x3b05,0x00},	// frex_exp
		{0x3b06,0x04},
		{0x3b07,0x08},	// frex inv
		{0x3b08,0x00},	// frex exp req
		{0x3b09,0x02},	// frex end option
		{0x3b0a,0x04},	// frex rst length
		{0x3b0b,0x00},	// frex strobe width
		{0x3b0c,0x3d},	// frex strobe width
		{0x3f01,0x0d},
		{0x3f0f,0xf5},
		{0x4000,0x89},	// blc enable
		{0x4001,0x02},	// blc start line
		{0x4002,0x45},	// blc auto, reset frame number = 5
		{0x4004,0x02},	// black line number
		{0x4005,0x18},	// blc normal freeze
		{0x4006,0x08},
		{0x4007,0x10},
		{0x4008,0x00},
		{0x4300,0xf8},
		{0x4303,0xff},
		{0x4304,0x00},
		{0x4307,0xff},
		{0x4520,0x00},
		{0x4521,0x00},
		{0x4511,0x22},
	//{0x4800,0x14}, // MIPI line sync enable ??
		{0x4801,0x0f},	//mipi ctrl 01
		{0x4814,0x2a},	//mipi ctrl 4 virtual channel of mipi /default 0x2a //**??,与4801联动
	//{0x481b,0x3c},	//hs trail min
		{0x481f,0x3c},	// MIPI clk prepare min
		{0x4823,0x3c},	//clk trail min
		{0x4826,0x00},	// MIPI hs prepare min
		{0x4827,0x32},	//hs prepare min
		{0x4837,0x18},	// MIPI global timing
		{0x4b00,0x06},
		{0x4b01,0x0a},
		{0x4b04,0x10},	//SPI rk ctrl 04
		{0x5000,0xff},	// bpc on, wpc on
		{0x5001,0x01},	// awb enable
		{0x5002,0x41},	// win enable, awb gain enable
		{0x5003,0x0a},	// buf en, bin auto en
		{0x5004,0x00},	// size man off
		{0x5043,0x00},
		{0x5013,0x00},
		{0x501f,0x03},	// ISP output data
		{0x503d,0x00},	//test pattern off
		{0x5180,0x00},	// auto wb gain on
		{0x5a00,0x08},
		{0x5b00,0x01},
		{0x5b01,0x40},
		{0x5b02,0x00},
		{0x5b03,0xf0},
		{0x4837,0x17}, // MIPI global timing
		{OV5648_SW_STANDBY,0x01},	// wake up from software sleep
		{REG_NULL,0x00}	/* end of the list ??*/
	};

/*
 * Xclk 24Mhz
 * linelength(hst) 2816(0x0b00)
 * framelength(vst) 1984(0x07c0)
 * grabwindow_width 2592
 * grabwindow_height 1944
 * max_framerate 15fps
 * 2592x1944 15fps 2 lane MIPI 420Mbps/lane
 */
static const struct regval ov5648_2592x1944_regs[] = {
		{OV5648_SW_STANDBY,0x00},//software standby
		{0x3501,0x7b}, // exposure 0x007b00
		{0x3502,0x00},	// exposure
		{0x3708,0x63},
		{0x3709,0x12},
		{0x370c,0xcc},  // changed by AM05d ??
		{0x3800,0x00}, // xstart = 0
		{0x3801,0x00}, // xstart
		{0x3802,0x00}, // ystart = 0
		{0x3803,0x00}, // ystart
		{0x3804,0x0a}, // xend = 2623
		{0x3805,0x3f}, // xend
		{0x3806,0x07}, // yend = 1955
		{0x3807,0xa3}, // yend
		{0x3808,0x0a}, // x output size = 2592
		{0x3809,0x20}, // x output size
		{0x380a,0x07}, // y output size = 1944
		{0x380b,0x98}, // y output size
		{0x380c,0x0b}, // hts = 2816
		{0x380d,0x00}, // hts
		{0x380e,0x07}, // vts = 1984  15 fps
		{0x380f,0xc0}, // vts		   15 fps
		{0x3810,0x00}, // isp x win = 16
		{0x3811,0x10}, // isp x win
		{0x3812,0x00}, // isp y win = 6
		{0x3813,0x06}, // isp y win
		{0x3814,0x11}, // x inc
		{0x3815,0x11}, // y inc
		{0x3817,0x00}, // hsync start
		{0x3820,0x40}, // flip off, v bin off
		{0x3821,0x06}, // mirror on, v bin off
		{0x4004,0x04}, // black line number
		{0x4005,0x1a}, // blc always update
		{0x350b,0x20}, // gain = 2x
		{0x4837,0x17}, // MIPI global timing
		{OV5648_SW_STANDBY,0x01},  //wake up, streaming
		{REG_NULL,0x00}	/* end of the list */
	};
/*
 * Xclk 24Mhz
 * linelength(hst) 2554(0x09fa)
 * framelength(vst) 1521(0x05f1)
 * grabwindow_width 1920
 * grabwindow_height 1080
 * max_framerate 30fps
 * 1920x1080 30fps 2 lane MIPI 420Mbps/lane
 */
static const struct regval ov5648_1920x1080_regs[] = {
		{OV5648_SW_STANDBY,0x00},// software standby
		{0x3501,0x3d},// exposure 0x003d00
		{0x3502,0x00},// exposure
		{0x3708, 0x63},
		{0x3709, 0x12},
		{0x370c, 0xc0},
		{0x3800, 0x01},// xstart = 336
		{0x3801, 0x50},// x start
		{0x3802, 0x01},// y start = 434
		{0x3803, 0xb2},// y start
		{0x3804, 0x08},// xend = 2287
		{0x3805, 0xef},// xend
		{0x3806, 0x05},// yend = 1521
		{0x3807, 0xf1},// yend
		{0x3808, 0x07},// x output size = 1920
		{0x3809, 0x80},// x output size
		{0x380a, 0x04},// y output size = 1080
		{0x380b, 0x38},// y output size
		{0x380c, 0x09},/* hts = 2554 */
		{0x380d, 0xfa},/* hts*/
		{0x380e, 0x04},//vst= 1120,30 fps
		{0x380f, 0x60},//vst= 1120,30 fps
		{0x3810, 0x00},// isp x win = 16
		{0x3811, 0x10},// isp x win
		{0x3812, 0x00},// isp y win = 4
		{0x3813, 0x04},// isp y win
		{0x3814, 0x11},// x inc
		{0x3815, 0x11},// y inc
		{0x3817,0x00},// hsync start
		{0x3820,0x08},// flip off, v bin off
		{0x3821,0x07},// mirror on, h bin on
		{0x4004,0x02},// black line number
		{0x4005,0x18},// blc level trigger
		{0x350b,0x20},// gain = 2x
		{0x4837,0x17},// MIPI global timing
		{OV5648_SW_STANDBY,0x01},// wake up from software sleep
		{REG_NULL,0x00}	/* end of the list */
	};

/*
 * Xclk 24Mhz
 * linelength(hst) 2816(0x0b00)
 * framelength(vst) 992(0x03e0)
 * grabwindow_width 1296
 * grabwindow_height 972
 * max_framerate 30fps
 * 1296x972 30fps 2 lane MIPI 420Mbps/lane
 */
static const struct regval ov5648_1296x972_regs[] = {
		{OV5648_SW_STANDBY,0x00},// software standby
		{0x3501,0x3d},	// exposure 0x003d00
		{0x3502,0x00},	// exposure
		{0x3708,0x66},
		{0x3709,0x52},
		{0x370c,0xcf},
		{0x3800,0x00},	// xstart = 0
		{0x3801,0x00},	// x start
		{0x3802,0x00},	// y start = 0
		{0x3803,0x00},	// y start
		{0x3804,0x0a},	// xend = 2623
		{0x3805,0x3f},	// xend
		{0x3806,0x07},	// yend = 1955
		{0x3807,0xa3},	// yend
		{0x3808,0x05},	// x output size = 1296
		{0x3809,0x10},	// x output size
		{0x380a,0x03},	// y output size = 972
		{0x380b,0xcc},	// y output size
		{0x380c,0x0b},	/* hts = 2816 */
		{0x380d,0x00},	/* hts*/
		{0x380e,0x03},	//vst= 992,30 fps
		{0x380f,0xe0},	//vst= 992 30 fps
		{0x3810,0x00},	// isp x win = 8
		{0x3811,0x08},	// isp x win
		{0x3812,0x00},	// isp y win = 4
		{0x3813,0x04},	// isp y win
		{0x3814,0x31},	// x inc
		{0x3815,0x31},	// y inc
		{0x3817,0x00},	// hsync start
		{0x3820,0x08},	// flip off, v bin off
		{0x3821,0x07},	// mirror on, h bin on
		{0x4004,0x02},	// black line number
		{0x4005,0x18},	// blc level trigger
		{0x350b,0x20},	// gain = 2x
		{0x4837,0x17},	// MIPI global timing
		{OV5648_SW_STANDBY,0x01},	// wake up from software sleep
		{REG_NULL,0x00}	/* end of the list */
	};

/*
 * Xclk 24Mhz
 * linelength(hst) 1912(0x0778)
 * framelength(vst) 1496(0x05d8)
 * grabwindow_width 1280
 * grabwindow_height 720
 * max_framerate 30fps
 * 1280x720 30fps 2 lane MIPI 420Mbps/lane
 */
static const struct regval ov5648_1280x720_regs[] = {
	/* to 1280x720P30 */
		{OV5648_SW_STANDBY,0x00},// software standby
		{0x3501,0x3d},// exposure 0x003d00
		{0x3502,0x00},// exposure
		{0x3708,0x66},
		{0x3709,0x52},
		{0x370c,0xc3},
		{0x3800,0x00},// xstart = 16
		{0x3801,0x10},// x start
		{0x3802,0x00},// y start = 254
		{0x3803,0xfe},// y start
		{0x3804,0x0a},// xend = 2623
		{0x3805,0x3f},// xend
		{0x3806,0x07},// yend = 1955
		{0x3807,0xa3},// yend
		{0x3808,0x05},// x output size = 1280
		{0x3809,0x00},// x output size
		{0x380a,0x02},// y output size = 720
		{0x380b,0xd0},// y output size
		{0x380c,0x07},/* hts = 1912 */
		{0x380d,0x78},/* hts*/
		{0x380e,0x05},//vst= 1496,30 fps
		{0x380f,0xd8},//vst= 1496,30 fps
		{0x3810,0x00},// isp x win = 8
		{0x3811,0x08},// isp x win
		{0x3812,0x00},// isp y win = 4
		{0x3813,0x02},// isp y win
		{0x3814,0x31},// x inc
		{0x3815,0x31},// y inc
		{0x3817,0x00},// hsync start
		{0x3820,0x08},// flip off, v bin off
		{0x3821,0x07},// mirror on, h bin on
		{0x4004,0x02},// black line number
		{0x4005,0x1a},// blc level trigger
		{0x350b,0x20},// gain = 2x
		{0x4837,0x17},// MIPI global timing
		{OV5648_SW_STANDBY,0x01},	// wake up from software sleep
		{REG_NULL,0x00}	/* end of the list */
	};
/*
 * Xclk 24Mhz
 * linelength(hst) 1832(0x0728)
 * framelength(vst) 508(0x01fc)
 * grabwindow_width 640
 * grabwindow_height 480
 * max_framerate 90fps
 * 640x480 90fps 2 lane MIPI 420Mbps/lane
 */
static const struct regval ov5648_640x480_regs[] = {
	// 640x480 90fps 2 lane MIPI 420Mbps/lane
		{OV5648_SW_STANDBY,0x00},// software standby
		{0x3501,0x3d},	// exposure 0x003d00
		{0x3502,0x00},	// exposure
		{0x3709, 0x92},
		{0x370c, 0xc3},
		{0x3800, 0x00}, //xstart = 0
		{0x3801, 0x00}, //xstart
		{0x3802, 0x00}, //ystart = 2
		{0x3803, 0x02}, //ystart
		{0x3804, 0x0a}, //xend = 2623
		{0x3805, 0x3f}, //xend
		{0x3806, 0x07}, //yend = 1953
		{0x3807, 0xa1}, //yend
		{0x3808, 0x02}, //x output size = 640
		{0x3809, 0x80}, //x output size
		{0x380a, 0x01}, //y output size = 480
		{0x380b, 0xe0}, //y output size
		{0x380c, 0x07}, //hts = 1832
		{0x380d, 0x28}, //hts
		{0x380e, 0x01}, //vts = 508
		{0x380f, 0xfc}, //vts
		{0x3810, 0x00}, //isp x win = 8
		{0x3811, 0x08}, //isp x win
		{0x3812, 0x00}, //isp y win = 4
		{0x3813, 0x04}, //isp y win
		{0x3814, 0x71}, //x inc
		{0x3815, 0x53}, //y inc
		{0x3817, 0x00}, //hsync start
		{0x3820, 0x08}, //flip off, v bin off
		{0x3821, 0x07}, //mirror on, h bin on
		{0x4004,0x02},	// black line number
		{0x4005,0x18},	// blc level trigger
		{0x350b,0x20},	// gain = 2x
		{0x4837,0x17},	// MIPI global timing
		{OV5648_SW_STANDBY,0x01},	// wake up from software sleep
		{REG_NULL,0x00}	/* end of the list */
	};
#endif

#ifdef _210M_
/*
 * 1296_972 20fps 2 lane MIPI 210Mbps/lane
 */
static const struct regval ov5648_global_regs[] = {
		{OV5648_SW_RESET,0x01},		//software reset
		{OV5648_SW_STANDBY,0x00},	// Software Standy
		{OV5648_SW_STANDBY,0x00},	// Software Standy
		{OV5648_SW_STANDBY,0x00},	// Software Standy
		{OV5648_SW_STANDBY,0x00},	// Software Standy
		{OV5648_SW_STANDBY,0x00},	// Software Standy
		{OV5648_SW_STANDBY,0x00},	// Software Standy
		{OV5648_SW_STANDBY,0x00},	// Software Standy
		{OV5648_SW_STANDBY,0x00},	// Software Standy
		{0x3001,0x00},	// D[7:0] set to input
		{0x3002,0x00},	// D[11:8] set to input
		{0x3011,0x02},	// Drive strength 2x
	//{0x3016,0x08},	//sc_comm_mipi_phy  mipi_pad_enable ??
		{0x3017,0x05},	//sc_comm_mipi_phy  ??
		{0x3018,0x4c},	// MIPI 2 lane
	//{0x301c,0xd2},	//?? OTP
		{0x3022,0x00},   //??
		{0x3034,0x1a},	// 10-bit mode
		{0x3035,0x41},
		{0x3036,0x69}, /* MCLK=24MHz MIPI=210MBs*2 Lanes FPS=30 */
		{0x3037,0x03},
		{0x3038,0x00},
		{0x3039,0x00},
		{0x303a,0x00},
		{0x303b,0x19},
		{0x303c,0x11},
		{0x303d,0x30},
		{0x3105,0x11},
		{0x3106,0x05},
		{0x3304,0x28},
		{0x3305,0x41},
		{0x3306,0x30},
		{0x3308,0x00},
		{0x3309,0xc8},
		{0x330a,0x01},
		{0x330b,0x90},
		{0x330c,0x02},
		{0x330d,0x58},
		{0x330e,0x03},
		{0x330f,0x20},
		{0x3300,0x00},
		{0x3500,0x00}, // exposure [19:16]
		{0x3501,0x7b}, // exposure [15:8]
		{0x3502,0x00}, // exposure [7:0], exposure = 0x3d0 = 976
		{0x3503,0x00}, /* exposure set to auto */
	//{0x350a,0x00}, 	// gain[9:8]
	//{0x350b,0x40}, // gain[7:0], gain = 4x
		{0x3601,0x33},
		{0x3602,0x00},
		{0x3611,0x0e},
		{0x3612,0x2b},
		{0x3614,0x50},
		{0x3620,0x33},
		{0x3622,0x00},
		{0x3630,0xad},
		{0x3631,0x00},
		{0x3632,0x94},
		{0x3633,0x17},
		{0x3634,0x14},
		{0x3704,0xc0},
		{0x3705,0x2a},
		{0x3708,0x63},
		{0x3709,0x12},
		{0x370b,0x23},
		{0x370c,0xcc},  // was cf,others c3 ??
		{0x370d,0x00},
		{0x370e,0x00},
		{0x371c,0x07},
		{0x3739,0xd2},
		{0x373c,0x00},
		{0x3800,0x00},// xstart = 0
		{0x3801,0x00},// xstart
		{0x3802,0x00},// ystart = 0
		{0x3803,0x00},	// ystart
		{0x3804,0x0a},// xend = 2623
		{0x3805,0x3f},// yend
		{0x3806,0x07},// yend = 1955
		{0x3807,0xa3},// yend
		{0x3808,0x0a}, // x output size = 2592
		{0x3809,0x20}, // x output size
		{0x380a,0x07}, // y output size = 1944
		{0x380b,0x98}, // y output size
		{0x380c,0x0b},		/* hts = 2816 */
		{0x380d,0x00},/* hts */
		{0x380e,0x07}, 	// vts = 1988 7.5 fps
		{0x380f,0xc4}, 	// vts
		{0x3810,0x00}, // isp x win = 16
		{0x3811,0x10}, // isp x win
		{0x3812,0x00}, // isp y win = 6
		{0x3813,0x06}, // isp y win
		{0x3814,0x11}, // x inc
		{0x3815,0x11}, // y inc
		{0x3817,0x00},// hsync start
		{0x3820,0x40},// flip off, v bin off
		{0x3821,0x06},// mirror on, h bin on
		{0x3826,0x03},
		{0x3829,0x00},
		{0x382b,0x0b},
		{0x3830,0x00},
		{0x3836,0x00},
		{0x3837,0x00},
		{0x3838,0x00},
		{0x3839,0x04},
		{0x383a,0x00},
		{0x383b,0x01},
		{0x3b00,0x00},	// strobe off
		{0x3b02,0x08},	// shutter delay
		{0x3b03,0x00},	// shutter delay
		{0x3b04,0x04},	// frex_exp
		{0x3b05,0x00},	// frex_exp
		{0x3b06,0x04},
		{0x3b07,0x08},	// frex inv
		{0x3b08,0x00},	// frex exp req
		{0x3b09,0x02},	// frex end option
		{0x3b0a,0x04},	// frex rst length
		{0x3b0b,0x00},	// frex strobe width
		{0x3b0c,0x3d},	// frex strobe width
		{0x3f01,0x0d},
		{0x3f0f,0xf5},
		{0x4000,0x89},	// blc enable
		{0x4001,0x02},	// blc start line
		{0x4002,0x45},	// blc auto, reset frame number = 5
		{0x4004,0x04},	// black line number
		{0x4005,0x1a},	// blc normal freeze
		{0x4006,0x08},
		{0x4007,0x10},
		{0x4008,0x00},
		{0x4050,0x6E},  // ??
		{0x4051,0x8F}, //??
		{0x4300,0xf8},
		{0x4303,0xff},
		{0x4304,0x00},
		{0x4307,0xff},
		{0x4520,0x00},
		{0x4521,0x00},
		{0x4511,0x22},
	//{0x4800,0x14}, // MIPI line sync enable
		{0x4801,0x0f}, //??
		{0x4814,0x2a}, //??
		{0x481f,0x3c}, // MIPI clk prepare min
		{0x4823,0x3c}, //??
		{0x4826,0x00}, // MIPI hs prepare min
		{0x481b,0x3c}, //??
		{0x4827,0x32}, //??
		{0x4837,0x18}, // MIPI global timing
		{0x4b00,0x06},
		{0x4b01,0x0a},
		{0x4b04,0x10},	//??SPI rk ctrl 04
		{0x5000,0xff},	// bpc on, wpc on
		{0x5001,0x01},	// awb enable
		{0x5002,0x41},	// win enable, awb gain enable
		{0x5003,0x0a},	// buf en, bin auto en
		{0x5004,0x00},	// size man off
		{0x5043,0x00},
		{0x5013,0x00},
		{0x501f,0x03}, // ISP output data
		{0x503d,0x00},//test pattern off
		{0x5180,0x00}, // auto wb gain on
		{0x5a00,0x08},
		{0x5b00,0x01},
		{0x5b01,0x40},
		{0x5b02,0x00},
		{0x5b03,0xf0},
		{0x5781,0x1F}, //??
		{0x5782,0x03}, //??
		{0x5786,0x20}, //??
		{0x5787,0x40}, //??
		{0x5788,0x08}, //??
		{0x5789,0x08}, //??
		{0x578A,0x02}, //??
		{0x578B,0x01}, //??
		{0x578C,0x01}, //??
		{0x578D,0x0C}, //??
		{0x578E,0x02}, //??
		{0x578F,0x01}, //??
		{0x5790,0x01}, //??
		{0x301a, 0xf0}, //debug model
		{0x4837, 0x17}, // MIPI global timing
		{OV5648_SW_STANDBY,0x01}, // wake up from software sleep
		{REG_NULL,0x00}
	};

/*
 * Xclk 24Mhz
 * linelength(hst) 2816(0x0b00)
 * framelength(vst) 1988(0x07c4)
 * grabwindow_width 2592
 * grabwindow_height 1944
 * max_framerate 7fps
 * 2592x1944 7.5fps 2 lane MIPI 210Mbps/lane
 */
static const struct regval ov5648_2592x1944_regs[] = {
	//@@ rockchip 2592x1944 7.5fps 2 lane MIPI 210Mbps/lane
		{OV5648_SW_STANDBY,0x00},//software standby
//	{0x3034,0x1a},
		{0x3035,0x41},
		{0x3036,0x69},
		{0x3501,0x7b},
		{0x3502,0x00},
		{0x3708,0x63},
		{0x3709,0x12},
		{0x370c,0xcc},  // changed by AM05d ??
		{0x3800,0x00}, // xstart = 0
		{0x3801,0x00}, // xstart
		{0x3802,0x00},  // ystart = 0
		{0x3803,0x00}, // ystart
		{0x3804,0x0a}, // xend = 2623
		{0x3805,0x3f}, // xend
		{0x3806,0x07}, // yend = 1955
		{0x3807,0xa3}, // yend
		{0x3808,0x0a}, // x output size = 2592
		{0x3809,0x20}, // x output size
		{0x380a,0x07}, // y output size = 1944
		{0x380b,0x98}, // y output size
		{0x380c,0x0b},  // hts = 2816
		{0x380d,0x00}, 	// hts
		{0x380e,0x07}, 	// vts = 1988 7.5 fps
		{0x380f,0xc4}, 	// vts
		{0x3810,0x00}, // isp x win = 16
		{0x3811,0x10}, // isp x win
		{0x3812,0x00}, // isp y win = 6
		{0x3813,0x06}, // isp y win
		{0x3814,0x11}, // x inc
		{0x3815,0x11}, // y inc
		{0x3817,0x00}, // hsync start
		{0x3820,0x40}, // flip off, v bin off
		{0x3821,0x06}, // mirror on, v bin off
		{0x4004,0x04}, // black line number
		{0x4005,0x1a}, // blc always update
	//{0x350b,0x40}, // gain = 4x
		{0x4837,0x17},  // MIPI global timing
		{OV5648_SW_STANDBY,0x01},  //wake up, streaming
		{REG_NULL,0x00}	/* end of the list */
	};

/*
 * Xclk 24Mhz
 * linelength(hst) 2816(0x0b00)
 * framelength(vst) 992(0x03e0)
 * grabwindow_width 1296
 * grabwindow_height 972
 * max_framerate 15fps
 * 1296x972 20fps 2 lane MIPI 210Mbps/lane
 */
static const struct regval ov5648_1296x972_regs[] = {
//@@ rockchip 1296x972 20fps 2 lane MIPI 210Mbps/lane
		{OV5648_SW_STANDBY,0x00},// software standby
	//{0x3034,0x18},
		{0x3035,0x41},
		{0x3036,0x69},
		{0x3501,0x3d}, // exposure 0x003d00
		{0x3502,0x00}, // exposure
		{0x3708,0x66},
		{0x3709,0x52},
		{0x370c,0xcf},
		{0x3800,0x00},	 // xstart = 0
		{0x3801,0x00},	 	// x start
		{0x3802,0x00},	 // y start = 0
		{0x3803,0x00},	  	// y start
		{0x3804,0x0a},	  // xend = 2623
		{0x3805,0x3f},	  // xend
		{0x3806,0x07},	  // yend = 1955
		{0x3807,0xa3},	  // yend
		{0x3808,0x05},	 	// x output size = 1296
		{0x3809,0x10},	  // x output size
		{0x380a,0x03},	  // y output size = 972
		{0x380b,0xcc},	  // y output size
//	{0x380c,0x0a},//0B
//	{0x380d,0x56},//00
		{0x380c,0x0b},//0B hts = 2816 */
		{0x380d,0x00},//00 hts*/
		{0x380e,0x03},//03 vst= 992,20 fps
		{0x380f,0xe0},//E0 vst= 992 20 fps
		{0x3810,0x00},// isp x win = 8
		{0x3811,0x08},	// isp x win
		{0x3812,0x00},	// isp y win = 4
		{0x3813,0x04},	 // isp y win
		{0x3814,0x31},	  // x inc
		{0x3815,0x31},	  // y inc
		{0x3817,0x00},	  // hsync start
		{0x3820,0x08},	  // flip off, v bin off
		{0x3821,0x07},	  // mirror on, h bin on
		{0x4004,0x02},	  // black line number
		{0x4005,0x18},	  // blc level trigger
	//{0x350b,0x80},
		{0x4837,0x17},	  // MIPI global timing
		{OV5648_SW_STANDBY,0x01},	// wake up from software sleep
		{REG_NULL,0x00}	/* end of the list */
	};
#endif

static const struct ov5648_mode supported_modes[] = {
#ifdef _420M_
		{
		.width = 1296,
		.height = 972,
		.max_fps = 30,
		.exp_def = 0x3d00,
		.hts_def = 0x0b00, //2816
		.vts_def = 0x03e0, //992
		.reg_list = ov5648_1296x972_regs,
		},
		{
		.width = 640,
		.height = 480,
		.max_fps = 90,
		.exp_def = 0x3d00,
		.hts_def = 0x0728, //1832
		.vts_def = 0x01fc, //508
		.reg_list = ov5648_640x480_regs,
		},
		{
		.width = 1280,
		.height = 720,
		.max_fps = 30,
		.exp_def = 0x3d00,
		.hts_def = 0x0778, //1912
		.vts_def = 0x05d8, //1496
		.reg_list = ov5648_1280x720_regs,
		},
		{
		.width = 1920,
		.height = 1080,
		.max_fps = 30,
		.exp_def = 0x3d00,
		.hts_def = 0x09fa, //2554
		.vts_def = 0x05f1, //1521
		.reg_list = ov5648_1920x1080_regs,
		},
		{
		.width = 2592,
		.height = 1944,
		.max_fps = 15,
		.exp_def = 0x7b00,
		.hts_def = 0x0b00, //2816
		.vts_def = 0x07c0, //1984
		.reg_list = ov5648_2592x1944_regs,
		},
#endif

#ifdef _210M_
		{
		.width = 2592,
		.height = 1944,
		.max_fps = 7,
		.exp_def = 0x7b00,
		.hts_def = 0x0b00,
		.vts_def = 0x07c4,
		.reg_list = ov5648_2592x1944_regs,
		},
		{
		.width = 1296,
		.height = 972,
		.max_fps = 15,
		.exp_def = 0x3d00,
		.hts_def = 0x0b00,
		.vts_def = 0x03e0,
		.reg_list = ov5648_1296x972_regs,
		},
#endif
	};

static const s64 link_freq_menu_items[] = {
#ifdef _420M_
	OV5648_LINK_FREQ_210MHZ
#endif

#ifdef _210M_
	OV5648_LINK_FREQ_105MHZ
#endif
	};


/* Write registers up to 4 at a time */
static int ov5648_write_reg(struct i2c_client *client, u16 reg,
                            u32 len, u32 val) {
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

#ifdef _DEBUG_
	printk("ov5648_write_reg \n");
#endif

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2) {
#ifdef _DEBUG_
		printk("ov5648_write_reg false 1 \n");
#endif
		return -EIO;
		}

	return 0;
	}

static int ov5648_write_array(struct i2c_client *client,
                              const struct regval *regs) {
	u32 i;
	int ret = 0;
#ifdef _DEBUG_
	printk("ov5648_write_array \n");
#endif
	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = ov5648_write_reg(client, regs[i].addr,
		                       OV5648_REG_VALUE_08BIT, regs[i].val);

	return ret;
	}

/* Read registers up to 4 at a time */
static int ov5648_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
                           u32 *val) {
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret;
#ifdef _DEBUG_
	printk("ov5648_read_reg \n");
#endif
	if (len > 4 || !len) {
#ifdef _DEBUG_
		printk("ov5648_read_reg false #1 \n");
#endif
		return -EINVAL;
		}

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
#ifdef _DEBUG_
		printk("ov5648_read_reg false #2 \n");
#endif
		return -EIO;
		}
	*val = be32_to_cpu(data_be);

	return 0;
	}

static int ov5648_get_reso_dist(const struct ov5648_mode *mode,
                                struct v4l2_mbus_framefmt *framefmt) {
#ifdef _DEBUG_
	printk("ov5648_get_reso_dist \n");
	printk("ov5648_get_reso_dist  mode->width=%d \n",mode->width);
	printk("ov5648_get_reso_dist  framefmt->width=%d \n",framefmt->width);
	printk("ov5648_get_reso_dist  mode->height=%d \n",mode->height);
	printk("ov5648_get_reso_dist  framefmt->height->width=%d \n",framefmt->height);
#endif
	return abs(mode->width - framefmt->width) + abs(mode->height - framefmt->height);
	}

static const struct ov5648_mode *ov5648_find_best_fit(struct v4l2_subdev_format *fmt) {
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	int i;

#ifdef _DEBUG_
	printk("ov5648_mode ov5648_find_best_fit \n");
	printk("ov5648_mode ov5648_find_best_fit  ARRAY_SIZE(supported_modes) = %d \n",ARRAY_SIZE(supported_modes));
#endif

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = ov5648_get_reso_dist(&supported_modes[i], framefmt);
#ifdef _DEBUG_
		printk("ov5648_mode ov5648_find_best_fit supported_modes[i]=%d , dist=%d \n",i,dist);
#endif
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
			}
		}
#ifdef _DEBUG_
	printk("ov5648_mode ov5648_find_best_fit cur_best_fit=%d \n",cur_best_fit);
#endif
	return &supported_modes[cur_best_fit];
	}

static int ov5648_set_fmt(struct v4l2_subdev *sd,
                          struct v4l2_subdev_pad_config *cfg,
                          struct v4l2_subdev_format *fmt) {
	struct ov5648 *ov5648 = to_ov5648(sd);
	const struct ov5648_mode *mode;
	s64 h_blank, vblank_def;

#ifdef _DEBUG_
	printk("ov5648_set_fmt \n");
#endif
	mutex_lock(&ov5648->mutex);

	mode = ov5648_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef _DEBUG_
		printk("ov5648_set_fmt V4L2_SUBDEV_FORMAT_TRY \n");
#endif
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov5648->mutex);
		return -ENOTTY;
#endif
		}
	else {
#ifdef _DEBUG_
		printk("ov5648_set_fmt V4L2_SUBDEV_FORMAT_TRY else \n");
#endif
		ov5648->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ov5648->hblank,h_blank,h_blank, 1, h_blank);

		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov5648->vblank,
		                         vblank_def,
		                         OV5648_VTS_MAX - mode->height,
		                         1,
		                         vblank_def);
		}

	mutex_unlock(&ov5648->mutex);

	return 0;
	}

static int ov5648_get_fmt(struct v4l2_subdev *sd,
                          struct v4l2_subdev_pad_config *cfg,
                          struct v4l2_subdev_format *fmt) {
	struct ov5648 *ov5648 = to_ov5648(sd);
	const struct ov5648_mode *mode = ov5648->cur_mode;

#ifdef _DEBUG_
	printk("ov5648_get_fmt \n");
#endif
	mutex_lock(&ov5648->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov5648->mutex);
		return -ENOTTY;
#endif
		}
	else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
		}
	mutex_unlock(&ov5648->mutex);

	return 0;
	}

static int ov5648_enum_mbus_code(struct v4l2_subdev *sd,
                                 struct v4l2_subdev_pad_config *cfg,
                                 struct v4l2_subdev_mbus_code_enum *code) {
#ifdef _DEBUG_
	printk("ov5648_enum_mbus_code \n");
#endif
	if (code->index != 0) {
#ifdef _DEBUG_
		printk("ov5648_enum_mbus_code index error \n");
#endif
		return -EINVAL;
		}
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
	}

static int ov5648_enum_frame_sizes(struct v4l2_subdev *sd,
                                   struct v4l2_subdev_pad_config *cfg,
                                   struct v4l2_subdev_frame_size_enum *fse) {
#ifdef _DEBUG_
	printk("ov5648_enum_frame_sizes \n");
#endif
	if (fse->index >= ARRAY_SIZE(supported_modes)) {
#ifdef _DEBUG_
		printk("ov5648_enum_frame_sizes supported_modes flase \n");
#endif
		return -EINVAL;
		}

	if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10) {
#ifdef _DEBUG_
		printk("ov5648_enum_frame_sizes MEDIA_BUS_FMT_SBGGR10_1X10 flase \n");
#endif
		return -EINVAL;
		}

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
	}


static int __ov5648_start_stream(struct ov5648 *ov5648) {
	int ret;
#ifdef _DEBUG_
	printk("ov5648_start_stream \n");
#endif
#ifdef _DEBUG_
	printk("ov5648_start_stream write ov5648_global_regs start \n");
#endif
	ret = ov5648_write_array(ov5648->client, ov5648_global_regs);
	if (ret) {
#ifdef _DEBUG_
		printk("ov5648_start_stream write ov5648_global_regs error \n");
#endif
		return ret;
		}
#ifdef _DEBUG_
	printk("ov5648_start_stream write ov5648_global_regs end \n");
#endif

#ifdef _DEBUG_
	printk("ov5648_start_stream write reg_list start \n");
#endif
	ret = ov5648_write_array(ov5648->client, ov5648->cur_mode->reg_list);
	if (ret) {
#ifdef _DEBUG_
		printk("ov5648_start_stream write reg_list error \n");
#endif
		return ret;
		}
#ifdef _DEBUG_
	printk("ov5648_start_stream write reg_list start \n");
#endif

	/* In case these controls are set before streaming */
	mutex_unlock(&ov5648->mutex);
	ret = v4l2_ctrl_handler_setup(&ov5648->ctrl_handler);
	mutex_lock(&ov5648->mutex);
	if (ret) {
#ifdef _DEBUG_
		printk("ov5648_start_stream error \n");
#endif
		return ret;
		}
#ifdef _DEBUG_
	printk("ov5648_start_stream update 0x4800 to 0x14 \n");
#endif
	ret=ov5648_write_reg(ov5648->client,0x4800,OV5648_REG_VALUE_08BIT, 0x14);

#ifdef _DEBUG_
	printk("ov5648_start_stream OV5648_SW_STANDBY set to 1 \n");
#endif
	ret=ov5648_write_reg(ov5648->client,OV5648_SW_STANDBY,OV5648_REG_VALUE_08BIT, 0x01);
	//msleep(25);
	return 0;
	}

static int __ov5648_stop_stream(struct ov5648 *ov5648) {
	int ret=0;
#ifdef _DEBUG_
	printk("__ov5648_stop_stream \n");
#endif

#ifdef _DEBUG_
	printk("__ov5648_stop_stream update 0x4800 to 0x25 \n");
#endif
	ret=ov5648_write_reg(ov5648->client,0x4800,OV5648_REG_VALUE_08BIT, 0x25);

#ifdef _DEBUG_
	printk("__ov5648_stop_stream OV5648_SW_STANDBY set to 0 \n");
#endif
	ret=ov5648_write_reg(ov5648->client, OV5648_SW_STANDBY,OV5648_REG_VALUE_08BIT, 0x00);
	msleep(25);
	return 0;
	}

static int ov5648_s_stream(struct v4l2_subdev *sd, int on) {
	struct ov5648 *ov5648 = to_ov5648(sd);
	struct i2c_client *client = ov5648->client;
	int ret = 0;
#ifdef _DEBUG_
	printk("ov5648_s_stream \n");
#endif
	mutex_lock(&ov5648->mutex);
	on = !!on;
	if (on == ov5648->streaming) {
#ifdef _DEBUG_
		printk("ov5648_s_stream on streaming \n");
#endif
		goto unlock_and_return;
		}
	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
#ifdef _DEBUG_
			printk("ov5648_s_stream can not get sync \n");
#endif
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
			}

		ret = __ov5648_start_stream(ov5648);
		if (ret) {
#ifdef _DEBUG_
			printk("ov5648_s_stream start stream failed while write regs \n");
#endif
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
			}
		}
	else {
		__ov5648_stop_stream(ov5648);
		pm_runtime_put(&client->dev);
		}

	ov5648->streaming = on;

unlock_and_return:
	mutex_unlock(&ov5648->mutex);

	return ret;
	}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ov5648_cal_delay(u32 cycles) {
	return DIV_ROUND_UP(cycles, OV5648_XVCLK_FREQ / 1000 / 1000);
	}


static int __ov5648_power_on(struct ov5648 *ov5648) {
	int ret;
	u32 delay_us;
	struct device *dev = &ov5648->client->dev;
	delay_us = ov5648_cal_delay(8192);

#ifdef _DEBUG_
	printk("__ov5648_power_on\n");
#endif

	ret = clk_prepare_enable(ov5648->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
		}
#ifdef _DEBUG_
	else {
		printk("5648 xvclk enabled!\n");
		}
#endif

	usleep_range(delay_us, delay_us * 2);

	ret = regulator_bulk_enable(OV5648_NUM_SUPPLIES, ov5648->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		//goto disable_clk;
		}
#ifdef _DEBUG_
	else {
		printk("5648 regulator_bulk_enable \n");
		}
#endif
	usleep_range(delay_us, delay_us * 2);


	if (!IS_ERR(ov5648->pwdn_gpio)) {
		gpiod_set_value_cansleep(ov5648->pwdn_gpio, 1);
#ifdef _DEBUG_
		printk("5648 pwdn_gpio set to 1 \n");
#endif
		}
#ifdef _DEBUG_
	else {
		printk("5648 pwdn_gpio set to 1 flase \n");
		}
#endif

	usleep_range(delay_us, delay_us * 10);

//disable_clk:
//	clk_disable_unprepare(ov5648->xvclk);

	return ret;
	}



static void __ov5648_power_off(struct ov5648 *ov5648) {

#ifdef _DEBUG_
	printk("__ov5648_power_off \n");
#endif
	if (!IS_ERR(ov5648->pwdn_gpio)) {
#ifdef _DEBUG_
		printk("5648 pwdn down \n");
#endif
		gpiod_set_value_cansleep(ov5648->pwdn_gpio, 0);
		}
	clk_disable_unprepare(ov5648->xvclk);
#ifdef _DEBUG_
	printk("5648 xvclk disabled \n");
#endif
	regulator_bulk_disable(OV5648_NUM_SUPPLIES, ov5648->supplies);
#ifdef _DEBUG_
	printk("5648 disabled power \n");
#endif
	}


static int ov5648_runtime_resume(struct device *dev) {
	int ret=0;
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5648 *ov5648 = to_ov5648(sd);

#ifdef _DEBUG_
	printk("ov5648_runtime_resume \n");
	printk("ov5648 sent ov5648_runtime_resume disable standby to SCCB(streaming) \n");
#endif
	ret = ov5648_write_reg(client, OV5648_SW_STANDBY,OV5648_REG_VALUE_08BIT, 0x01);

	return __ov5648_power_on(ov5648);
	}

static int ov5648_runtime_suspend(struct device *dev) {
	int ret=0;
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5648 *ov5648 = to_ov5648(sd);

#ifdef _DEBUG_
	printk("ov5648_runtime_suspend \n");
	printk("ov5648 sent standby to SCCB(suspended) \n");
#endif
	ret = ov5648_write_reg(client, OV5648_SW_STANDBY,OV5648_REG_VALUE_08BIT, 0x00);
	__ov5648_power_off(ov5648);
	return 0;
	}

static int ov5648_check_sensor_id(struct ov5648 *ov5648,struct i2c_client *client) {
	struct device *dev = &ov5648->client->dev;
	u32 id = 0,delay_us;
	int ret;
	delay_us = ov5648_cal_delay(8192);
#ifdef _DEBUG_
	printk("ov5648_check_sensor_id \n");
	printk("ov5648 sw_reset sent \n");
#endif
	ret = ov5648_write_reg(client, OV5648_SW_RESET,OV5648_REG_VALUE_08BIT, 0x01);
	usleep_range(delay_us, delay_us * 2);
	ret = ov5648_read_reg(client, OV5648_REG_CHIPID,OV5648_REG_VALUE_16BIT, &id);
#ifdef _DEBUG_
	printk("ov5648 check senor id %04x \n", id);
#endif
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%04x), ret(%d)\n", id, ret);
		return ret;
		}
	dev_info(dev, "Detected %s sensor\n", SENSOR_NAME);

	return 0;
	}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov5648_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh) {
	struct ov5648 *ov5648 = to_ov5648(sd);
	struct v4l2_mbus_framefmt *try_fmt =v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov5648_mode *def_mode = &supported_modes[0];
#ifdef _DEBUG_
	printk("5648 ov5648_open");
#endif
	mutex_lock(&ov5648->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&ov5648->mutex);
	/* No crop or compose */

	return 0;
	}
#endif

static const struct dev_pm_ops ov5648_pm_ops = {
	SET_RUNTIME_PM_OPS(ov5648_runtime_suspend,ov5648_runtime_resume, NULL)
	};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov5648_internal_ops = {
	.open = ov5648_open,
	};
#endif

static const struct v4l2_subdev_video_ops ov5648_video_ops = {
	.s_stream = ov5648_s_stream,
	};

static const struct v4l2_subdev_pad_ops ov5648_pad_ops = {
	.enum_mbus_code = ov5648_enum_mbus_code,
	.enum_frame_size = ov5648_enum_frame_sizes,
	.get_fmt = ov5648_get_fmt,
	.set_fmt = ov5648_set_fmt,
	};

static const struct v4l2_subdev_ops ov5648_subdev_ops = {
	.video	= &ov5648_video_ops,
	.pad	= &ov5648_pad_ops,
	};

static int ov5648_set_ctrl(struct v4l2_ctrl *ctrl) {
	struct ov5648 *ov5648 = container_of(ctrl->handler,struct ov5648, ctrl_handler);
	struct i2c_client *client = ov5648->client;
	s64 max;
	int ret = 0;
#ifdef _DEBUG_
	u32 id = 0;
	printk("ov5648_set_ctrl \n");
#endif
	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
		case V4L2_CID_VBLANK:
			/* Update max exposure while meeting expected vblanking */
#ifdef _DEBUG_
			printk("ov5648_set_ctrl V4L2_CID_VBLANK #1 Update max exposure while meeting expected vblanking \n");
#endif
			max = ov5648->cur_mode->height + ctrl->val - 4;
			__v4l2_ctrl_modify_range(ov5648->exposure,
			                         ov5648->exposure->minimum,
			                         max,
			                         ov5648->exposure->step,
			                         ov5648->exposure->default_value);
			break;
		}

	if (pm_runtime_get(&client->dev) <= 0) {
#ifdef _DEBUG_
		printk("ov5648_set_ctrl pm_runtime_get_if_in_use \n");
#endif
		return 0;
		}

	switch (ctrl->id) {
		case V4L2_CID_EXPOSURE:
#ifdef _DEBUG_
			printk("ov5648_set_ctrl update exposure\n");
#endif
			ret = ov5648_write_reg(client, OV5648_REG_LINE,OV5648_REG_VALUE_24BIT,ctrl->val);
			/*tmp*/
#ifdef _DEBUG_
			printk("5648 ov5648_set_ctrl is %06x \n",ctrl->val);
			ret = ov5648_read_reg(client, 0x3500,OV5648_REG_VALUE_08BIT, &id);
			printk("5648 ov5648_set_ctrl read OV5648_REG_LINE_H: %02x \n",id);
			ret = ov5648_read_reg(client, 0x3501,OV5648_REG_VALUE_08BIT, &id);
			printk("5648 ov5648_set_ctrl read OV5648_REG_LINE_M: %02x \n",id);
			ret = ov5648_read_reg(client, 0x3502,OV5648_REG_VALUE_08BIT, &id);
			printk("5648 ov5648_set_ctrl read OV5648_REG_LINE_L: %02x \n",id);
#endif
			/*tmp*/

			break;


		case V4L2_CID_ANALOGUE_GAIN:
			ret = ov5648_write_reg(client, OV5648_SW_STANDBY,OV5648_REG_VALUE_08BIT, 0x00);
#ifdef _DEBUG_
			printk("ov5648_set_ctrl ANALOGUE GAIN \n");
#endif
			ret = ov5648_write_reg(client, OV5648_REG_GAIN,OV5648_REG_VALUE_16BIT,ctrl->val);

			/*tmp*/
#ifdef _DEBUG_
			printk("5648 ov5648_set_ctrl is %06x \n",ctrl->val);
			ret = ov5648_read_reg(client, 0x350a,OV5648_REG_VALUE_08BIT, &id);
			printk("5648 ov5648_set_ctrl read OV5648_REG_GAIN_H: %02x \n",id);
			ret = ov5648_read_reg(client, 0x350b,OV5648_REG_VALUE_08BIT, &id);
			printk("5648 ov5648_set_ctrl read OV5648_REG_GAIN_L: %02x \n",id);
#endif
			/*tmp*/
			break;

		case V4L2_CID_VBLANK:
#ifdef _DEBUG_
			printk("ov5648_set_ctrl VBLANK \n");
#endif
			ret = ov5648_write_reg(client, OV5648_REG_VTS,OV5648_REG_VALUE_16BIT,ctrl->val+ov5648->cur_mode->height);
			/*tmp*/
#ifdef _DEBUG_
			printk("5648 ov5648_set_ctrl VBLANK is %06x \n",ctrl->val);
			ret = ov5648_read_reg(client, 0x380e,OV5648_REG_VALUE_08BIT, &id);
			printk("5648 ov5648_set_ctrl read OV5648_REG_VTS_H: %02x \n",id);
			ret = ov5648_read_reg(client, 0x380f,OV5648_REG_VALUE_08BIT, &id);
			printk("5648 ov5648_set_ctrl read OV5648_REG_VTS_L: %02x \n",id);
#endif
			/*tmp*/
			break;

		default:
			dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			         __func__, ctrl->id, ctrl->val);
			break;
		}

	pm_runtime_put(&client->dev);

	return ret;
	}

static const struct v4l2_ctrl_ops ov5648_ctrl_ops = {
	.s_ctrl = ov5648_set_ctrl,
	};

static int ov5648_initialize_controls(struct ov5648 *ov5648) {
	struct v4l2_ctrl_handler *handler;
	const struct ov5648_mode *mode;
	struct v4l2_ctrl *ctrl;
	int ret;

#ifdef _DEBUG_
	printk("ov5648_initialize_controls \n");
#endif

	handler = &ov5648->ctrl_handler;
	mode = ov5648->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8); //??
	if (ret)
		return ret;
	handler->lock = &ov5648->mutex;
	/* register standard controls */

	/* freq */
	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,0, 0, link_freq_menu_items);
	if (ctrl) ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

#ifdef _420M_
	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,0, OV5648_PIXEL_RATE_84MHZ, 1, OV5648_PIXEL_RATE_84MHZ);
#endif

#ifdef _210M_
	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,0, OV5648_PIXEL_RATE_42MHZ, 1, OV5648_PIXEL_RATE_42MHZ);
#endif
	/* blank */

	//*hblank*//
	//h_blank = mode->hts_def - mode->width;
	ov5648->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
	                                   mode->hts_def - mode->width,
	                                   mode->hts_def - mode->width,
	                                   1,
	                                   mode->hts_def - mode->width);
	if (ov5648->hblank) ov5648->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	//*vblank*//
	ov5648->vblank = v4l2_ctrl_new_std(handler,&ov5648_ctrl_ops,V4L2_CID_VBLANK,
	                                   mode->vts_def - mode->height,
	                                   OV5648_VTS_MAX - mode->height,
	                                   1,
	                                   mode->vts_def - mode->height);

	/* exposure */
	ov5648->exposure = v4l2_ctrl_new_std(handler, &ov5648_ctrl_ops,V4L2_CID_EXPOSURE,
	                                     OV5648_EXPOSURE_MIN,
	                                     OV5648_EXPOSURE_MAX,
	                                     OV5648_EXPOSURE_STEP,
	                                     mode->exp_def);
	/*ANALOG GAIN*/
	ov5648->anal_gain = v4l2_ctrl_new_std(handler, &ov5648_ctrl_ops,V4L2_CID_ANALOGUE_GAIN,
	                                      OV5648_ANALOG_GAIN_MIN,
	                                      OV5648_ANALOG_GAIN_MAX,
	                                      OV5648_ANALOG_GAIN_STEP,
	                                      OV5648_ANALOG_GAIN_DEFAULT);
	if (handler->error) {
		ret = handler->error;
		dev_err(&ov5648->client->dev,"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
		}

	ov5648->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
	}


static int ov5648_configure_regulators(struct ov5648 *ov5648) {
	int i;
#ifdef _DEBUG_
	printk("ov5648_configure_regulators \n");
#endif
	for (i = 0; i < OV5648_NUM_SUPPLIES; i++)
		ov5648->supplies[i].supply = ov5648_supply_names[i];

	return devm_regulator_bulk_get(&ov5648->client->dev,
	                               OV5648_NUM_SUPPLIES,
	                               ov5648->supplies);
	}

static int ov5648_probe(struct i2c_client *client,const struct i2c_device_id *id) {
	struct device *dev = &client->dev;
	struct ov5648 *ov5648;
	struct v4l2_subdev *sd;
	int ret;
#ifdef _DEBUG_
	printk("ov5648_probe \n");
#endif
	ov5648 = devm_kzalloc(dev, sizeof(*ov5648), GFP_KERNEL);
	if (!ov5648)
		return -ENOMEM;

	ov5648->client = client;
	ov5648->cur_mode = &supported_modes[0];

	ov5648->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov5648->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
		}
	ret = clk_set_rate(ov5648->xvclk, OV5648_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
		}
	if (clk_get_rate(ov5648->xvclk) != OV5648_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");

	ov5648->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov5648->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = ov5648_configure_regulators(ov5648);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
		}

	mutex_init(&ov5648->mutex);

	sd = &ov5648->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov5648_subdev_ops);
	ret = ov5648_initialize_controls(ov5648);
	if (ret)
		goto err_destroy_mutex;

	ret = __ov5648_power_on(ov5648);
	if (ret)
		goto err_free_handler;

	ret = ov5648_check_sensor_id(ov5648, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov5648_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov5648->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	ret = media_entity_init(&sd->entity, 1, &ov5648->pad, 0);
	if (ret < 0)
		goto err_power_off;
#endif

	ret = v4l2_async_register_subdev(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
		}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__ov5648_power_off(ov5648);
err_free_handler:
	v4l2_ctrl_handler_free(&ov5648->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ov5648->mutex);

	return ret;
	}

static int ov5648_remove(struct i2c_client *client) {
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5648 *ov5648 = to_ov5648(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ov5648->ctrl_handler);
	mutex_destroy(&ov5648->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov5648_power_off(ov5648);
	pm_runtime_set_suspended(&client->dev);

	return 0;
	}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov5648_of_match[] = {
		{ .compatible = "ovti,ov5648" },
		{},
	};
MODULE_DEVICE_TABLE(of, ov5648_of_match);
#endif

static const struct i2c_device_id ov5648_match_id[] = {
		{ "ovti,ov5648", 0 },
		{ },
	};

static struct i2c_driver ov5648_i2c_driver = {
	.driver = {
		.name = "ov5648",
		.pm = &ov5648_pm_ops,
		.of_match_table = of_match_ptr(ov5648_of_match),
		},
	.probe		= &ov5648_probe,
	.remove		= &ov5648_remove,
	.id_table	= ov5648_match_id,
	};

static int __init sensor_mod_init(void) {
#ifdef _DEBUG_
	printk("5648__init sensor_mod_init \n");
#endif
	return i2c_add_driver(&ov5648_i2c_driver);
	}

static void __exit sensor_mod_exit(void) {
#ifdef _DEBUG_
	printk("5648__exit sensor_mod_exit \n");
#endif
	i2c_del_driver(&ov5648_i2c_driver);
	}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_AUTHOR("Simon Chen <simon@yeacreate.com>");
MODULE_DESCRIPTION("A low-level driver for OmniVision ov5648 sensors");
MODULE_LICENSE("GPL v2");
