/* Gstreamer
 * Copyright (C) <2014> Aurélien Zanelli <aurelien.zanelli@darkosphere.fr>
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

#ifndef __GST_CAVS_VIDEO_PARSER_H__
#define __GST_CAVS_VIDEO_PARSER_H__

#ifndef GST_USE_UNSTABLE_API
#warning "The Chinese AVS parsing library is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstCAVSVideoSequenceHeader GstCAVSVideoSequenceHeader;
typedef struct _GstCAVSVideoSequenceDisplayExtension GstCAVSVideoSequenceDisplayExtension;
typedef struct _GstCAVSVideoCopyrightExtension GstCAVSVideoCopyrightExtension;
typedef struct _GstCAVSVideoCameraParametersExtension GstCAVSVideoCameraParametersExtension;
typedef struct _GstCAVSVideoPictureDisplayExtension GstCAVSVideoPictureDisplayExtension;
typedef struct _GstCAVSVideoExtensionData GstCAVSVideoExtensionData;
typedef struct _GstCAVSVideoPictureHeader GstCAVSVideoPictureHeader;
typedef struct _GstCAVSVideoSliceHeader GstCAVSVideoSliceHeader;
typedef struct _GstCAVSVideoUnit GstCAVSVideoUnit;

/**
 * GstCAVSVideoProfile:
 * @GST_CAVS_VIDEO_PROFILE_JIZHUN: Jihzhun profile
 *
 * CAVS videos profiles (Annex B.1)
 */
typedef enum {
  GST_CAVS_VIDEO_PROFILE_JIZHUN = 0x20
} GstCAVSVideoProfile;

/**
 * GstCAVSVideoLevel:
 * @GST_CAVS_VIDEO_LEVEL_2_0: 2.0
 * @GST_CAVS_VIDEO_LEVEL_2_1: 2.1
 * @GST_CAVS_VIDEO_LEVEL_4_0: 4.0
 * @GST_CAVS_VIDEO_LEVEL_4_2: 4.2
 * @GST_CAVS_VIDEO_LEVEL_6_0: 6.0
 * @GST_CAVS_VIDEO_LEVEL_6_0_1: 6.0.1
 * @GST_CAVS_VIDEO_LEVEL_6_2: 6.2
 *
 * CAVS videos levels (Annex B.2)
 */
typedef enum {
  GST_CAVS_VIDEO_LEVEL_2_0   = 0x10,
  GST_CAVS_VIDEO_LEVEL_2_1   = 0x11,
  GST_CAVS_VIDEO_LEVEL_4_0   = 0x20,
  GST_CAVS_VIDEO_LEVEL_4_2   = 0x22,
  GST_CAVS_VIDEO_LEVEL_6_0   = 0x40,
  GST_CAVS_VIDEO_LEVEL_6_0_1 = 0x41,
  GST_CAVS_VIDEO_LEVEL_6_2   = 0x42,
} GstCAVSVideoLevel;

/**
 * GstCAVSVideoUnitType:
 * @GST_CAVS_VIDEO_UNIT_SLICE: Slice unit
 * @GST_CAVS_VIDEO_UNIT_SEQUENCE_HEADER: Sequence header unit
 * @GST_CAVS_VIDEO_UNIT_VIDEO_SEQUENCE_END: End of video sequence unit
 * @GST_CAVS_VIDEO_UNIT_USER_DATA: User data unit
 * @GST_CAVS_VIDEO_UNIT_I_PICTURE: I picture header unit
 * @GST_CAVS_VIDEO_UNIT_EXTENSION: Extension data unit
 * @GST_CAVS_VIDEO_UNIT_PB_PICTURE: PB picture header unit
 * @GST_CAVS_VIDEO_UNIT_VIDEO_EDIT: Video edit unit
 *
 * Indicates the type of CAVS video unit.
 */
typedef enum {
  GST_CAVS_VIDEO_UNIT_SLICE              = 0x00,

  GST_CAVS_VIDEO_UNIT_SEQUENCE_HEADER    = 0xb0,
  GST_CAVS_VIDEO_UNIT_VIDEO_SEQUENCE_END = 0xb1,
  GST_CAVS_VIDEO_UNIT_USER_DATA          = 0xb2,
  GST_CAVS_VIDEO_UNIT_I_PICTURE          = 0xb3,
  /* 0xb4 is reserved */
  GST_CAVS_VIDEO_UNIT_EXTENSION          = 0xb5,
  GST_CAVS_VIDEO_UNIT_PB_PICTURE         = 0xb6,
  GST_CAVS_VIDEO_UNIT_VIDEO_EDIT         = 0xb7,
  /* 0xb8 is reserved */

  GST_CAVS_VIDEO_UNIT_SYSTEM             = 0xb9
} GstCAVSVideoUnitType;

/**
 * GstCAVSVideoParserResult:
 * @GST_CAVS_VIDEO_PARSER_OK: The parsing succeeded
 * @GST_CAVS_VIDEO_PARSER_NO_UNIT: No CAVS unit found during the parsing
 * @GST_CAVS_VIDEO_PARSER_NO_UNIT_END: Start of CAVS unit found, but not the end
 * @GST_CAVS_VIDEO_PARSER_ERROR: An error occured when parsing
 */
typedef enum {
  GST_CAVS_VIDEO_PARSER_OK,
  GST_CAVS_VIDEO_PARSER_NO_UNIT,
  GST_CAVS_VIDEO_PARSER_NO_UNIT_END,
  GST_CAVS_VIDEO_PARSER_ERROR
} GstCAVSVideoParserResult;

typedef enum {
  GST_CAVS_VIDEO_CHROMA_4_2_0 = 1,
  GST_CAVS_VIDEO_CHROMA_4_2_2 = 2
} GstCAVSVideoChroma;

/**
 * GstCAVSVideoPictureHeaderType:
 * @GST_CAVS_VIDEO_PICTURE_HEADER_I: I picture header
 * @GST_CAVS_VIDEO_PICTURE_HEADER_PB: PB picture header
 *
 * Indicates the type of the #GstPictureHeader
 */
typedef enum {
  GST_CAVS_VIDEO_PICTURE_HEADER_I,
  GST_CAVS_VIDEO_PICTURE_HEADER_PB
} GstCAVSVideoPictureHeaderType;

/**
 * GstCAVSVideoPictureType:
 * @GST_CAVS_VIDEO_PICTURE_I: I frame
 * @GST_CAVS_VIDEO_PICTURE_P: P frame
 * @GST_CAVS_VIDEO_PICTURE_B: B frame
 *
 * Indicate the type of the PB picture coding type
 */
typedef enum {
  GST_CAVS_VIDEO_PICTURE_I = 0,
  GST_CAVS_VIDEO_PICTURE_P = 1,
  GST_CAVS_VIDEO_PICTURE_B = 2
} GstCAVSVideoPictureType;

/**
 * GstCAVSVideoExtensionDataType:
 * @GST_CAVS_VIDEO_EXTENSION_DATA_SEQUENCE_DISPLAY: Sequence display extension data
 * @GST_CAVS_VIDEO_EXTENSION_DATA_COPYRIGHT: Copyright extension data
 * @GST_CAVS_VIDEO_EXTENSION_DATA_PICTURE_DISPLAY: Picture display extension data
 * @GST_CAVS_VIDEO_EXTENSION_DATA_CAMERA_PARAMETERS: Camera parameters extension data
 *
 * Indicate the type of the extension data
 */
typedef enum {
  GST_CAVS_VIDEO_EXTENSION_DATA_SEQUENCE_DISPLAY  = 2,
  GST_CAVS_VIDEO_EXTENSION_DATA_COPYRIGHT         = 4,
  GST_CAVS_VIDEO_EXTENSION_DATA_PICTURE_DISPLAY   = 7,
  GST_CAVS_VIDEO_EXTENSION_DATA_CAMERA_PARAMETERS = 11
} GstCAVSVideoExtensionDataType;

/**
 * GstCAVSVideoFormat:
 * @GST_CAVS_VIDEO_FORMAT_COMPONENT: Component video format
 * @GST_CAVS_VIDEO_FORMAT_PAL: PAL video format
 * @GST_CAVS_VIDEO_FORMAT_NTSC: NTSC video format
 * @GST_CAVS_VIDEO_FORMAT_SECAM: SECAM video format
 * @GST_CAVS_VIDEO_FORMAT_MAC: MAC video format
 * @GST_CAVS_VIDEO_FORMAT_UNSPECIFIED: Unspecified video format
 */
typedef enum {
  GST_CAVS_VIDEO_FORMAT_COMPONENT   = 0,
  GST_CAVS_VIDEO_FORMAT_PAL         = 1,
  GST_CAVS_VIDEO_FORMAT_NTSC        = 2,
  GST_CAVS_VIDEO_FORMAT_SECAM       = 3,
  GST_CAVS_VIDEO_FORMAT_MAC         = 4,
  GST_CAVS_VIDEO_FORMAT_UNSPECIFIED = 5
} GstCAVSVideoFormat;

/**
 * GstCAVSVideoSequenceHeader:
 *
 * CAVS Video Sequence Header
 */
struct _GstCAVSVideoSequenceHeader {
  guint8 profile_id;
  guint8 level_id;
  guint8 progressive_sequence;
  guint16 horizontal_size;
  guint16 vertical_size;
  GstCAVSVideoChroma chroma_format;
  guint8 sample_precision;
  guint8 aspect_ratio;
  guint8 frame_rate_code;
  guint32 bit_rate_lower;
  guint16 bit_rate_upper;
  guint8 low_delay;
  guint32 bbv_buffer_size;

  /* calculated values */
  guint fps_n;
  guint fps_d;
  guint bitrate;
  guint mb_width;
  guint mb_height;
  guint bitstream_buffer_size;
};

struct _GstCAVSVideoSequenceDisplayExtension {
  GstCAVSVideoFormat video_format;
  guint8 sample_range;

  guint8 colour_description;
  guint8 colour_primaries;
  guint8 transfer_characteristics;
  guint8 matrix_coefficients;

  guint16 display_horizontal_size;
  guint16 display_vertical_size;
};

struct _GstCAVSVideoCopyrightExtension {
  guint8 copyright_flag;
  guint8 copyright_id;
  guint8 original_or_copy;
  guint32 copyright_number_1;
  guint32 copyright_number_2;
  guint32 copyright_number_3;

  /* calculated value */
  guint64 copyright_number;
};

struct _GstCAVSVideoCameraParametersExtension {
  guint8 camera_id;
  guint32 height_of_image_device;
  guint32 focal_length;
  guint32 f_number;
  guint32 vertical_angle_of_view;
  gint16 camera_position_x_upper;
  gint16 camera_position_x_lower;
  gint16 camera_position_y_upper;
  gint16 camera_position_y_lower;
  gint16 camera_position_z_lower;
  gint16 camera_position_z_upper;
  gint32 camera_direction_x;
  gint32 camera_direction_y;
  gint32 camera_direction_z;
  gint32 image_plane_vertical_x;
  gint32 image_plane_vertical_y;
  gint32 image_plane_vertical_z;

  /* calculated value */
  gint32 camera_position_x;
  gint32 camera_position_y;
  gint32 camera_position_z;
};

struct _GstCAVSVideoPictureDisplayExtension {
  gint16 frame_centre_horizontal_offset[3];
  gint16 frame_centre_vertical_offset[3];
};

struct _GstCAVSVideoExtensionData {
  GstCAVSVideoExtensionDataType type;

  union {
    GstCAVSVideoSequenceDisplayExtension sequence_display;
    GstCAVSVideoCopyrightExtension copyright;
    GstCAVSVideoCameraParametersExtension camera_parameters;
    GstCAVSVideoPictureDisplayExtension picture_display;
  } data;
};

struct _GstCAVSVideoPictureHeader {
  GstCAVSVideoPictureHeaderType type;

  guint16 bbv_delay;

  guint8 time_code_flag; /* I picture only */
  guint32 time_code;     /* I picture only */

  GstCAVSVideoPictureType picture_coding_type; /* PB picture only */

  guint8 picture_distance;
  guint32 bbv_check_times;

  guint8 progressive_frame;
  guint8 picture_structure;

  guint8 advanced_pred_mode_disable; /* PB picture only */

  guint8 top_field_first;
  guint8 repeat_first_field;
  guint8 fixed_picture_qp;
  guint8 picture_qp;

  guint8 picture_reference_flag;    /* PB picture only */
  guint8 no_forward_reference_flag; /* PB picture only */

  guint8 skip_mode_flag;
  guint8 loop_filter_disable;
  guint8 loop_filter_parameter_flag;
  gint32 alpha_c_offset;
  gint32 beta_offset;
};

struct _GstCAVSVideoSliceHeader {
  guint8 slice_vertical_position;
  guint8 slice_vertical_position_extension;
  guint8 fixed_slice_qp;
  guint8 slice_qp;

  guint8 slice_weighting_flag;
  guint8 luma_scale[4];
  gint8 luma_shift[4];
  guint8 chroma_scale[4];
  gint8 chroma_shift[4];
  guint8 mb_weighting_flag;

  /* calculated value */
  guint16 mb_row;
};

struct _GstCAVSVideoUnit {
  GstCAVSVideoUnitType type;

  const guint8 *data;

  guint sc_offset; /* start code offset */
  guint offset;    /* unit content offset */
  gsize size;      /* unit content size */
};

GstCAVSVideoParserResult gst_cavs_video_parser_identify_unit (const guint8 * data,
    guint offset, gsize size, GstCAVSVideoUnit * unit);

GstCAVSVideoParserResult gst_cavs_video_parser_parse_sequence_header (const GstCAVSVideoUnit * unit,
    GstCAVSVideoSequenceHeader * seqhdr);

GstCAVSVideoParserResult gst_cavs_video_parser_parse_extension_data (const GstCAVSVideoUnit * unit,
    const GstCAVSVideoSequenceHeader * seqhdr,
    const GstCAVSVideoPictureHeader * picture_header, GstCAVSVideoExtensionData * ext);

GstCAVSVideoParserResult gst_cavs_video_parser_parse_i_picture (const GstCAVSVideoUnit * unit,
    const GstCAVSVideoSequenceHeader * seqhdr, GstCAVSVideoPictureHeader * pic);

GstCAVSVideoParserResult gst_cavs_video_parser_parse_pb_picture (const GstCAVSVideoUnit * unit,
    const GstCAVSVideoSequenceHeader * seqhdr, GstCAVSVideoPictureHeader * pic);

GstCAVSVideoParserResult gst_cavs_video_parser_parse_slice_header (const GstCAVSVideoUnit * unit,
    const GstCAVSVideoSequenceHeader * seqhdr,
    const GstCAVSVideoPictureHeader * pic, GstCAVSVideoSliceHeader * slice);

G_END_DECLS

#endif
