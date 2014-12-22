/* GStreamer Chinese AVS Parser
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

/*
 * Information about stream-format caps field:
 *   unit: one unit per buffer with startcode
 *
 *   unit-frame: unit with startcode, each frame buffer will contain everything
 *       needed to decode a frame, ie picture header and slice unit
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/base/base.h>
#include <gst/video/video.h>
#include "gstcavsvideoparse.h"

#include <string.h>

GST_DEBUG_CATEGORY (avs_video_parse_debug);
#define GST_CAT_DEFAULT avs_video_parse_debug

#define GST_CAVS_VIDEO_PARSE_IS_END_OF_FRAME(unit) \
  ((unit)->type == GST_CAVS_VIDEO_UNIT_VIDEO_SEQUENCE_END || \
  (unit)->type == GST_CAVS_VIDEO_UNIT_I_PICTURE || \
  (unit)->type == GST_CAVS_VIDEO_UNIT_PB_PICTURE || \
  (unit)->type == GST_CAVS_VIDEO_UNIT_VIDEO_EDIT || \
  (unit)->type == GST_CAVS_VIDEO_UNIT_SEQUENCE_HEADER)

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-gst-av-cavs"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-gst-av-cavs, "
        "parsed = (boolean) true, "
        "stream-format= (string) {unit, unit-frame}"));

#define parent_class gst_cavs_video_parse_parent_class
G_DEFINE_TYPE (GstCAVSVideoParse, gst_cavs_video_parse, GST_TYPE_BASE_PARSE);

static gboolean gst_cavs_video_parse_start (GstBaseParse * parse);
static gboolean gst_cavs_video_parse_stop (GstBaseParse * parse);
static void gst_cavs_video_parse_reset (GstCAVSVideoParse * cavsvideoparse);

static GstFlowReturn gst_cavs_video_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize);

static gboolean gst_cavs_video_parse_set_sink_caps (GstBaseParse * parse,
    GstCaps * caps);
static GstCaps *gst_cavs_video_parse_get_sink_caps (GstBaseParse * parse,
    GstCaps * filter);

static void
gst_cavs_video_parse_class_init (GstCAVSVideoParseClass * klass)
{
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (avs_video_parse_debug, "cavsvideoparse", 0,
      "chinese avs video parser element");

  parse_class->start = GST_DEBUG_FUNCPTR (gst_cavs_video_parse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_cavs_video_parse_stop);
  parse_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_cavs_video_parse_handle_frame);
  parse_class->set_sink_caps =
      GST_DEBUG_FUNCPTR (gst_cavs_video_parse_set_sink_caps);
  parse_class->get_sink_caps =
      GST_DEBUG_FUNCPTR (gst_cavs_video_parse_get_sink_caps);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_static_metadata (gstelement_class, "CAVS Video parser",
      "Codec/Parser/Converter/Video",
      "Parse Chinese AVS video streams",
      "Aurélien Zanelli <aurelien.zanelli@darkosphere.fr>");
}

static void
gst_cavs_video_parse_init (GstCAVSVideoParse * cavsvideoparse)
{
  gst_base_parse_set_syncable (GST_BASE_PARSE (cavsvideoparse), TRUE);
  gst_base_parse_set_has_timing_info (GST_BASE_PARSE (cavsvideoparse), FALSE);
  gst_base_parse_set_pts_interpolation (GST_BASE_PARSE (cavsvideoparse), FALSE);

  /* We need at least the startcode */
  gst_base_parse_set_min_frame_size (GST_BASE_PARSE (cavsvideoparse), 4);

  gst_cavs_video_parse_reset (cavsvideoparse);

  GST_PAD_SET_ACCEPT_INTERSECT (GST_BASE_PARSE_SINK_PAD (cavsvideoparse));
}

static void
gst_cavs_video_parse_reset_frame (GstCAVSVideoParse * cavsvideoparse)
{
  cavsvideoparse->current_offset = -1;
  cavsvideoparse->frame_start = FALSE;
}

static void
gst_cavs_video_parse_reset (GstCAVSVideoParse * cavsvideoparse)
{
  cavsvideoparse->profile = -1;
  cavsvideoparse->width = 0;
  cavsvideoparse->height = 0;

  cavsvideoparse->update_caps = TRUE;
  cavsvideoparse->have_seqhdr = FALSE;
  cavsvideoparse->format = GST_CAVS_STREAM_FORMAT_NONE;

  gst_cavs_video_parse_reset_frame (cavsvideoparse);
}

static gboolean
gst_cavs_video_parse_start (GstBaseParse * parse)
{
  GstCAVSVideoParse *cavsvideoparse = GST_CAVS_VIDEO_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "start");
  gst_cavs_video_parse_reset (cavsvideoparse);

  return TRUE;
}

static gboolean
gst_cavs_video_parse_stop (GstBaseParse * parse)
{
  GstCAVSVideoParse *cavsvideoparse = GST_CAVS_VIDEO_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "stop");
  gst_cavs_video_parse_reset (cavsvideoparse);

  return TRUE;
}

static GstFlowReturn
gst_cavs_video_parse_negotiate (GstCAVSVideoParse * cavsvideoparse)
{
  GstCaps *allowed;
  GstStructure *s;
  const gchar *format;

  GST_DEBUG_OBJECT (cavsvideoparse, "negotiate with downstream");

  allowed = gst_pad_get_allowed_caps (GST_BASE_PARSE_SRC_PAD (cavsvideoparse));
  if (!allowed) {
    GST_ERROR_OBJECT (cavsvideoparse, "failed to get allowed caps");
    return GST_FLOW_NOT_LINKED;
  }

  if (gst_caps_is_empty (allowed)) {
    GST_ERROR_OBJECT (cavsvideoparse, "allowed caps is empty");
    gst_caps_unref (allowed);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  allowed = gst_caps_fixate (allowed);

  cavsvideoparse->format = GST_CAVS_STREAM_FORMAT_NONE;

  /* get stream format from downstream caps */
  s = gst_caps_get_structure (allowed, 0);
  format = gst_structure_get_string (s, "stream-format");
  if (format) {
    if (g_strcmp0 (format, "unit") == 0)
      cavsvideoparse->format = GST_CAVS_STREAM_FORMAT_UNIT;
    else if (g_strcmp0 (format, "unit-frame") == 0)
      cavsvideoparse->format = GST_CAVS_STREAM_FORMAT_UNIT_FRAME;
  }

  if (cavsvideoparse->format == GST_CAVS_STREAM_FORMAT_NONE)
    cavsvideoparse->format = GST_CAVS_STREAM_FORMAT_UNIT;

  cavsvideoparse->update_caps = TRUE;

  GST_DEBUG_OBJECT (cavsvideoparse, "downstream allowed caps: %" GST_PTR_FORMAT,
      allowed);

  return GST_FLOW_OK;;
}

static gboolean
gst_cavs_video_parse_update_src_caps (GstCAVSVideoParse * cavsvideoparse)
{
  GstCaps *caps;
  gboolean ret;
  const gchar *level = NULL;

  if (gst_pad_has_current_caps (GST_BASE_PARSE_SRC_PAD (cavsvideoparse))
      && !cavsvideoparse->update_caps)
    return TRUE;

  caps = gst_caps_new_simple ("video/x-gst-av-cavs",
      "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

  g_assert (cavsvideoparse->width != 0);
  g_assert (cavsvideoparse->height != 0);
  gst_caps_set_simple (caps,
      "width", G_TYPE_INT, cavsvideoparse->width,
      "height", G_TYPE_INT, cavsvideoparse->height, NULL);

  if (cavsvideoparse->profile == GST_CAVS_VIDEO_PROFILE_JIZHUN)
    gst_caps_set_simple (caps, "profile", G_TYPE_STRING, "Jizhun", NULL);
  else
    g_assert_not_reached ();

  switch (cavsvideoparse->level) {
    case GST_CAVS_VIDEO_LEVEL_2_0:
      level = "2.0";
      break;
    case GST_CAVS_VIDEO_LEVEL_2_1:
      level = "2.1";
      break;
    case GST_CAVS_VIDEO_LEVEL_4_0:
      level = "4.0";
      break;
    case GST_CAVS_VIDEO_LEVEL_4_2:
      level = "4.2";
      break;
    case GST_CAVS_VIDEO_LEVEL_6_0:
      level = "6.0";
      break;
    case GST_CAVS_VIDEO_LEVEL_6_0_1:
      level = "6.0.1";
      break;
    case GST_CAVS_VIDEO_LEVEL_6_2:
      level = "6.2";
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  gst_caps_set_simple (caps, "level", G_TYPE_STRING, level, NULL);

  g_assert (cavsvideoparse->fps_den != 0);
  gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION,
      cavsvideoparse->fps_num, cavsvideoparse->fps_den, NULL);
  gst_base_parse_set_frame_rate (GST_BASE_PARSE (cavsvideoparse),
      cavsvideoparse->fps_num, cavsvideoparse->fps_den, 0, 0);

  GST_DEBUG_OBJECT (cavsvideoparse, "setting src caps %" GST_PTR_FORMAT, caps);
  ret = gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (cavsvideoparse), caps);
  gst_caps_unref (caps);

  cavsvideoparse->update_caps = FALSE;
  return ret;
}

#ifndef GST_DISABLE_GST_DEBUG
static const gchar *unit_names[] = {
  "Slice",
  "Sequence header",
  "Sequence end",
  "User data",
  "I Picture header",
  "Reserved",
  "Extension",
  "PB Picture header",
  "Video edit",
  "Reserved",
  "System"
};

static const gchar *
_unit_name (GstCAVSVideoUnitType unit_type)
{
  if (unit_type < GST_CAVS_VIDEO_UNIT_SEQUENCE_HEADER)
    return unit_names[0];
  else if (unit_type > GST_CAVS_VIDEO_UNIT_VIDEO_EDIT)
    return unit_names[8];
  else
    return unit_names[unit_type - GST_CAVS_VIDEO_UNIT_SEQUENCE_HEADER + 1];
}
#endif

static gboolean
gst_cavs_video_parse_process_sequence_header (GstCAVSVideoParse *
    cavsvideoparse, GstCAVSVideoUnit * unit)
{
  GstCAVSVideoParserResult pres;

  pres = gst_cavs_video_parser_parse_sequence_header (unit,
      &cavsvideoparse->seqhdr);

  if (pres != GST_CAVS_VIDEO_PARSER_OK) {
    GST_WARNING_OBJECT (cavsvideoparse, "failed to parse sequence header");
    return FALSE;
  }

  if (cavsvideoparse->profile != cavsvideoparse->seqhdr.profile_id) {
    cavsvideoparse->profile = cavsvideoparse->seqhdr.profile_id;
    cavsvideoparse->update_caps = TRUE;
  }

  if (cavsvideoparse->level != cavsvideoparse->seqhdr.level_id) {
    cavsvideoparse->level = cavsvideoparse->seqhdr.level_id;
    cavsvideoparse->update_caps = TRUE;
  }

  if (cavsvideoparse->width != cavsvideoparse->seqhdr.horizontal_size ||
      cavsvideoparse->height != cavsvideoparse->seqhdr.vertical_size) {
    cavsvideoparse->width = cavsvideoparse->seqhdr.horizontal_size;
    cavsvideoparse->height = cavsvideoparse->seqhdr.vertical_size;
    cavsvideoparse->update_caps = TRUE;
    GST_INFO_OBJECT (cavsvideoparse, "definition changed: %ux%u",
        cavsvideoparse->width, cavsvideoparse->height);
  }

  if (cavsvideoparse->fps_num != cavsvideoparse->seqhdr.fps_n ||
      cavsvideoparse->fps_den != cavsvideoparse->seqhdr.fps_d) {
    cavsvideoparse->fps_num = cavsvideoparse->seqhdr.fps_n;
    cavsvideoparse->fps_den = cavsvideoparse->seqhdr.fps_d;
    cavsvideoparse->update_caps = TRUE;
    GST_INFO_OBJECT (cavsvideoparse, "framerate changed: %u/%u",
        cavsvideoparse->fps_num, cavsvideoparse->fps_den);
  }

  cavsvideoparse->have_seqhdr = TRUE;
  return TRUE;
}

static gboolean
gst_cavs_video_parse_process_unit (GstCAVSVideoParse * cavsvideoparse,
    GstCAVSVideoUnit * unit)
{
  gboolean ret = TRUE;
  GstCAVSVideoParserResult pres;

  GST_DEBUG_OBJECT (cavsvideoparse, "processing unit of type 0x%.02x %s, size %"
      G_GSIZE_FORMAT, unit->type, _unit_name (unit->type), unit->size);

  switch (unit->type) {
    case GST_CAVS_VIDEO_UNIT_SEQUENCE_HEADER:
      ret = gst_cavs_video_parse_process_sequence_header (cavsvideoparse, unit);
      break;

    case GST_CAVS_VIDEO_UNIT_VIDEO_SEQUENCE_END:
    case GST_CAVS_VIDEO_UNIT_USER_DATA:
      if (G_UNLIKELY (!cavsvideoparse->have_seqhdr))
        goto no_seqhdr;
      break;

    case GST_CAVS_VIDEO_UNIT_I_PICTURE:
      if (G_UNLIKELY (!cavsvideoparse->have_seqhdr))
        goto no_seqhdr;

      pres = gst_cavs_video_parser_parse_i_picture (unit,
          &cavsvideoparse->seqhdr, &cavsvideoparse->pichdr);
      if (pres != GST_CAVS_VIDEO_PARSER_OK) {
        GST_WARNING_OBJECT (cavsvideoparse, "failed to parse I picture header");
        return FALSE;
      }
      cavsvideoparse->frame_start = TRUE;
      break;

    case GST_CAVS_VIDEO_UNIT_EXTENSION:
      if (G_UNLIKELY (!cavsvideoparse->have_seqhdr))
        goto no_seqhdr;
      break;

    case GST_CAVS_VIDEO_UNIT_PB_PICTURE:
      if (G_UNLIKELY (!cavsvideoparse->have_seqhdr))
        goto no_seqhdr;

      pres = gst_cavs_video_parser_parse_pb_picture (unit,
          &cavsvideoparse->seqhdr, &cavsvideoparse->pichdr);
      if (pres != GST_CAVS_VIDEO_PARSER_OK) {
        GST_WARNING_OBJECT (cavsvideoparse,
            "failed to parse PB picture header");
        return FALSE;
      }
      cavsvideoparse->frame_start = TRUE;
      break;

    case GST_CAVS_VIDEO_UNIT_VIDEO_EDIT:
      break;

    case GST_CAVS_VIDEO_UNIT_SLICE:
      if (G_UNLIKELY (!cavsvideoparse->have_seqhdr))
        goto no_seqhdr;
      break;

    case GST_CAVS_VIDEO_UNIT_SYSTEM:
      GST_DEBUG_OBJECT (cavsvideoparse, "dropping system unit");
      return FALSE;
      break;

    default:
      g_assert_not_reached ();
      break;
  }

  return ret;

no_seqhdr:
  GST_WARNING_OBJECT (cavsvideoparse,
      "No valid sequence header yet, dropping unit of type 0x%.02x %s",
      unit->type, _unit_name (unit->type));
  return FALSE;
}

static GstFlowReturn
gst_cavs_video_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  GstCAVSVideoParse *cavsvideoparse = GST_CAVS_VIDEO_PARSE (parse);
  GstBuffer *buffer = frame->buffer;
  GstMapInfo minfo;
  GstCAVSVideoParserResult pres;
  GstCAVSVideoUnit unit;
  gsize framesize = -1;
  gsize size;
  gint offset;

  *skipsize = 0;

  /* First we set src caps */
  if (gst_pad_check_reconfigure (GST_BASE_PARSE_SRC_PAD (parse))) {
    if (gst_cavs_video_parse_negotiate (cavsvideoparse) != GST_FLOW_OK) {
      GST_ERROR_OBJECT (cavsvideoparse, "Failed to negotiate with downstream");
      return GST_FLOW_NOT_NEGOTIATED;
    }
  }

  if (!gst_buffer_map (buffer, &minfo, GST_MAP_READ)) {
    GST_ERROR_OBJECT (cavsvideoparse, "failed to map buffer");
    return GST_FLOW_ERROR;
  }

  size = minfo.size;

  GST_DEBUG_OBJECT (cavsvideoparse, "handling buffer %p of size %"
      G_GSIZE_FORMAT " at offset %" G_GUINT64_FORMAT, buffer, minfo.size,
      GST_BUFFER_OFFSET (buffer));

  offset = cavsvideoparse->current_offset;
  if (offset < 0)
    offset = 0;

  /* skip initial data before first start code */
  if (cavsvideoparse->current_offset == -1) {
    pres = gst_cavs_video_parser_identify_unit (minfo.data, 0, size, &unit);
    switch (pres) {
      case GST_CAVS_VIDEO_PARSER_OK:
        if (unit.sc_offset > 4) {
          *skipsize = unit.sc_offset;
          goto skip;
        }
        break;
      case GST_CAVS_VIDEO_PARSER_NO_UNIT:
        GST_DEBUG_OBJECT (cavsvideoparse, "found no CAVS unit");
        *skipsize = size - 3;
        goto skip;
      case GST_CAVS_VIDEO_PARSER_NO_UNIT_END:
        break;
      case GST_CAVS_VIDEO_PARSER_ERROR:
        GST_ERROR_OBJECT (cavsvideoparse, "parsing error");
        goto invalid_stream;
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }

  g_assert (*skipsize == 0);
  g_assert (cavsvideoparse->format != GST_CAVS_STREAM_FORMAT_NONE);

  while (TRUE) {
    gboolean finish_frame =
        (cavsvideoparse->format == GST_CAVS_STREAM_FORMAT_UNIT);

    pres = gst_cavs_video_parser_identify_unit (minfo.data, offset, size,
        &unit);
    switch (pres) {
      case GST_CAVS_VIDEO_PARSER_OK:
        GST_DEBUG_OBJECT (cavsvideoparse, "have complete CAVS unit");
        break;
      case GST_CAVS_VIDEO_PARSER_NO_UNIT:
        /* Should really not happen since we have check for an initial unit */
        GST_ERROR_OBJECT (cavsvideoparse, "found no CAVS unit");
        break;
      case GST_CAVS_VIDEO_PARSER_NO_UNIT_END:
        GST_DEBUG_OBJECT (cavsvideoparse, "found no CAVS unit end");
        if (G_UNLIKELY (GST_BASE_PARSE_DRAINING (cavsvideoparse))) {
          GST_DEBUG_OBJECT (cavsvideoparse,
              "draining, assuming complete frame");
          unit.size = size - unit.offset;
          finish_frame = TRUE;
        } else {
          /* need more data */
          goto more;
        }
        break;
      case GST_CAVS_VIDEO_PARSER_ERROR:
        GST_ERROR_OBJECT (cavsvideoparse, "parsing error");
        goto invalid_stream;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    /* Check for complete frame in case we aggregate frame unit */
    if (cavsvideoparse->format == GST_CAVS_STREAM_FORMAT_UNIT_FRAME &&
        cavsvideoparse->frame_start &&
        GST_CAVS_VIDEO_PARSE_IS_END_OF_FRAME (&unit)) {
      GST_LOG_OBJECT (cavsvideoparse, "frame complete");
      framesize = unit.sc_offset;
      break;
    }

    /* Now, we have a complete frame so process CAVS unit */
    if (!gst_cavs_video_parse_process_unit (cavsvideoparse, &unit)) {
      GST_DEBUG_OBJECT (cavsvideoparse, "invalid unit will be dropped");
      frame->flags |= GST_BASE_PARSE_FRAME_FLAG_DROP;
      framesize = unit.offset + unit.size;
      break;
    }

    /* always finish frame in unit stream-format */
    finish_frame |= (cavsvideoparse->format == GST_CAVS_STREAM_FORMAT_UNIT);

    if (cavsvideoparse->format == GST_CAVS_STREAM_FORMAT_UNIT_FRAME)
      finish_frame |= !cavsvideoparse->frame_start;

    if (finish_frame) {
      framesize = unit.offset + unit.size;
      break;
    }

    offset = unit.offset + unit.size;
  }

  gst_buffer_unmap (buffer, &minfo);

  /* Update src caps if needed */
  if (cavsvideoparse->have_seqhdr &&
      !gst_cavs_video_parse_update_src_caps (cavsvideoparse)) {
    return GST_FLOW_NOT_NEGOTIATED;
  }

  GST_DEBUG_OBJECT (cavsvideoparse, "finish frame of size %" G_GSIZE_FORMAT,
      framesize);
  gst_cavs_video_parse_reset_frame (cavsvideoparse);
  return gst_base_parse_finish_frame (parse, frame, framesize);

skip:
  GST_DEBUG_OBJECT (cavsvideoparse, "skipping %d", *skipsize);
  gst_buffer_unmap (buffer, &minfo);
  gst_cavs_video_parse_reset_frame (cavsvideoparse);
  return GST_FLOW_OK;

more:
  GST_DEBUG_OBJECT (cavsvideoparse, "need more data");
  *skipsize = 0;

  /* restore parsing from here */
  if (offset > 0)
    cavsvideoparse->current_offset = offset;
  gst_buffer_unmap (buffer, &minfo);
  return GST_FLOW_OK;

invalid_stream:
  gst_buffer_unmap (buffer, &minfo);
  return GST_FLOW_ERROR;
}

static gboolean
gst_cavs_video_parse_set_sink_caps (GstBaseParse * parse, GstCaps * caps)
{
  GstCAVSVideoParse *cavsvideoparse = GST_CAVS_VIDEO_PARSE (parse);
  GstStructure *s;

  GST_DEBUG_OBJECT (cavsvideoparse, "sink caps %" GST_PTR_FORMAT, caps);

  s = gst_caps_get_structure (caps, 0);

  /* get info from upstream if provided */
  gst_structure_get_int (s, "width", &cavsvideoparse->width);
  gst_structure_get_int (s, "height", &cavsvideoparse->height);
  gst_structure_get_fraction (s, "framerate", &cavsvideoparse->fps_num,
      &cavsvideoparse->fps_den);

  gst_cavs_video_parse_negotiate (cavsvideoparse);

  return TRUE;
}

static void
remove_fields (GstCaps * caps)
{
  guint i, n;

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);

    gst_structure_remove_field (s, "parsed");
  }
}

static GstCaps *
gst_cavs_video_parse_get_sink_caps (GstBaseParse * parse, GstCaps * filter)
{
  GstCaps *peercaps, *templ;
  GstCaps *res;

  templ = gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD (parse));
  if (filter) {
    GstCaps *fcopy = gst_caps_copy (filter);
    /* Remove the fields we convert */
    remove_fields (fcopy);
    peercaps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (parse), fcopy);
    gst_caps_unref (fcopy);
  } else {
    peercaps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (parse), NULL);
  }

  if (peercaps) {
    peercaps = gst_caps_make_writable (peercaps);
    remove_fields (peercaps);

    res = gst_caps_intersect_full (peercaps, templ, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (peercaps);
    gst_caps_unref (templ);
  } else {
    res = templ;
  }

  if (filter) {
    GstCaps *tmp = gst_caps_intersect_full (res, filter,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (res);
    res = tmp;
  }

  return res;
}
