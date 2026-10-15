/*
 * Copyright 2012 Red Hat Inc.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 *
 */

#include "util/format/u_format.h"
#include "util/u_inlines.h"

#include "nv30/nv30_screen.h"
#include "nv30/nv30_context.h"
#include "nv30/nv30_resource.h"
#include "nv30/nv30_transfer.h"

static void
nv30_memory_barrier(struct pipe_context *pipe, unsigned flags)
{
   struct nv30_context *nv30 = nv30_context(pipe);
   int i;

   if (flags & PIPE_BARRIER_MAPPED_BUFFER) {
      for (i = 0; i < nv30->num_vtxbufs; ++i) {
         if (!nv30->vtxbuf[i].buffer.resource)
            continue;
         if (nv30->vtxbuf[i].buffer.resource->flags & PIPE_RESOURCE_FLAG_MAP_PERSISTENT)
            nv30->base.vbo_dirty = true;
      }
   }
}

static struct pipe_resource *
nv30_resource_create(struct pipe_screen *pscreen,
                     const struct pipe_resource *tmpl)
{
   switch (tmpl->target) {
   case PIPE_BUFFER:
      return nouveau_buffer_create(pscreen, tmpl);
   default:
      return nv30_miptree_create(pscreen, tmpl);
   }
}

static void
nv30_resource_destroy(struct pipe_screen *pscreen, struct pipe_resource *res)
{
   if (res->target == PIPE_BUFFER)
      nouveau_buffer_destroy(pscreen, res);
   else
      nv30_miptree_destroy(pscreen, res);
}

static struct pipe_resource *
nv30_resource_from_handle(struct pipe_screen *pscreen,
                          const struct pipe_resource *tmpl,
                          struct winsys_handle *handle,
                          unsigned usage)
{
   if (tmpl->target == PIPE_BUFFER)
      return NULL;
   else
      return nv30_miptree_from_handle(pscreen, tmpl, handle);
}

void
nv30_resource_screen_init(struct pipe_screen *pscreen)
{
   pscreen->resource_create = nv30_resource_create;
   pscreen->resource_from_handle = nv30_resource_from_handle;
   pscreen->resource_get_handle = nv30_miptree_get_handle;
   pscreen->resource_destroy = nv30_resource_destroy;
}

/* PS3: texture uploads stage through GART, a small boot-time reservation (ps3rsx_gart=) that
 * every process shares and that is allocated contiguously. One upload of a 1080p window texture
 * (SDL's desktop-GL renderer streams the whole window, e.g. ClassiCube's launcher) asked for
 * 8100 KiB of it, which often didn't fit: the upload failed and the window kept stale content.
 * So big uploads go in bands of rows of at most 1 MB.
 */
#define NV30_PS3_UPLOAD_BAND (1 << 20)

static void
nv30_texture_subdata(struct pipe_context *pipe, struct pipe_resource *res, unsigned level,
                     unsigned usage, const struct pipe_box *box, const void *data,
                     unsigned stride, uintptr_t layer_stride)
{
   struct nv30_context *nv30 = nv30_context(pipe);
   unsigned bh = util_format_get_blockheight(res->format);
   unsigned rows, y;

   if (!nv30->screen->base.is_ps3 || box->depth != 1 || !stride ||
       (uint64_t)stride * DIV_ROUND_UP(box->height, bh) <= NV30_PS3_UPLOAD_BAND) {
      u_default_texture_subdata(pipe, res, level, usage, box, data, stride, layer_stride);
      return;
   }

   /* rows of blocks per band, in pixel rows */
   rows = MAX2(NV30_PS3_UPLOAD_BAND / stride, 1) * bh;
   for (y = 0; y < box->height; y += rows) {
      struct pipe_box band = *box;

      band.y = box->y + y;
      band.height = MIN2(rows, box->height - y);
      u_default_texture_subdata(pipe, res, level, usage, &band,
                                (const uint8_t *)data + (size_t)(y / bh) * stride,
                                stride, layer_stride);
   }
}

void
nv30_resource_init(struct pipe_context *pipe)
{
   pipe->buffer_map = nouveau_buffer_transfer_map;
   pipe->texture_map = nv30_miptree_transfer_map;
   pipe->transfer_flush_region = nouveau_buffer_transfer_flush_region;
   pipe->buffer_unmap = nouveau_buffer_transfer_unmap;
   pipe->texture_unmap = nv30_miptree_transfer_unmap;
   pipe->buffer_subdata = u_default_buffer_subdata;
   pipe->texture_subdata = nv30_texture_subdata;
   pipe->resource_copy_region = nv30_resource_copy_region;
   pipe->blit = nv30_blit;
   pipe->flush_resource = nv30_flush_resource;
   pipe->memory_barrier = nv30_memory_barrier;
}
