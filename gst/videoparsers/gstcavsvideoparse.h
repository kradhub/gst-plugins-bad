/* GStreamer Chinese AVS Video Parser
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

#ifndef __GST_CAVS_VIDEO_PARSE_H__
#define __GST_CAVS_VIDEO_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstbaseparse.h>
#include <gst/codecparsers/gstcavsvideoparser.h>

G_BEGIN_DECLS

#define GST_TYPE_CAVS_VIDEO_PARSE \
  (gst_cavs_video_parse_get_type())
#define GST_CAVS_VIDEO_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CAVS_VIDEO_PARSE,GstCAVSVideoParse))
#define GST_CAVS_VIDEO_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CAVS_VIDEO_PARSE,GstCAVSVideoParseClass))
#define GST_IS_CAVS_VIDEO_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CAVS_VIDEO_PARSE))
#define GST_IS_CAVS_VIDEO_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CAVS_VIDEO_PARSE))

GType gst_cavs_video_parse_get_type (void);

typedef enum {
  GST_CAVS_STREAM_FORMAT_NONE = 0,
  GST_CAVS_STREAM_FORMAT_UNIT,
  GST_CAVS_STREAM_FORMAT_UNIT_FRAME
} GstCAVSStreamFormat;

typedef struct _GstCAVSVideoParse GstCAVSVideoParse;
typedef struct _GstCAVSVideoParseClass GstCAVSVideoParseClass;

struct _GstCAVSVideoParse
{
  GstBaseParse baseparse;

  /* stream */
  GstCAVSVideoProfile profile;
  GstCAVSVideoLevel level;
  gint width, height;
  gint fps_num, fps_den;

  GstCAVSVideoSequenceHeader seqhdr;
  GstCAVSVideoPictureHeader pichdr;

  /* state */
  gboolean update_caps;
  gboolean have_seqhdr;
  GstCAVSStreamFormat format;

  /* frame parsing state */
  gint current_offset;
  gboolean frame_start;
};

struct _GstCAVSVideoParseClass
{
  GstBaseParseClass parent_class;
};

G_END_DECLS
#endif
