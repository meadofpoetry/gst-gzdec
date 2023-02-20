/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2023 Eugene Bulavin <eugene.bulavin.se@gmail.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-gzdec
 *
 * gzip bzip2 decoder. Use caps filter for decoding format selection
 * e.g. "application/x-bzip2" ! gzdec will decode bzip streams.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m filesrc location=in.gz ! "application/x-gzip" ! gzdec ! filesrc location=out
 * ]|
 * </refsect2>
 */

#include "gst/base/gstbasetransform.h"
#include "gst/gstallocator.h"
#include "gst/gstbuffer.h"
#include "gst/gstcaps.h"
#include "gst/gstcompat.h"
#include "gst/gstelement.h"
#include "gst/gstevent.h"
#include "gst/gstghostpad.h"
#include "gst/gstinfo.h"
#include "gst/gstmemory.h"
#include "gst/gstpad.h"
#include "gst/gstquery.h"
#include "gst/gststructure.h"
#include <bzlib.h>
#include <pthread.h>
#include <string.h>
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <zlib.h>

#include "gstgzdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_gzdec_debug);
#define GST_CAT_DEFAULT gst_gzdec_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("application/x-gzip;" "application/x-bzip2;")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

#define gst_gzdec_parent_class parent_class
G_DEFINE_TYPE (GstGzdec, gst_gzdec, GST_TYPE_ELEMENT);

GST_ELEMENT_REGISTER_DEFINE (gzdec, "gzdec", GST_RANK_NONE,
    GST_TYPE_GZDEC);

static GstFlowReturn gst_gzdec_chain (GstPad    *pad,
    GstObject *parent, GstBuffer *buf);
static gboolean gst_gzdec_sink_event (GstPad    *pad,
    GstObject *parent, GstEvent  *event);

// Private methods

static gboolean zlib_init_encoder (GstGzdec *);
static gboolean zlib_free_encoder (GstGzdec *);
static GstFlowReturn zlib_encode (GstGzdec * filter,
    GstBuffer * inbuf, GstBuffer ** outbuf);

static gboolean bzlib_init_encoder (GstGzdec *);
static gboolean bzlib_free_encoder (GstGzdec *);
static GstFlowReturn bzlib_encode (GstGzdec * filter,
    GstBuffer * inbuf, GstBuffer ** outbuf);

/* GObject vmethod implementations */

/* initialize the gzdec's class */
static void
gst_gzdec_class_init (GstGzdecClass * klass)
{
  GstElementClass *element_class;

  element_class = (GstElementClass *) klass;

  gst_element_class_set_details_simple (element_class,
      "Gzdec",
      "gzip/bzip2 stream decoder",
      "Decoder capable of unarchiving gzip and bzip2 streams",
      "Eugene Bulavin <eugene.bulavin.se@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */
static void
gst_gzdec_init (GstGzdec * filter)
{
  memset(&filter->zlib_stream, 0, sizeof(filter->zlib_stream));
  memset(&filter->bzlib_stream, 0, sizeof(filter->bzlib_stream));
  
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_gzdec_chain));
  gst_pad_set_event_function(filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_gzdec_sink_event));
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->in_progress  = FALSE;
  // Default encoder is ZLIB
  filter->init_encoder = zlib_init_encoder;
  filter->free_encoder = zlib_free_encoder;
  filter->encode       = zlib_encode;
}

/* GstBaseTransform vmethod implementations */
static GstFlowReturn gst_gzdec_chain (GstPad    *pad,
    GstObject *parent, GstBuffer *buf)
{
  GstGzdec * filter = GST_GZDEC (parent);
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer * outbuf;

  if (G_UNLIKELY(! filter->in_progress)) {
    if (! filter->init_encoder (filter))
      goto not_supported;

    filter->in_progress = TRUE;
  }
  
  ret = filter->encode (filter, buf, &outbuf);
  gst_buffer_unref (buf);

  gst_pad_push(filter->srcpad, outbuf);

  if (ret == GST_FLOW_EOS) {
    filter->free_encoder (filter);
    filter->in_progress = FALSE;
  }
  
  return ret;

 not_supported:
  {
    gst_buffer_unref (buf);
    GST_WARNING_OBJECT (parent, "could not invoke encoder, input unsupported");
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static gboolean gst_gzdec_sink_event (GstPad    *pad,
    GstObject *parent, GstEvent  *event)
{
  gboolean ret;
  GstGzdec *filter = GST_GZDEC (parent);

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      GstStructure *s;

      gst_event_parse_caps (event, &caps);
      s = gst_caps_get_structure (caps, 0);
      
      if (gst_structure_has_name (s, "application/x-gzip")) {
        filter->init_encoder = zlib_init_encoder;
        filter->free_encoder = zlib_free_encoder;
        filter->encode       = zlib_encode;
        ret = TRUE;
        break;
      }

      if (gst_structure_has_name (s, "application/x-bzip2")) {
        filter->init_encoder = bzlib_init_encoder;
        filter->free_encoder = bzlib_free_encoder;
        filter->encode       = bzlib_encode;
        ret = TRUE;
        break;
      }

      ret = FALSE;
      break;
    }
  default:
    ret = gst_pad_event_default (pad, parent, event);
    break;
  }
  return ret;
}
/* Private methods implementation */

static gboolean
zlib_init_encoder (GstGzdec * filter)
{
  // Use system allocator
  filter->zlib_stream.zalloc = Z_NULL;
  filter->zlib_stream.zfree  = Z_NULL;
  filter->zlib_stream.opaque = Z_NULL;
  
#define windowBits 15
#define ENABLE_GZIP 16

  filter->zlib_stream.avail_in = 0;
  filter->zlib_stream.next_in = Z_NULL;
      
  inflateInit2(&filter->zlib_stream, windowBits | ENABLE_GZIP);
  
  return TRUE;
}

static gboolean
zlib_free_encoder (GstGzdec * filter)
{
  inflateEnd(&filter->zlib_stream);
  
  return TRUE;
}

static GstFlowReturn
zlib_encode (GstGzdec * filter,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstMemory * memory_block;
  GstMapInfo map_info_in, map_info_out;
  int zlib_status = Z_OK;
    
  if (! gst_buffer_map(inbuf, &map_info_in, GST_MAP_READ))
    return GST_FLOW_ERROR;

  filter->zlib_stream.avail_in = map_info_in.size;
  filter->zlib_stream.next_in  = map_info_in.data;

  *outbuf = gst_buffer_new();
  if (outbuf == NULL)
    goto no_buffer;
  
  GST_BUFFER_OFFSET (*outbuf) = filter->zlib_stream.total_out;
  gst_buffer_copy_into(inbuf, *outbuf, GST_BUFFER_COPY_METADATA, 0, -1);

  while (filter->zlib_stream.avail_in > 0 && zlib_status != Z_STREAM_END) {

    memory_block = gst_allocator_alloc(NULL, OUT_BUF_SIZE, NULL);

    if (! gst_memory_map(memory_block, &map_info_out, GST_MAP_WRITE))
      goto map_error;

    filter->zlib_stream.avail_out = map_info_out.size;
    filter->zlib_stream.next_out  = map_info_out.data;

    zlib_status = inflate(&filter->zlib_stream, Z_NO_FLUSH);
    switch (zlib_status) {
    case Z_OK:
    case Z_STREAM_END:
    case Z_BUF_ERROR:
      break;
    default:
      inflateEnd(&filter->zlib_stream);
      goto decompress_error;
    }

    gst_memory_unmap(memory_block, &map_info_out);
    gst_memory_resize(memory_block, 0, OUT_BUF_SIZE - filter->zlib_stream.avail_out);
    
    gst_buffer_append_memory(*outbuf, memory_block);
  }

  gst_buffer_unmap(inbuf, &map_info_in);

  if (zlib_status == Z_STREAM_END)
    return GST_FLOW_EOS;
  
  return GST_FLOW_OK;

 no_buffer:
  {
    gst_buffer_unmap(inbuf, &map_info_in);
    GST_WARNING_OBJECT (filter, "could not allocate buffer");
    return ret;
  }

 map_error:
  {
    gst_buffer_unmap(inbuf, &map_info_in);
    GST_WARNING_OBJECT (filter, "could not map memory object");
    return ret;
  }

 decompress_error:
  {
    gst_memory_unmap(memory_block, &map_info_out);
    gst_buffer_unmap(inbuf, &map_info_in);
    GST_WARNING_OBJECT (filter, "could not decompress stream: ZLIB_ERROR(%d)",
         zlib_status);
    return ret;
  }
}

static gboolean
bzlib_init_encoder (GstGzdec * filter)
{
  // Use system allocator
  filter->bzlib_stream.bzalloc = NULL;
  filter->bzlib_stream.bzfree  = NULL;
  filter->bzlib_stream.opaque  = NULL;
  
  filter->bzlib_stream.avail_in = 0;
  filter->bzlib_stream.next_in  = NULL;

  BZ2_bzDecompressInit(&filter->bzlib_stream, 0, 0);
  
  return TRUE;
}

static gboolean
bzlib_free_encoder (GstGzdec * filter)
{
  BZ2_bzDecompressEnd(&filter->bzlib_stream);
  
  return TRUE;
}

static GstFlowReturn
bzlib_encode (GstGzdec * filter,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstMemory * memory_block;
  GstMapInfo map_info_in, map_info_out;
  int bzlib_status = BZ_OK;
    
  if (! gst_buffer_map(inbuf, &map_info_in, GST_MAP_READ))
    return GST_FLOW_ERROR;

  filter->bzlib_stream.avail_in = map_info_in.size;
  filter->bzlib_stream.next_in  = (char*) map_info_in.data;

  *outbuf = gst_buffer_new();
  if (outbuf == NULL)
    goto no_buffer;
  
  GST_BUFFER_OFFSET (*outbuf) = filter->zlib_stream.total_out;
  gst_buffer_copy_into(inbuf, *outbuf, GST_BUFFER_COPY_METADATA, 0, -1);

  while (filter->bzlib_stream.avail_in > 0 && bzlib_status != BZ_STREAM_END) {

    memory_block = gst_allocator_alloc(NULL, OUT_BUF_SIZE, NULL);

    if (! gst_memory_map(memory_block, &map_info_out, GST_MAP_WRITE))
      goto map_error;

    filter->bzlib_stream.avail_out = map_info_out.size;
    filter->bzlib_stream.next_out  = (char*) map_info_out.data;

    bzlib_status = BZ2_bzDecompress (&filter->bzlib_stream);
    if (bzlib_status != BZ_OK && bzlib_status != BZ_STREAM_END) {
      BZ2_bzDecompressEnd(&filter->bzlib_stream);
      ret = GST_FLOW_ERROR;
      goto decompress_error;
    }

    gst_memory_unmap(memory_block, &map_info_out);
    gst_memory_resize(memory_block, 0, OUT_BUF_SIZE - filter->bzlib_stream.avail_out);
    
    gst_buffer_append_memory(*outbuf, memory_block);
  }

  gst_buffer_unmap(inbuf, &map_info_in);

  if (bzlib_status == BZ_STREAM_END)
    return GST_FLOW_EOS;
  
  return GST_FLOW_OK;

 no_buffer:
  {
    gst_buffer_unmap(inbuf, &map_info_in);
    GST_WARNING_OBJECT (filter, "could not allocate buffer");
    return ret;
  }

 map_error:
  {
    gst_buffer_unmap(inbuf, &map_info_in);
    GST_WARNING_OBJECT (filter, "could not map memory object");
    return ret;
  }

 decompress_error:
  {
    gst_memory_unmap(memory_block, &map_info_out);
    gst_buffer_unmap(inbuf, &map_info_in);
    GST_WARNING_OBJECT (filter, "could not decompress stream: BZLIB_ERROR(%d)",
         bzlib_status);
    return ret;
  }
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
gzdec_init (GstPlugin * gzdec)
{
  /* debug category for filtering log messages
   *
   * exchange the string 'Template gzdec' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_gzdec_debug, "gzdec",
      0, "Template gzdec");

  return GST_ELEMENT_REGISTER (gzdec, gzdec);
}

/* PACKAGE: this is usually set by meson depending on some _INIT macro
 * in meson.build and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use meson to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "gzdec"
#endif

/* gstreamer looks for this structure to register gzdecs
 *
 * exchange the string 'Template gzdec' with your gzdec description
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    gzdec,
    "gzdec",
    gzdec_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
