/* Gstreamer
 * Copyright (C) <2014> Aurélien Zanelli <aurelien.zanelli@darkosphere.fr>
 *
 * CAVSReader code has been copied and adjusted from nalutils.c
 *    Copyright (C) <2011> Intel Corporation
 *    Copyright (C) <2011> Collabora Ltd.
 *    Copyright (C) <2011> Thibault Saunier <thibault.saunier@collabora.com>
 *    Copyright (C) <2010> Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 *    Copyright (C) <2010> Collabora Multimedia
 *    Copyright (C) <2010> Nokia Corporation
 *    (C) 2005 Michal Benes <michal.benes@itonis.tv>
 *    (C) 2008 Wim Taymans <wim.taymans@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:gstcavsvideoparser
 * @short_description: Convenience library for parsing Chinese AVS video
 * bitstream.
 *
 * For more details about the structures, look at the Chinese AVS standard
 * part-2 video (GB/T 20090.2--2006)
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/base/gstbytereader.h>
#include <gst/base/gstbitreader.h>
#include <string.h>
#include "gstcavsvideoparser.h"

#ifndef GST_DISABLE_GST_DEBUG

#define GST_CAT_DEFAULT ensure_debug_category()

static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce;

  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new ("codecparsers_cavsvideo", 0,
        "Chinese AVS video codec parsing library");

    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}

#else

#define ensure_debug_category()

#endif /* GST_DISABLE_GST_DEBUG */

/**
 * CAVSReader implementation
 */
typedef struct
{
  const guint8 *data;
  guint size;

  guint n_epb;                  /* Number of emulation prevention bytes */
  guint byte;                   /* Byte position */
  guint bits_in_cache;          /* bitpos in the cache of next bit */
  guint8 first_byte;
  guint64 cache;                /* cached bytes */
} CAVSReader;

static void
avs_reader_init (CAVSReader * nr, const guint8 * data, guint size)
{
  nr->data = data;
  nr->size = size;
  nr->n_epb = 0;

  nr->byte = 0;
  nr->bits_in_cache = 0;
  /* fill with something other than 0 to detect emulation prevention bytes */
  nr->first_byte = 0xff;
  nr->cache = 0xff;
}

static inline gboolean
avs_reader_read (CAVSReader * nr, guint nbits)
{
  if (G_UNLIKELY (nr->byte * 8 + (nbits - nr->bits_in_cache) > nr->size * 8)) {
    GST_DEBUG ("Can not read %u bits, bits in cache %u, Byte * 8 %u, size in "
        "bits %u", nbits, nr->bits_in_cache, nr->byte * 8, nr->size * 8);
    return FALSE;
  }

  while (nr->bits_in_cache < nbits) {
    guint8 byte;
    gboolean check_three_byte;

    check_three_byte = TRUE;
  next_byte:
    if (G_UNLIKELY (nr->byte >= nr->size))
      return FALSE;

    byte = nr->data[nr->byte++];

    /* check if the byte is a emulation_prevention_three_byte */
    if (check_three_byte && byte == 0x02 && nr->first_byte == 0x00 &&
        ((nr->cache & 0xff) == 0)) {
      /* next byte goes unconditioavsly to the cache, even if it's 0x02 */
      check_three_byte = FALSE;
      nr->n_epb++;
      goto next_byte;
    }
    nr->cache = (nr->cache << 8) | nr->first_byte;
    nr->first_byte = byte;
    nr->bits_in_cache += 8;
  }

  return TRUE;
}

/* Skips the specified amount of bits. This is only suitable to a
   cacheable number of bits */
static inline gboolean
avs_reader_skip (CAVSReader * nr, guint nbits)
{
  g_assert (nbits <= 8 * sizeof (nr->cache));

  if (G_UNLIKELY (!avs_reader_read (nr, nbits)))
    return FALSE;

  nr->bits_in_cache -= nbits;

  return TRUE;
}

static inline guint
avs_reader_get_pos (const CAVSReader * nr)
{
  return nr->byte * 8 - nr->bits_in_cache;
}

static inline guint
avs_reader_get_remaining (const CAVSReader * nr)
{
  return (nr->size - nr->byte) * 8 + nr->bits_in_cache;
}

static inline guint
avs_reader_get_epb_count (const CAVSReader * nr)
{
  return nr->n_epb;
}

#define CAVS_READER_READ_BITS(bits) \
static gboolean \
avs_reader_get_bits_uint##bits (CAVSReader *nr, guint##bits *val, guint nbits) \
{ \
  guint shift; \
  \
  if (!avs_reader_read (nr, nbits)) \
    return FALSE; \
  \
  /* bring the required bits down and truncate */ \
  shift = nr->bits_in_cache - nbits; \
  *val = nr->first_byte >> shift; \
  \
  *val |= nr->cache << (8 - shift); \
  /* mask out required bits */ \
  if (nbits < bits) \
    *val &= ((guint##bits)1 << nbits) - 1; \
  \
  nr->bits_in_cache = shift; \
  \
  return TRUE; \
} \

CAVS_READER_READ_BITS (8);
CAVS_READER_READ_BITS (16);
CAVS_READER_READ_BITS (32);

static gboolean
avs_reader_get_ue (CAVSReader * nr, guint32 * val)
{
  guint i = 0;
  guint8 bit;
  guint32 value;

  if (G_UNLIKELY (!avs_reader_get_bits_uint8 (nr, &bit, 1))) {

    return FALSE;
  }

  while (bit == 0) {
    i++;
    if G_UNLIKELY
      ((!avs_reader_get_bits_uint8 (nr, &bit, 1)))
          return FALSE;
  }

  if (G_UNLIKELY (i > 32))
    return FALSE;

  if (G_UNLIKELY (!avs_reader_get_bits_uint32 (nr, &value, i)))
    return FALSE;

  *val = (1 << i) - 1 + value;

  return TRUE;
}

static inline gboolean
avs_reader_get_se (CAVSReader * nr, gint32 * val)
{
  guint32 value;

  if (G_UNLIKELY (!avs_reader_get_ue (nr, &value)))
    return FALSE;

  if (value % 2)
    *val = (value / 2) + 1;
  else
    *val = -(value / 2);

  return TRUE;
}

#define CHECK_ALLOWED(val, min, max) { \
  if (val < min || val > max) { \
    GST_WARNING ("value not in allowed range. value: %d, range %d-%d", \
                     val, min, max); \
    goto error; \
  } \
}

#define READ_UINT8(r, val, nbits) { \
  if (!avs_reader_get_bits_uint8 (r, &val, nbits)) { \
    GST_WARNING ("failed to read uint8, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_INT8(r, val, nbits) { \
  guint8 tmp; \
  guint shift = 8 - nbits; \
  if (!avs_reader_get_bits_uint8 (r, &tmp, nbits)) { \
    GST_WARNING ("failed to read int8, nbits: %d", nbits); \
    goto error; \
  } \
  val = (((gint8) tmp) << shift) >> shift; \
}

#define READ_UINT16(r, val, nbits) { \
  if (!avs_reader_get_bits_uint16 (r, &val, nbits)) { \
    GST_WARNING ("failed to read uint16, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_INT16(r, val, nbits) { \
  guint16 tmp; \
  guint shift = 16 - nbits; \
  if (!avs_reader_get_bits_uint16 (r, &tmp, nbits)) { \
    GST_WARNING ("failed to read int16, nbits: %d", nbits); \
    goto error; \
  } \
  val = (((gint16) tmp) << shift) >> shift; \
}

#define READ_UINT32(r, val, nbits) { \
  if (!avs_reader_get_bits_uint32 (r, &val, nbits)) { \
    GST_WARNING ("failed to read uint32, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_INT32(r, val, nbits) { \
  guint32 tmp; \
  guint shift = 16 - nbits; \
  if (!avs_reader_get_bits_uint32 (r, &tmp, nbits)) { \
    GST_WARNING ("failed to read int32, nbits: %d", nbits); \
    goto error; \
  } \
  val = (((gint32) tmp) << shift) >> shift; \
}

#define READ_UINT64(r, val, nbits) { \
  if (!avs_reader_get_bits_uint64 (r, &val, nbits)) { \
    GST_WARNING ("failed to read uint32, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_UE(r, val) { \
  if (!avs_reader_get_ue (r, &val)) { \
    GST_WARNING ("failed to read UE"); \
    goto error; \
  } \
}

#define READ_UE_ALLOWED(r, val, min, max) { \
  guint32 tmp; \
  READ_UE (r, tmp); \
  CHECK_ALLOWED (tmp, min, max); \
  val = tmp; \
}

#define READ_SE(r, val) { \
  if (!avs_reader_get_se (r, &val)) { \
    GST_WARNING ("failed to read SE"); \
    goto error; \
  } \
}

#define READ_SE_ALLOWED(r, val, min, max) { \
  gint32 tmp; \
  READ_SE (r, tmp); \
  CHECK_ALLOWED (tmp, min, max); \
  val = tmp; \
}

#define CHECK_MARKER_BIT(r) { \
  guint8 marker_bit; \
  READ_UINT8 (r, marker_bit, 1); \
  if (marker_bit != 1) { \
    GST_WARNING ("bad marker bit"); \
  } \
}

/***********  end of avs reader ***************/

static inline gint
scan_for_start_codes (const guint8 * data, guint size)
{
  GstByteReader br;
  gst_byte_reader_init (&br, data, size);

  return gst_byte_reader_masked_scan_uint32 (&br, 0xffffff00, 0x00000100,
      0, size);
}



/**
 * Parser implementation
 */
typedef enum
{
  GST_CAVS_VIDEO_SLICE_RANGE_START = 0x00,
  GST_CAVS_VIDEO_SLICE_RANGE_END = 0xaf,
  GST_CAVS_VIDEO_VIDEO_SEQUENCE_START = 0xb0,
  GST_CAVS_VIDEO_VIDEO_SEQUENCE_END = 0xb1,
  GST_CAVS_VIDEO_USER_DATA = 0xb2,
  GST_CAVS_VIDEO_I_PICTURE = 0xb3,
  GST_CAVS_VIDEO_EXTENSION = 0xb5,
  GST_CAVS_VIDEO_PB_PICTURE = 0xb6,
  GST_CAVS_VIDEO_VIDEO_EDIT = 0xb7,
  GST_CAVS_VIDEO_SYSTEM_RANGE_START = 0xb9,
  GST_CAVS_VIDEO_SYSTEM_RANGE_END = 0xff
} GstCAVSVideoStartCode;;

typedef struct
{
  guint fps_n;
  guint fps_d;
} FPS;

/* Table 7-6 - Frame rate codes */
static FPS frame_rates[9] = {
  {0, 0},                       /* forbidden */
  {24000, 1001},
  {24, 1},
  {25, 1},
  {30000, 1001},
  {30, 1},
  {50, 1},
  {60000, 1001},
  {60, 1}
};

static GstCAVSVideoParserResult
gst_cavs_video_parser_parse_sequence_display_extension (CAVSReader * r,
    GstCAVSVideoSequenceDisplayExtension * sequence_display)
{
  guint8 tmp;

  GST_DEBUG ("parse sequence display extension");

  memset (sequence_display, 0, sizeof (*sequence_display));

  READ_UINT8 (r, tmp, 3);
  sequence_display->video_format = tmp;

  READ_UINT8 (r, sequence_display->sample_range, 1);

  READ_UINT8 (r, sequence_display->colour_description, 1);
  if (sequence_display->colour_description) {
    READ_UINT8 (r, sequence_display->colour_primaries, 8);
    CHECK_ALLOWED (sequence_display->colour_primaries, 1, 255);

    READ_UINT8 (r, sequence_display->transfer_characteristics, 8);
    CHECK_ALLOWED (sequence_display->transfer_characteristics, 1, 255);

    READ_UINT8 (r, sequence_display->matrix_coefficients, 8);
    CHECK_ALLOWED (sequence_display->matrix_coefficients, 1, 255);
  }

  READ_UINT16 (r, sequence_display->display_horizontal_size, 14);

  CHECK_MARKER_BIT (r);

  READ_UINT16 (r, sequence_display->display_vertical_size, 14);

  return GST_CAVS_VIDEO_PARSER_OK;

error:
  GST_ERROR ("parse sequence display extension failed");
  return GST_CAVS_VIDEO_PARSER_ERROR;
}

static GstCAVSVideoParserResult
gst_cavs_video_parser_parse_copyright_extension (CAVSReader * r,
    GstCAVSVideoCopyrightExtension * copyright)
{
  GST_DEBUG ("parse copyright extension");

  memset (copyright, 0, sizeof (*copyright));

  READ_UINT8 (r, copyright->copyright_flag, 1);
  READ_UINT8 (r, copyright->copyright_id, 8);
  READ_UINT8 (r, copyright->original_or_copy, 1);

  if (!avs_reader_skip (r, 7))
    goto error;

  CHECK_MARKER_BIT (r);

  READ_UINT32 (r, copyright->copyright_number_1, 20);
  CHECK_MARKER_BIT (r);
  READ_UINT32 (r, copyright->copyright_number_2, 22);
  CHECK_MARKER_BIT (r);
  READ_UINT32 (r, copyright->copyright_number_3, 22);

  copyright->copyright_number = ((guint64) copyright->copyright_number_1) << 44;
  copyright->copyright_number += copyright->copyright_number_2 << 22;
  copyright->copyright_number += copyright->copyright_number_3;

  return GST_CAVS_VIDEO_PARSER_OK;

error:
  GST_ERROR ("parse copyright extension failed");
  return GST_CAVS_VIDEO_PARSER_ERROR;
}

static GstCAVSVideoParserResult
gst_cavs_video_parser_parse_picture_display_extension (CAVSReader * r,
    const GstCAVSVideoSequenceHeader * seqhdr,
    const GstCAVSVideoPictureHeader * picture_header,
    GstCAVSVideoPictureDisplayExtension * picture_display)
{
  guint i;
  guint n_frames_centre_offsets = 0;

  GST_DEBUG ("parse picture display extension");

  memset (picture_display, 0, sizeof (*picture_display));

  if (seqhdr->progressive_sequence && picture_header->repeat_first_field)
    n_frames_centre_offsets = picture_header->repeat_first_field ? 3 : 2;
  else if (!seqhdr->progressive_sequence && picture_header->picture_structure)
    n_frames_centre_offsets = picture_header->repeat_first_field ? 3 : 2;
  else
    n_frames_centre_offsets = 1;

  for (i = 0; i < n_frames_centre_offsets; i++) {
    READ_INT16 (r, picture_display->frame_centre_horizontal_offset[i], 16);
    CHECK_MARKER_BIT (r);
    READ_INT16 (r, picture_display->frame_centre_vertical_offset[i], 16);
    CHECK_MARKER_BIT (r);
  }

  return GST_CAVS_VIDEO_PARSER_OK;

error:
  GST_ERROR ("parse picture display extension failed");
  return GST_CAVS_VIDEO_PARSER_ERROR;
}

static GstCAVSVideoParserResult
gst_cavs_video_parser_parse_camera_parameters_extension (CAVSReader * r,
    GstCAVSVideoCameraParametersExtension * camera_params)
{
  GST_DEBUG ("parse camera parameter extension");

  memset (camera_params, 0, sizeof (*camera_params));

  if (!avs_reader_skip (r, 1))
    goto error;

  READ_UINT8 (r, camera_params->camera_id, 7);
  CHECK_MARKER_BIT (r);
  READ_UINT32 (r, camera_params->height_of_image_device, 22);
  CHECK_MARKER_BIT (r);
  READ_UINT32 (r, camera_params->focal_length, 22);
  CHECK_MARKER_BIT (r);
  READ_UINT32 (r, camera_params->f_number, 22);
  CHECK_MARKER_BIT (r);
  READ_UINT32 (r, camera_params->vertical_angle_of_view, 22);
  CHECK_MARKER_BIT (r);

  READ_INT16 (r, camera_params->camera_position_x_upper, 16);
  CHECK_MARKER_BIT (r);
  READ_INT16 (r, camera_params->camera_position_x_lower, 16);
  CHECK_MARKER_BIT (r);
  camera_params->camera_position_x =
      ((gint32) camera_params->camera_position_x_upper << 16) +
      camera_params->camera_position_x_lower;

  READ_INT16 (r, camera_params->camera_position_y_upper, 16);
  CHECK_MARKER_BIT (r);
  READ_INT16 (r, camera_params->camera_position_y_lower, 16);
  CHECK_MARKER_BIT (r);
  camera_params->camera_position_y =
      ((gint32) camera_params->camera_position_y_upper << 16) +
      camera_params->camera_position_y_lower;

  READ_INT16 (r, camera_params->camera_position_z_upper, 16);
  CHECK_MARKER_BIT (r);
  READ_INT16 (r, camera_params->camera_position_z_lower, 16);
  CHECK_MARKER_BIT (r);
  camera_params->camera_position_z =
      ((gint32) camera_params->camera_position_z_upper << 16) +
      camera_params->camera_position_z_lower;

  READ_INT32 (r, camera_params->camera_direction_x, 22);
  CHECK_MARKER_BIT (r);
  READ_INT32 (r, camera_params->camera_direction_y, 22);
  CHECK_MARKER_BIT (r);
  READ_INT32 (r, camera_params->camera_direction_z, 22);
  CHECK_MARKER_BIT (r);
  READ_INT32 (r, camera_params->image_plane_vertical_x, 22);
  CHECK_MARKER_BIT (r);
  READ_INT32 (r, camera_params->image_plane_vertical_y, 22);
  CHECK_MARKER_BIT (r);
  READ_INT32 (r, camera_params->image_plane_vertical_z, 22);
  CHECK_MARKER_BIT (r);

  return GST_CAVS_VIDEO_PARSER_OK;

error:
  GST_ERROR ("parse picture display extension failed");
  return GST_CAVS_VIDEO_PARSER_ERROR;
}

static guint
gst_cavs_video_parser_get_number_of_reference (const GstCAVSVideoPictureHeader *
    pic)
{
  guint ret = 0;

  switch (pic->picture_coding_type) {
    case GST_CAVS_VIDEO_PICTURE_I:
      if (!pic->picture_structure)
        ret = 1;
      break;
    case GST_CAVS_VIDEO_PICTURE_P:
    case GST_CAVS_VIDEO_PICTURE_B:
      ret = pic->picture_structure ? 2 : 4;
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  return ret;
}


/**** API ****/
/**
 * gst_cavs_video_parser_identify_unit:
 * @data: The data to parse
 * @offset: The offset from which to parse @data
 * @size: the size of @data
 * @unit: (out): The #GstCAVSVideoUnit where to store parsed unit headers
 *
 * Parses @data and fills @unit fields
 *
 * Returns: a @GstCAVSVideoParserResult
 */
GstCAVSVideoParserResult
gst_cavs_video_parser_identify_unit (const guint8 * data, guint offset,
    gsize size, GstCAVSVideoUnit * unit)
{
  gint off1, off2;
  guint8 sc;

  g_return_val_if_fail (unit != NULL, GST_CAVS_VIDEO_PARSER_ERROR);

  if (size < offset + 4) {
    GST_DEBUG ("buffer is too small: size = %" G_GSIZE_FORMAT ", offset = %u",
        size, offset);
    return GST_CAVS_VIDEO_PARSER_ERROR;
  }

  off1 = scan_for_start_codes (data + offset, size - offset);

  if (off1 < 0) {
    GST_DEBUG ("no start code in the buffer");
    return GST_CAVS_VIDEO_PARSER_NO_UNIT;
  }

  unit->data = data;
  unit->sc_offset = offset + off1;
  unit->offset = offset + off1 + 4;

  /* identify unit code */
  sc = unit->data[unit->sc_offset + 3];
  if (sc >= GST_CAVS_VIDEO_SLICE_RANGE_START
      && sc <= GST_CAVS_VIDEO_SLICE_RANGE_END)
    unit->type = GST_CAVS_VIDEO_UNIT_SLICE;
  else if (sc >= GST_CAVS_VIDEO_SYSTEM_RANGE_START
      && sc <= GST_CAVS_VIDEO_SYSTEM_RANGE_END)
    unit->type = GST_CAVS_VIDEO_UNIT_SYSTEM;
  else
    unit->type = sc;

  if (unit->type == GST_CAVS_VIDEO_UNIT_VIDEO_SEQUENCE_END) {
    GST_DEBUG ("video sequence end found");
    unit->size = 0;
    return GST_CAVS_VIDEO_PARSER_OK;
  }

  /* check for unit end */
  off2 = scan_for_start_codes (data + unit->offset, size - unit->offset);
  if (off2 < 0) {
    GST_DEBUG ("unit start %d, no end found", unit->offset);
    return GST_CAVS_VIDEO_PARSER_NO_UNIT_END;
  }

  unit->size = off2;

  GST_DEBUG ("complete unit found. offset: %d, size: %" G_GSIZE_FORMAT,
      unit->offset, unit->size);

  return GST_CAVS_VIDEO_PARSER_OK;
}

/**
 * gst_cavs_video_parser_parse_sequence_header:
 * @unit: A #GstCAVSVideoUnit of type #GST_CAVS_VIDEO_UNIT_SEQUENCE_HEADER
 * @seqhdr: The #GstCAVSVideoSequenceHeader to fill
 *
 * Parses @unit data and fill @seqhdr fields
 *
 * Returns: a #GstCAVSVideoParserResult
 */
GstCAVSVideoParserResult
gst_cavs_video_parser_parse_sequence_header (const GstCAVSVideoUnit * unit,
    GstCAVSVideoSequenceHeader * seqhdr)
{
  CAVSReader r;
  guint8 tmp;

  avs_reader_init (&r, unit->data + unit->offset, unit->size);
  memset (seqhdr, 0, sizeof (*seqhdr));

  GST_DEBUG ("parse sequence-header");

  READ_UINT8 (&r, seqhdr->profile_id, 8);
  READ_UINT8 (&r, seqhdr->level_id, 8);
  READ_UINT8 (&r, seqhdr->progressive_sequence, 1);

  READ_UINT16 (&r, seqhdr->horizontal_size, 14);
  seqhdr->mb_width = (seqhdr->horizontal_size + 15) / 16;

  READ_UINT16 (&r, seqhdr->vertical_size, 14);
  seqhdr->mb_height = (seqhdr->vertical_size + 15) / 16;

  READ_UINT8 (&r, tmp, 2);
  seqhdr->chroma_format = tmp;

  READ_UINT8 (&r, seqhdr->sample_precision, 3);
  CHECK_ALLOWED (seqhdr->sample_precision, 1, 7);

  READ_UINT8 (&r, seqhdr->aspect_ratio, 4);
  CHECK_ALLOWED (seqhdr->aspect_ratio, 1, 15);

  READ_UINT8 (&r, seqhdr->frame_rate_code, 4);
  CHECK_ALLOWED (seqhdr->frame_rate_code, 1, 8);
  seqhdr->fps_n = frame_rates[seqhdr->frame_rate_code].fps_n;
  seqhdr->fps_d = frame_rates[seqhdr->frame_rate_code].fps_d;

  READ_UINT32 (&r, seqhdr->bit_rate_lower, 18);

  CHECK_MARKER_BIT (&r);

  READ_UINT16 (&r, seqhdr->bit_rate_upper, 12);

  seqhdr->bitrate = (seqhdr->bit_rate_upper << 18) + seqhdr->bit_rate_lower;

  READ_UINT8 (&r, seqhdr->low_delay, 1);

  CHECK_MARKER_BIT (&r);

  READ_UINT32 (&r, seqhdr->bbv_buffer_size, 18);

  seqhdr->bitstream_buffer_size = 16 * 1024 * seqhdr->bbv_buffer_size;
  return GST_CAVS_VIDEO_PARSER_OK;

error:
  GST_ERROR ("parse sequence-header failed");
  return GST_CAVS_VIDEO_PARSER_ERROR;
}

/**
 * gst_cavs_video_parser_parse_extension_data:
 * @unit: A #GstCAVSVideoUnit of type #GST_CAVS_VIDEO_UNIT_EXTENSION
 * @seqhdr: The associated #GstCAVSVideoSequenceHeader
 * @picture_header: The associated #GstCAVSVideoPictureHeader
 * @ext: The #GstCAVSVideoExtensionData to fill
 *
 * Parses extension data in @unit data and fill @ext fields.
 *
 * Returns: a #GstCAVSVideoParserResult
 */
GstCAVSVideoParserResult
gst_cavs_video_parser_parse_extension_data (const GstCAVSVideoUnit * unit,
    const GstCAVSVideoSequenceHeader * seqhdr,
    const GstCAVSVideoPictureHeader * picture_header,
    GstCAVSVideoExtensionData * ext)
{
  GstCAVSVideoParserResult res;
  CAVSReader r;
  guint8 tmp;

  avs_reader_init (&r, unit->data + unit->offset, unit->size);
  GST_DEBUG ("parse extension data");

  /* Specification distinguish two cases: after sequence header and after
   * picture header. Since extension code are unique at this moment, don't
   * bother about position */
  READ_UINT8 (&r, tmp, 4);
  ext->type = tmp;

  switch (ext->type) {
    case GST_CAVS_VIDEO_EXTENSION_DATA_SEQUENCE_DISPLAY:
      res = gst_cavs_video_parser_parse_sequence_display_extension (&r,
          &ext->data.sequence_display);
      break;
    case GST_CAVS_VIDEO_EXTENSION_DATA_COPYRIGHT:
      res =
          gst_cavs_video_parser_parse_copyright_extension (&r,
          &ext->data.copyright);
      break;
    case GST_CAVS_VIDEO_EXTENSION_DATA_PICTURE_DISPLAY:
      res = gst_cavs_video_parser_parse_picture_display_extension (&r, seqhdr,
          picture_header, &ext->data.picture_display);
      break;
    case GST_CAVS_VIDEO_EXTENSION_DATA_CAMERA_PARAMETERS:
      res = gst_cavs_video_parser_parse_camera_parameters_extension (&r,
          &ext->data.camera_parameters);
      break;
    default:
      GST_INFO ("unknown extension data");
      res = GST_CAVS_VIDEO_PARSER_ERROR;
      break;
  }

  return res;

error:
  GST_ERROR ("parse extension data failed");
  return GST_CAVS_VIDEO_PARSER_ERROR;
}

/**
 * gst_cavs_video_parser_parse_i_picture:
 * @unit: A #GstCAVSVideoUnit of type #GST_CAVS_VIDEO_UNIT_I_PICTURE
 * @seqhdr: The associated #GstCAVSVideoSequenceHeader
 * @pic: The #GstCAVSVideoPictureHeader to fill
 *
 * Parses @unit data and fill @pic fields
 *
 * Returns: a GstCAVSVideoParserResult
 */
GstCAVSVideoParserResult
gst_cavs_video_parser_parse_i_picture (const GstCAVSVideoUnit * unit,
    const GstCAVSVideoSequenceHeader * seqhdr, GstCAVSVideoPictureHeader * pic)
{
  CAVSReader r;

  avs_reader_init (&r, unit->data + unit->offset, unit->size);
  memset (pic, 0, sizeof (*pic));
  GST_DEBUG ("parse I picture");

  /* Some default values */
  pic->picture_structure = 1;

  READ_UINT16 (&r, pic->bbv_delay, 16);

  READ_UINT8 (&r, pic->time_code_flag, 1);
  if (pic->time_code_flag)
    READ_UINT32 (&r, pic->time_code, 24);

  CHECK_MARKER_BIT (&r);

  READ_UINT8 (&r, pic->picture_distance, 8);

  if (seqhdr->low_delay)
    READ_UE (&r, pic->bbv_check_times);

  READ_UINT8 (&r, pic->progressive_frame, 1);
  if (!pic->progressive_frame)
    READ_UINT8 (&r, pic->picture_structure, 1);

  READ_UINT8 (&r, pic->top_field_first, 1);
  READ_UINT8 (&r, pic->repeat_first_field, 1);
  READ_UINT8 (&r, pic->fixed_picture_qp, 1);
  READ_UINT8 (&r, pic->picture_qp, 6);

  if (!pic->progressive_frame && !pic->picture_structure)
    READ_UINT8 (&r, pic->skip_mode_flag, 1);

  if (!avs_reader_skip (&r, 4))
    goto error;

  READ_UINT8 (&r, pic->loop_filter_disable, 1);
  if (!pic->loop_filter_disable) {
    READ_UINT8 (&r, pic->loop_filter_parameter_flag, 1);

    if (pic->loop_filter_parameter_flag) {
      READ_SE_ALLOWED (&r, pic->alpha_c_offset, -8, 8);
      READ_SE_ALLOWED (&r, pic->beta_offset, -8, 8);
    }
  }

  return GST_CAVS_VIDEO_PARSER_OK;

error:
  GST_ERROR ("parse I picture failed");
  return GST_CAVS_VIDEO_PARSER_ERROR;
}

/**
 * gst_cavs_video_parser_parse_pb_picture:
 * @unit: A #GstCAVSVideoUnit of type #GST_CAVS_VIDEO_UNIT_PB_PICTURE
 * @seqhdr: The associated #GstCAVSVideoSequenceHeader
 * @pic: The #GstCAVSVideoPictureHeader to fill
 *
 * Parses @unit data and fill @pic fields
 *
 * Returns: a GstCAVSVideoParserResult
 */
GstCAVSVideoParserResult
gst_cavs_video_parser_parse_pb_picture (const GstCAVSVideoUnit * unit,
    const GstCAVSVideoSequenceHeader * seqhdr, GstCAVSVideoPictureHeader * pic)
{
  CAVSReader r;
  guint8 tmp;

  avs_reader_init (&r, unit->data + unit->offset, unit->size);
  memset (pic, 0, sizeof (*pic));
  GST_DEBUG ("parse PB picture");

  /* Some default values */
  pic->picture_structure = 1;

  READ_UINT16 (&r, pic->bbv_delay, 16);

  READ_UINT8 (&r, tmp, 2);
  pic->picture_coding_type = tmp;

  READ_UINT8 (&r, pic->picture_distance, 8);

  if (seqhdr->low_delay)
    READ_UE (&r, pic->bbv_check_times);

  READ_UINT8 (&r, pic->progressive_frame, 1);
  if (!pic->progressive_frame) {
    READ_UINT8 (&r, pic->picture_structure, 1);

    if (!pic->picture_structure)
      READ_UINT8 (&r, pic->advanced_pred_mode_disable, 1);
  }

  READ_UINT8 (&r, pic->top_field_first, 1);
  READ_UINT8 (&r, pic->repeat_first_field, 1);
  READ_UINT8 (&r, pic->fixed_picture_qp, 1);
  READ_UINT8 (&r, pic->picture_qp, 6);

  if (!(pic->picture_coding_type == GST_CAVS_VIDEO_PICTURE_B
          && pic->picture_structure))
    READ_UINT8 (&r, pic->picture_reference_flag, 1);

  READ_UINT8 (&r, pic->no_forward_reference_flag, 1);

  if (!avs_reader_skip (&r, 3))
    goto error;

  READ_UINT8 (&r, pic->skip_mode_flag, 1);

  READ_UINT8 (&r, pic->loop_filter_disable, 1);
  if (!pic->loop_filter_disable) {
    READ_UINT8 (&r, pic->loop_filter_parameter_flag, 1);

    if (pic->loop_filter_parameter_flag) {
      READ_SE (&r, pic->alpha_c_offset);
      READ_SE (&r, pic->beta_offset);
    }
  }

  return GST_CAVS_VIDEO_PARSER_OK;

error:
  GST_ERROR ("parse PB picture failed");
  return GST_CAVS_VIDEO_PARSER_ERROR;
}

/**
 * gst_cavs_video_parser_parse_slice_header:
 * @unit: A #GstCAVSVideoUnit of type #GST_CAVS_VIDEO_UNIT_SLICE
 * @seqhdr: The associated #GstCAVSVideoSequenceHeader
 * @pic: The associated #GstCAVSVideoPictureHeader
 * @slice: The #GstCAVSVideoSlice to fill
 *
 * Parses @unit data and fill @slice fields
 *
 * Returns: a GstCAVSVideoParserResult
 */
GstCAVSVideoParserResult
gst_cavs_video_parser_parse_slice_header (const GstCAVSVideoUnit * unit,
    const GstCAVSVideoSequenceHeader * seqhdr,
    const GstCAVSVideoPictureHeader * pic, GstCAVSVideoSliceHeader * slice)
{
  CAVSReader r;
  guint mb_index;

  avs_reader_init (&r, unit->data + unit->offset, unit->size);
  memset (slice, 0, sizeof (*slice));
  GST_DEBUG ("parse slice");

  /* The vertical position is encoded in the start code */
  slice->slice_vertical_position = unit->data[unit->offset - 1];

  if (seqhdr->vertical_size > 2800)
    READ_UINT8 (&r, slice->slice_vertical_position_extension, 3);

  slice->mb_row = (slice->slice_vertical_position_extension << 7) +
      slice->slice_vertical_position;

  if (!pic->fixed_picture_qp) {
    READ_UINT8 (&r, slice->fixed_slice_qp, 1);
    READ_UINT8 (&r, slice->slice_qp, 6);
  }

  mb_index = slice->mb_row * seqhdr->mb_width;
  if (pic->picture_coding_type != GST_CAVS_VIDEO_PICTURE_I ||
      (!pic->picture_structure
          && mb_index >= seqhdr->mb_width * seqhdr->mb_height / 2)) {
    READ_UINT8 (&r, slice->slice_weighting_flag, 1);

    if (slice->slice_weighting_flag) {
      guint n_references;
      guint i;

      n_references = gst_cavs_video_parser_get_number_of_reference (pic);
      for (i = 0; i < n_references; i++) {
        READ_UINT8 (&r, slice->luma_scale[i], 8);
        READ_INT8 (&r, slice->luma_shift[i], 8);
        CHECK_MARKER_BIT (&r);

        READ_UINT8 (&r, slice->chroma_scale[i], 8);
        READ_INT8 (&r, slice->chroma_shift[i], 8);
        CHECK_MARKER_BIT (&r);
      }
      READ_UINT8 (&r, slice->mb_weighting_flag, 1);
    }
  }

  return GST_CAVS_VIDEO_PARSER_OK;

error:
  GST_ERROR ("parse slice failed");
  return GST_CAVS_VIDEO_PARSER_ERROR;
}
