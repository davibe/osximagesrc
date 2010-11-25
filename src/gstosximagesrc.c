/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2010  <<user@hostname.org>>
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
 * SECTION:element-osximagesrc
 *
 * FIXME:Describe osximagesrc here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! osximagesrc ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include "gstosximagesrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_osximagesrc_debug);
#define GST_CAT_DEFAULT gst_osximagesrc_debug

enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
);

GST_BOILERPLATE (Gstosximagesrc, gst_osximagesrc, GstPushSrc,
    GST_TYPE_PUSH_SRC);

static gboolean gst_ximage_src_start (GstBaseSrc *basesrc);
static gboolean gst_ximage_src_stop (GstBaseSrc *basesrc);

static gboolean negotiate (GstBaseSrc *basesrc);

static void gst_osximagesrc_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_osximagesrc_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);

void swizzleBitmap (void *data, gint rowBytes, gint height) {
  gint top, bottom;
  void * buffer;
  void * topP;
  void * bottomP;
  void * base;

  top = 0;
  bottom = height - 1;
  base = data;
  buffer = malloc(rowBytes);

  while (top < bottom) {
    topP = (void *) ((top * rowBytes) + (intptr_t)base);
    bottomP = (void *) ((bottom * rowBytes) + (intptr_t)base);
  
    bcopy( topP, buffer, rowBytes );
    bcopy( bottomP, topP, rowBytes );
    bcopy( buffer, bottomP, rowBytes );
  
    ++top;
    --bottom;
  }
  free(buffer);
}

static GstFlowReturn
gst_osximage_src_create (GstPushSrc * bs, GstBuffer **buf)
{
  Gstosximagesrc *s = GST_OSXIMAGESRC (bs);
  void *data;

  if (s->next_frame_time == 0) {
    s->next_frame_time = gst_clock_get_time (GST_ELEMENT_CLOCK (s));
  } else {
    GstClockID clock_id = gst_clock_new_single_shot_id (
        GST_ELEMENT_CLOCK (s), s->next_frame_time
    );
    gst_clock_id_wait(clock_id, NULL);
  }
  CGLSetCurrentContext (s->glContextObj) ;
  glReadBuffer (GL_FRONT);
	data = malloc (s->size);

// this call is different on Intel and PPC becaused of endian issues
// http://developer.apple.com/documentation/MacOSX/Conceptual/universal_binary/universal_binary_tips/chapter_5_section_25.html
  
	glReadPixels ((GLint)s->rect.origin.x, 
      (GLint) s->rect.origin.y, 
      s->rect.size.width, 
      s->rect.size.height,
			GL_BGRA,
#if defined(__BIG_ENDIAN__)
			GL_UNSIGNED_INT_8_8_8_8_REV,
#elif defined(__LITTLE_ENDIAN__)
			GL_UNSIGNED_INT_8_8_8_8,
#else
#error Do not know the endianess of this architecture
#endif
			data);

  glFinish();

	swizzleBitmap (data, s->rect.size.width*4, s->rect.size.height);

	CGLSetCurrentContext (NULL);

  *buf = gst_buffer_new ();
  GST_BUFFER_SIZE (*buf) = s->size;
  GST_BUFFER_MALLOCDATA (*buf) = data;
  GST_BUFFER_DATA (*buf) = data; 

  GST_BUFFER_TIMESTAMP (*buf) = s->next_frame_time - GST_ELEMENT_CAST (s)->base_time;

  s->next_frame_time += GST_SECOND/s->fps;
  return GST_FLOW_OK;
}


/* GObject vmethod implementations */
static void
gst_osximagesrc_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
    "osximagesrc",
    "FIXME:Generic",
    "FIXME:Generic Template Element",
    " <<user@hostname.org>>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
}

/* initialize the osximagesrc's class */
static void
gst_osximagesrc_class_init (GstosximagesrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbase_class;
  GstPushSrcClass *gstpush_class;
  
  gstpush_class = GST_PUSH_SRC_CLASS (klass);
  gobject_class = (GObjectClass *) klass;
  gstbase_class = (GstBaseSrcClass *) klass;

  gobject_class->set_property = gst_osximagesrc_set_property;
  gobject_class->get_property = gst_osximagesrc_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  gstpush_class->create = gst_osximage_src_create;
  gstbase_class->stop = gst_ximage_src_stop;
  gstbase_class->start = gst_ximage_src_start;
  gstbase_class->negotiate = negotiate;
}

static void
gst_osximagesrc_init (Gstosximagesrc * filter,
    GstosximagesrcClass * gclass)
{
  gst_base_src_set_live (GST_BASE_SRC (filter), TRUE);
filter->silent = FALSE;
  filter->fps = 1;
}

static gboolean negotiate (GstBaseSrc *basesrc) {
  Gstosximagesrc *s = GST_OSXIMAGESRC (basesrc);

  GstCaps *caps;
  
  caps = gst_caps_new_simple ("video/x-raw-rgb",
      "bpp", G_TYPE_INT, 32,
      "depth", G_TYPE_INT, 24,
      "endianness", G_TYPE_INT, 4321,
      "red_mask", G_TYPE_INT, 16711680,
      "green_mask", G_TYPE_INT, 65280,
      "blue_mask", G_TYPE_INT, 255,
      "width", G_TYPE_INT, 1280,
      "height", G_TYPE_INT, 800,
      "framerate", GST_TYPE_FRACTION, s->fps, 1,
      NULL);

  assert(GST_IS_CAPS(caps));
  return gst_pad_set_caps(GST_BASE_SRC_CAST(s)->srcpad, caps);
}

static gboolean
gst_ximage_src_start (GstBaseSrc * basesrc)
{
  Gstosximagesrc *s = GST_OSXIMAGESRC (basesrc);

  CGDirectDisplayID display = CGMainDisplayID();
  s->rect = CGDisplayBounds (CGMainDisplayID());
  
  CGLPixelFormatObj pixelFormatObj;
  GLint numPixelFormats;

  CGLPixelFormatAttribute attribs[] =
  {
    kCGLPFAFullScreen,
    kCGLPFADisplayMask,
    0,    /* Display mask bit goes here */
    //kCGLPFADoubleBuffer,
    kCGLPFAColorSize, 32,
    kCGLPFADepthSize, 32,
    0
  };
  attribs[2] = CGDisplayIDToOpenGLDisplayMask (display);
	
  /* Build a full-screen GL context */
  CGLChoosePixelFormat (attribs, &pixelFormatObj, &numPixelFormats);
  CGLCreateContext (pixelFormatObj, NULL, &(s->glContextObj));
  CGLDestroyPixelFormat (pixelFormatObj);
  s->size = s->rect.size.width * s->rect.size.height * 4;
  CGLSetFullScreen (s->glContextObj);

  s->next_frame_time = 0;

  return TRUE;
} 

static gboolean
gst_ximage_src_stop(GstBaseSrc * basesrc)
{
  Gstosximagesrc *s = GST_OSXIMAGESRC (basesrc);

	CGLClearDrawable( s->glContextObj ); // disassociate from full screen
	CGLDestroyContext( s->glContextObj ); // and destroy the context

  return TRUE;
}

static void
gst_osximagesrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstosximagesrc *filter = GST_OSXIMAGESRC (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_osximagesrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstosximagesrc *filter = GST_OSXIMAGESRC (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
osximagesrc_init (GstPlugin * osximagesrc)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template osximagesrc' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_osximagesrc_debug, "osximagesrc",
      0, "Template osximagesrc");

  return gst_element_register (osximagesrc, "osximagesrc", GST_RANK_NONE,
      GST_TYPE_OSXIMAGESRC);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstosximagesrc"
#endif

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "osximagesrc",
    "Template osximagesrc",
    osximagesrc_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)
