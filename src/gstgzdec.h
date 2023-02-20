/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2020 Niels De Graef <niels.degraef@gmail.com>
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

#ifndef __GST_GZDEC_H__
#define __GST_GZDEC_H__

#include "gst/gstbuffer.h"
#include "gst/gstpad.h"
#include <gst/gst.h>
#include <gstreamer-1.0/gst/gstelement.h>
#include <zlib.h>
#include <bzlib.h>

G_BEGIN_DECLS

#define GST_TYPE_GZDEC            (gst_gzdec_get_type())
#define GST_GZDEC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GZDEC,GstGzdec))
#define GST_GZDEC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GZDEC,GstGzdecClass))
#define GST_IS_GZDEC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GZDEC))
#define GST_IS_GZDEC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GZDEC))

typedef struct _GstGzdec GstGzdec;
typedef struct _GstGzdecClass GstGzdecClass;

#define OUT_BUF_SIZE 4096

#define GST_FLOW_EOS GST_FLOW_CUSTOM_SUCCESS

struct _GstGzdec
{
  GstElement  element;

  GstPad    * sinkpad;
  GstPad    * srcpad;

  gboolean    in_progress;
  
  z_stream    zlib_stream;
  bz_stream   bzlib_stream;

  // Private encoding funcs
  gboolean(* init_encoder)(GstGzdec *);
  gboolean(* free_encoder)(GstGzdec *);
  GstFlowReturn(* encode)(GstGzdec *, GstBuffer *, GstBuffer **);
};

struct _GstGzdecClass {
  GstElementClass parent;
};

GType gst_gzdec_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __GST_GZDEC_H__ */
