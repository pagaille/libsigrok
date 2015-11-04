/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Daniel Elstner <daniel.kitta@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSIGROK_HARDWARE_SYSCLK_LWLA_LWLA_H
#define LIBSIGROK_HARDWARE_SYSCLK_LWLA_LWLA_H

#include <stdint.h>
#include <libusb.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>

struct sr_usb_dev_inst;

/* Rotate argument n bits to the left.
 * This construct is an idiom recognized by GCC as bit rotation.
 */
#define LROTATE(a, n) (((a) << (n)) | ((a) >> (CHAR_BIT * sizeof(a) - (n))))

/* Convert 16-bit little endian LWLA protocol word to machine word order. */
#define LWLA_TO_UINT16(val) GUINT16_FROM_LE(val)

/* Convert 32-bit mixed endian LWLA protocol word to machine word order. */
#define LWLA_TO_UINT32(val) LROTATE(GUINT32_FROM_LE(val), 16)

/* Convert 16-bit argument to LWLA protocol word. */
#define LWLA_WORD(val) GUINT16_TO_LE(val)

/* Extract 16-bit units in mixed endian order from 32/64-bit value. */
#define LWLA_WORD_0(val) GUINT16_TO_LE(((val) >> 16) & 0xFFFF)
#define LWLA_WORD_1(val) GUINT16_TO_LE((val) & 0xFFFF)
#define LWLA_WORD_2(val) GUINT16_TO_LE(((val) >> 48) & 0xFFFF)
#define LWLA_WORD_3(val) GUINT16_TO_LE(((val) >> 32) & 0xFFFF)

/** USB device end points.
 */
enum {
	EP_COMMAND   = 2,
	EP_BITSTREAM = 4,
	EP_REPLY     = 6 | LIBUSB_ENDPOINT_IN
};

/** LWLA protocol command ID codes.
 */
enum {
	CMD_READ_REG	= 1,
	CMD_WRITE_REG	= 2,
	CMD_READ_MEM	= 6,
	CMD_CAP_SETUP	= 7,
	CMD_CAP_STATUS	= 8,
};

/** LWLA capture state flags.
 */
enum {
	STATUS_CAPTURING = 1 << 1,
	STATUS_TRIGGERED = 1 << 4,
	STATUS_MEM_AVAIL = 1 << 5,
	STATUS_FLAG_MASK = 0x3F
};

/** LWLA1034 register addresses.
 */
enum {
	REG_MEM_CTRL    = 0x1074, /* capture buffer control */
	REG_MEM_FILL    = 0x1078, /* capture buffer fill level */
	REG_MEM_START   = 0x107C, /* capture buffer start address */

	REG_DIV_BYPASS  = 0x1094, /* bypass clock divider flag */

	REG_LONG_STROBE = 0x10B0, /* long register read/write strobe */
	REG_LONG_ADDR   = 0x10B4, /* long register address */
	REG_LONG_LOW    = 0x10B8, /* long register low word */
	REG_LONG_HIGH   = 0x10BC, /* long register high word */

	REG_FREQ_CH1    = 0x10C0, /* channel 1 live frequency */
	REG_FREQ_CH2    = 0x10C4, /* channel 2 live frequency */
	REG_FREQ_CH3    = 0x10C8, /* channel 3 live frequency */
	REG_FREQ_CH4    = 0x10CC, /* channel 4 live frequency */
};

/** Flag bits for REG_MEM_CTRL.
 */
enum {
	MEM_CTRL_WRITE   = 1 << 0, /* "wr1rd0" bit */
	MEM_CTRL_CLR_IDX = 1 << 1, /* "clr_idx" bit */
};

/* LWLA1034 long register addresses.
 */
enum {
	LREG_CAP_CTRL = 10,  /* capture control bits */
	LREG_TEST_ID  = 100, /* constant test ID */
};

/** Flag bits for LREG_CAP_CTRL.
 */
enum {
	CAP_CTRL_TRG_EN       = 1 << 0, /* "trg_en" bit */
	CAP_CTRL_CLR_TIMEBASE = 1 << 2, /* "do_clr_timebase" bit */
	CAP_CTRL_FLUSH_FIFO   = 1 << 4, /* "flush_fifo" bit */
	CAP_CTRL_CLR_FIFOFULL = 1 << 5, /* "clr_fifo32_ful" bit */
	CAP_CTRL_CLR_COUNTER  = 1 << 6, /* "clr_cntr0" bit */
};

/** Register/value pair.
 */
struct regval_pair {
	unsigned int reg;
	unsigned int val;
};

SR_PRIV int lwla_send_bitstream(struct sr_context *ctx,
				const struct sr_usb_dev_inst *usb,
				const char *name);

SR_PRIV int lwla_send_command(const struct sr_usb_dev_inst *usb,
			      const uint16_t *command, int cmd_len);

SR_PRIV int lwla_receive_reply(const struct sr_usb_dev_inst *usb,
			       uint32_t *reply, int reply_len, int expect_len);

SR_PRIV int lwla_read_reg(const struct sr_usb_dev_inst *usb,
			  uint16_t reg, uint32_t *value);

SR_PRIV int lwla_read_long_reg(const struct sr_usb_dev_inst *usb,
			       uint32_t addr, uint64_t *value);

SR_PRIV int lwla_write_reg(const struct sr_usb_dev_inst *usb,
			   uint16_t reg, uint32_t value);

SR_PRIV int lwla_write_regs(const struct sr_usb_dev_inst *usb,
			    const struct regval_pair *regvals, int count);

#endif
