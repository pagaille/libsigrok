/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
 * Copyright (C) 2015 Matthieu Gaillet <matthieu@gaillet.be> 
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

/*
 * DTM0660 from Dream Tech International Ltd protocol parser.
 *
 * 6000 counts (5 5-6 digits)
 *
 *  - Package: QFP-64
 *  - Communication parameters: Unidirectional, 2400/8n1
 *  - Protocol looks closely to the fs9721 but with 15 bytes and reversed nibbles.
 *
 * Decoding table :
 *
 * +======+==========+=============+=========+=========+============+
 * | Byte | Bits 7-4 |    Bit 3    |  Bit 2  |  Bit 1  |   Bit 0    |
 * +======+==========+=============+=========+=========+============+
 * |    0 | 0x1      | RS232       | Auto    | DC      | AC         |
 * +------+----------+-------------+---------+---------+------------+
 * |    1 | 0x2      | 4A          | 4F      | 4E      | - (minus)  |
 * +------+----------+-------------+---------+---------+------------+
 * |    2 | 0x3      | 4B          | 4G      | 4C      | 4D         |
 * +------+----------+-------------+---------+---------+------------+
 * |    3 | 0x4      | 3A          | 3F      | 3E      | DP1        |
 * +------+----------+-------------+---------+---------+------------+
 * |    4 | 0x5      | 3B          | 3G      | 3C      | 3D         |
 * +------+----------+-------------+---------+---------+------------+
 * |    5 | 0x6      | 2A          | 2F      | 2E      | DP2        |
 * +------+----------+-------------+---------+---------+------------+
 * |    6 | 0x7      | 2B          | 2G      | 2C      | 2D         |
 * +------+----------+-------------+---------+---------+------------+
 * |    7 | 0x8      | 1A          | 1F      | 1E      | DP3        |
 * +------+----------+-------------+---------+---------+------------+
 * |    8 | 0x9      | 1B          | 1G      | 1C      | 1D         |
 * +------+----------+-------------+---------+---------+------------+
 * |    9 | 0xa      | Diode       | k       | n       | u          |
 * +------+----------+-------------+---------+---------+------------+
 * |   10 | 0xb      | Beep        | M       |  %      | m          |
 * +------+----------+-------------+---------+---------+------------+
 * |   11 | 0xc      | Hold        | Rel     | Ohms    | Farads     |
 * +------+----------+-------------+---------+---------+------------+
 * |   12 | 0xd      | Low battery | Hz      | V       | A          |
 * +------+----------+-------------+---------+---------+------------+
 * |   13 | 0xe      | c2c1_00     | c2c1_01 | Celcius | Fahrenheit |
 * +------+----------+-------------+---------+---------+------------+
 * |   14 | 0xf      | Max         | Min-Max | Min     | Auto-Off   |
 * +------+----------+-------------+---------+---------+------------+
 */

//  LCD truth table 
//  Segments denomination follows the standardised definition (see https://en.wikipedia.org/wiki/Seven-segment_display)
//  
//  +--------------------------------+
//  |Digit|A|F|E|(n/a)|B|G|C|D|Result|
//  +--------------------------------+
//  |0    |1|1|1|0    |1|0|1|1|0xeb  |
//  |1    |0|0|0|0    |1|0|1|0|0x0a  |
//  |2    |1|0|1|0    |1|1|0|1|0xad  |
//  |3    |1|0|0|0    |1|1|1|1|0x8f  |
//  |4    |0|1|0|0    |1|1|1|0|0x4e  |
//  |5    |1|1|0|0    |0|1|1|1|0xc7  |
//  |6    |1|1|1|0    |0|1|1|1|0xe7  |
//  |7    |1|0|0|0    |1|0|1|0|0x8a  |
//  |8    |1|1|1|0    |1|1|1|1|0xef  |
//  |9    |1|1|0|0    |1|1|1|1|0xcf  |
//  |L    |0|1|1|0    |0|0|0|1|0x61  |
//  +--------------------------------+


#include <config.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "dtm0660"

static int parse_digit(uint8_t b)
{
	switch (b) {
	case 0xeb:
		return 0;
	case 0x0a:
		return 1;
	case 0xad:
		return 2;
	case 0x8f:
		return 3;
	case 0x4e:
		return 4;
	case 0xc7:
		return 5;
	case 0xe7:
		return 6;
	case 0x8a:
		return 7;
	case 0xef:
		return 8;
	case 0xcf:
		return 9;
	default:
		sr_dbg("Invalid digit byte: 0x%02x.", b);
		return -1;
	}
}

static gboolean sync_nibbles_valid(const uint8_t *buf)
{
	int i;

	/* Check the synchronization nibbles, and make sure they all match. */
	for (i = 0; i < DTM0660_PACKET_SIZE; i++) {
		if (((buf[i] >> 4) & 0x0f) != (i + 1)) {
			sr_dbg("Sync nibble in byte %d (0x%02x) is invalid.",
			       i, buf[i]);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean flags_valid(const struct dtm0660_info *info)
{
	int count;

	/* Does the packet have more than one multiplier? */
	count = 0;
	count += (info->is_nano) ? 1 : 0;
	count += (info->is_micro) ? 1 : 0;
	count += (info->is_milli) ? 1 : 0;
	count += (info->is_kilo) ? 1 : 0;
	count += (info->is_mega) ? 1 : 0;
	if (count > 1) {
		sr_dbg("More than one multiplier detected in packet.");
		return FALSE;
	}

	/* Does the packet "measure" more than one type of value? */
	count = 0;
	count += (info->is_hz) ? 1 : 0;
	count += (info->is_ohm) ? 1 : 0;
	count += (info->is_farad) ? 1 : 0;
	count += (info->is_ampere) ? 1 : 0;
	count += (info->is_volt) ? 1 : 0;
	count += (info->is_percent) ? 1 : 0;
	if (count > 1) {
		sr_dbg("More than one measurement type detected in packet.");
		return FALSE;
	}

	/* Both AC and DC set? */
	if (info->is_ac && info->is_dc) {
		sr_dbg("Both AC and DC flags detected in packet.");
		return FALSE;
	}

	/* RS232 flag not set? */
	if (!info->is_rs232) {
		sr_dbg("No RS232 flag detected in packet.");
		return FALSE;
	}

	return TRUE;
}

static int parse_value(const uint8_t *buf, float *result)
{
	int i, sign, intval = 0, digits[4];
	uint8_t digit_bytes[4];
	float floatval;

	/* Byte 1 contains sign in bit 0 */
	sign = ((buf[1] & (1 << 0)) != 0) ? -1 : 1;

	/*
	 * Bytes 1-8: Value (4 decimal digits, sign, decimal point)
	 *
	 * Over limit: "0L" (LCD), 0x00 0x7d 0x68 0x00 (digit bytes).
	 */

	/* Merge the two nibbles for a digit into one byte. */
	for (i = 0; i < 4; i++) {
		digit_bytes[i] = ((buf[1 + (i * 2)] & 0x0f) << 4);
		digit_bytes[i] |= (buf[1 + (i * 2) + 1] & 0x0f);

		/* Bit 4 in the byte is not part of the digit. */
		digit_bytes[i] &= ~(1 << 4);
	}

	/* Check for "OL". */
	if (digit_bytes[0] == 0x00 && digit_bytes[1] == 0xeb &&
	    digit_bytes[2] == 0x61 && digit_bytes[3] == 0x00) {
		sr_spew("Over limit.");
		*result = INFINITY;
		return SR_OK;
	}

	/* Parse the digits. */
	for (i = 0; i < 4; i++)
		digits[i] = parse_digit(digit_bytes[i]);
	sr_spew("Digits: %02x %02x %02x %02x (%d%d%d%d).",
		digit_bytes[0], digit_bytes[1], digit_bytes[2], digit_bytes[3],
		digits[0], digits[1], digits[2], digits[3]);

	/* Merge all digits into an integer value. */
	for (i = 0; i < 4; i++) {
		intval *= 10;
		intval += digits[i];
	}

	floatval = (float)intval;

	/* Decimal point position. */
	if ((buf[3] & 0x01) != 0) {
		floatval /= 1000;
		sr_spew("Decimal point after first digit.");
	} else if ((buf[5] & 0x01) != 0) {
		floatval /= 100;
		sr_spew("Decimal point after second digit.");
	} else if ((buf[7] & 0x01) != 0) {
		floatval /= 10;
		sr_spew("Decimal point after third digit.");
	} else {
		sr_spew("No decimal point in the number.");
	}

	/* Apply sign. */
	floatval *= sign;

	sr_spew("The display value is %f.", floatval);

	*result = floatval;

	return SR_OK;
}

static void parse_flags(const uint8_t *buf, struct dtm0660_info *info)
{
	/* Byte 0: LCD SEG1 */
	info->is_ac         = (buf[0] & (1 << 0)) != 0;
	info->is_dc         = (buf[0] & (1 << 1)) != 0;
	info->is_auto       = (buf[0] & (1 << 2)) != 0;
	info->is_rs232      = (buf[0] & (1 << 3)) != 0;

	/* Byte 1: LCD SEG2 */
	info->is_sign       = (buf[1] & (1 << 0)) != 0;

	/* Byte 9: LCD SEG10 */
	info->is_micro      = (buf[9] & (1 << 0)) != 0;
	info->is_nano       = (buf[9] & (1 << 1)) != 0;
	info->is_kilo       = (buf[9] & (1 << 2)) != 0;
	info->is_diode      = (buf[9] & (1 << 3)) != 0;

	/* Byte 10: LCD SEG11 */
	info->is_milli      = (buf[10] & (1 << 0)) != 0;
	info->is_percent    = (buf[10] & (1 << 1)) != 0;
	info->is_mega       = (buf[10] & (1 << 2)) != 0;
	info->is_beep       = (buf[10] & (1 << 3)) != 0;

	/* Byte 11: LCD SEG12 */
	info->is_farad      = (buf[11] & (1 << 0)) != 0;
	info->is_ohm        = (buf[11] & (1 << 1)) != 0;
	info->is_rel        = (buf[11] & (1 << 2)) != 0;
	info->is_hold       = (buf[11] & (1 << 3)) != 0;

	/* Byte 12: LCD SEG13 */
	info->is_ampere     = (buf[12] & (1 << 0)) != 0;
	info->is_volt       = (buf[12] & (1 << 1)) != 0;
	info->is_hz         = (buf[12] & (1 << 2)) != 0;
	info->is_bat        = (buf[12] & (1 << 3)) != 0;

	/* Byte 13: LCD SEG14  */
	info->is_degf       = (buf[13] & (1 << 0)) != 0;
	info->is_degc       = (buf[13] & (1 << 1)) != 0;
	info->is_c2c1_00    = (buf[13] & (1 << 2)) != 0;
	info->is_c2c1_01    = (buf[13] & (1 << 3)) != 0;
    
    /* Byte 14: LCD SEG15  */
    info->is_apo        = (buf[14] & (1 << 0)) != 0;
    info->is_min        = (buf[14] & (1 << 1)) != 0;
    info->is_minmax     = (buf[14] & (1 << 2)) != 0;
    info->is_max        = (buf[14] & (1 << 3)) != 0;
}

static void handle_flags(struct sr_datafeed_analog_old *analog, float *floatval,
			 const struct dtm0660_info *info)
{
	/* Factors */
	if (info->is_nano)
		*floatval /= 1000000000;
	if (info->is_micro)
		*floatval /= 1000000;
	if (info->is_milli)
		*floatval /= 1000;
	if (info->is_kilo)
		*floatval *= 1000;
	if (info->is_mega)
		*floatval *= 1000000;

	/* Measurement modes */
	if (info->is_volt) {
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
	}
	if (info->is_ampere) {
		analog->mq = SR_MQ_CURRENT;
		analog->unit = SR_UNIT_AMPERE;
	}
	if (info->is_ohm) {
		analog->mq = SR_MQ_RESISTANCE;
		analog->unit = SR_UNIT_OHM;
	}
	if (info->is_hz) {
		analog->mq = SR_MQ_FREQUENCY;
		analog->unit = SR_UNIT_HERTZ;
	}
	if (info->is_farad) {
		analog->mq = SR_MQ_CAPACITANCE;
		analog->unit = SR_UNIT_FARAD;
	}
	if (info->is_beep) {
		analog->mq = SR_MQ_CONTINUITY;
		analog->unit = SR_UNIT_BOOLEAN;
		*floatval = (*floatval == INFINITY) ? 0.0 : 1.0;
	}
	if (info->is_diode) {
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
	}
	if (info->is_percent) {
		analog->mq = SR_MQ_DUTY_CYCLE;
		analog->unit = SR_UNIT_PERCENTAGE;
    }

    if (info->is_degc) {
        analog->mq = SR_MQ_TEMPERATURE;
        analog->unit = SR_UNIT_CELSIUS;
    }
    
    if (info->is_degf) {
        analog->mq = SR_MQ_TEMPERATURE;
        analog->unit = SR_UNIT_FAHRENHEIT;
    }
    
	/* Measurement related flags */
	if (info->is_ac)
		analog->mqflags |= SR_MQFLAG_AC;
	if (info->is_dc)
		analog->mqflags |= SR_MQFLAG_DC;
	if (info->is_auto)
		analog->mqflags |= SR_MQFLAG_AUTORANGE;
	if (info->is_diode)
		analog->mqflags |= SR_MQFLAG_DIODE;
	if (info->is_hold)
		analog->mqflags |= SR_MQFLAG_HOLD;
	if (info->is_rel)
		analog->mqflags |= SR_MQFLAG_RELATIVE;
    if (info->is_min)
        analog->mqflags |= SR_MQFLAG_MIN;
    if (info->is_max)
        analog->mqflags |= SR_MQFLAG_MAX;
    

	/* Other flags */
	if (info->is_rs232)
		sr_spew("RS232 enabled.");
	if (info->is_bat)
		sr_spew("Battery is low.");
    if (info->is_apo)
        sr_spew("Auto power-off mode is active.");
    if (info->is_minmax)
        sr_spew("Min Max mode active.");
    if (info->is_c2c1_00)
		sr_spew("User-defined LCD symbol 0 is active.");
	if (info->is_c2c1_01)
		sr_spew("User-defined LCD symbol 1 is active.");
}

SR_PRIV gboolean sr_dtm0660_packet_valid(const uint8_t *buf)
{
	struct dtm0660_info info;

	parse_flags(buf, &info);

	return (sync_nibbles_valid(buf) && flags_valid(&info));
}

/**
 * Parse a protocol packet.
 *
 * @param buf Buffer containing the 15-byte protocol packet. Must not be NULL.
 * @param floatval Pointer to a float variable. That variable will contain the
 *                 result value upon parsing success. Must not be NULL.
 * @param analog Pointer to a struct sr_datafeed_analog_old. The struct will be
 *               filled with data according to the protocol packet.
 *               Must not be NULL.
 * @param info Pointer to a struct dtm0660_info. The struct will be filled
 *             with data according to the protocol packet. Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR upon failure. Upon errors, the
 *         'analog' variable contents are undefined and should not be used.
 */
SR_PRIV int sr_dtm0660_parse(const uint8_t *buf, float *floatval,
			    struct sr_datafeed_analog_old *analog, void *info)
{
	int ret;
	struct dtm0660_info *info_local;

	info_local = (struct dtm0660_info *)info;

	if ((ret = parse_value(buf, floatval)) != SR_OK) {
		sr_dbg("Error parsing value: %d.", ret);
		return ret;
	}

	parse_flags(buf, info_local);
	handle_flags(analog, floatval, info_local);

	return SR_OK;
}
