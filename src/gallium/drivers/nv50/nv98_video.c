/*
 * Copyright 2011-2013 Maarten Lankhorst, Ilia Mirkin
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
 */

#include "nv98_video.h"

#include "util/u_sampler.h"
#include "util/u_format.h"

static void
nv98_decoder_decode_bitstream(struct pipe_video_decoder *decoder,
                              struct pipe_video_buffer *video_target,
                              struct pipe_picture_desc *picture,
                              unsigned num_buffers,
                              const void *const *data,
                              const unsigned *num_bytes)
{
   struct nouveau_vp3_decoder *dec = (struct nouveau_vp3_decoder *)decoder;
   struct nouveau_vp3_video_buffer *target = (struct nouveau_vp3_video_buffer *)video_target;
   uint32_t comm_seq = ++dec->fence_seq;
   union pipe_desc desc;

   unsigned vp_caps, is_ref, ret;
   struct nouveau_vp3_video_buffer *refs[16] = {};

   desc.base = picture;

   assert(target->base.buffer_format == PIPE_FORMAT_NV12);

   ret = nv98_decoder_bsp(dec, desc, target, comm_seq,
                          num_buffers, data, num_bytes,
                          &vp_caps, &is_ref, refs);

   /* did we decode bitstream correctly? */
   assert(ret == 2);

   nv98_decoder_vp(dec, desc, target, comm_seq, vp_caps, is_ref, refs);
   nv98_decoder_ppp(dec, desc, target, comm_seq);
}

struct pipe_video_decoder *
nv98_create_decoder(struct pipe_context *context,
                    enum pipe_video_profile profile,
                    enum pipe_video_entrypoint entrypoint,
                    enum pipe_video_chroma_format chroma_format,
                    unsigned width, unsigned height, unsigned max_references,
                    bool chunked_decode)
{
   struct nouveau_screen *screen = &((struct nv50_context *)context)->screen->base;
   struct nouveau_vp3_decoder *dec;
   struct nouveau_pushbuf **push;
   struct nv04_fifo nv04_data = {.vram = 0xbeef0201, .gart = 0xbeef0202};
   union nouveau_bo_config cfg;

   cfg.nv50.tile_mode = 0x20;
   cfg.nv50.memtype = 0x70;

   int ret, i;
   uint32_t codec = 1, ppp_codec = 3;
   uint32_t timeout;
   u32 tmp_size = 0;

   if (getenv("XVMC_VL"))
       return vl_create_decoder(context, profile, entrypoint,
                                chroma_format, width, height,
                                max_references, chunked_decode);

   if (entrypoint != PIPE_VIDEO_ENTRYPOINT_BITSTREAM) {
      debug_printf("%x\n", entrypoint);
      return NULL;
   }

   dec = CALLOC_STRUCT(nouveau_vp3_decoder);
   if (!dec)
      return NULL;
   dec->client = screen->client;
   nouveau_vp3_decoder_init_common(&dec->base);

   dec->bsp_idx = 5;
   dec->vp_idx = 6;
   dec->ppp_idx = 7;

   ret = nouveau_object_new(&screen->device->object, 0,
                            NOUVEAU_FIFO_CHANNEL_CLASS,
                            &nv04_data, sizeof(nv04_data), &dec->channel[0]);

   if (!ret)
      ret = nouveau_pushbuf_new(screen->client, dec->channel[0], 4,
                                32 * 1024, true, &dec->pushbuf[0]);

   for (i = 1; i < 3; ++i) {
      dec->channel[i] = dec->channel[0];
      dec->pushbuf[i] = dec->pushbuf[0];
   }
   push = dec->pushbuf;

   if (!ret)
      ret = nouveau_object_new(dec->channel[0], 0x390b1, 0x85b1, NULL, 0, &dec->bsp);
   if (!ret)
      ret = nouveau_object_new(dec->channel[1], 0x190b2, 0x85b2, NULL, 0, &dec->vp);
   if (!ret)
      ret = nouveau_object_new(dec->channel[2], 0x290b3, 0x85b3, NULL, 0, &dec->ppp);
   if (ret)
      goto fail;

   BEGIN_NV04(push[0], SUBC_BSP(NV01_SUBCHAN_OBJECT), 1);
   PUSH_DATA (push[0], dec->bsp->handle);

   BEGIN_NV04(push[0], SUBC_BSP(0x180), 5);
   for (i = 0; i < 5; i++)
      PUSH_DATA (push[0], nv04_data.vram);

   BEGIN_NV04(push[1], SUBC_VP(NV01_SUBCHAN_OBJECT), 1);
   PUSH_DATA (push[1], dec->vp->handle);

   BEGIN_NV04(push[1], SUBC_VP(0x180), 6);
   for (i = 0; i < 6; i++)
      PUSH_DATA (push[1], nv04_data.vram);

   BEGIN_NV04(push[2], SUBC_PPP(NV01_SUBCHAN_OBJECT), 1);
   PUSH_DATA (push[2], dec->ppp->handle);

   BEGIN_NV04(push[2], SUBC_PPP(0x180), 5);
   for (i = 0; i < 5; i++)
      PUSH_DATA (push[2], nv04_data.vram);

   dec->base.context = context;
   dec->base.profile = profile;
   dec->base.entrypoint = entrypoint;
   dec->base.chroma_format = chroma_format;
   dec->base.width = width;
   dec->base.height = height;
   dec->base.max_references = max_references;
   dec->base.decode_bitstream = nv98_decoder_decode_bitstream;

   for (i = 0; i < NOUVEAU_VP3_VIDEO_QDEPTH && !ret; ++i)
      ret = nouveau_bo_new(screen->device, NOUVEAU_BO_VRAM,
                           0, 1 << 20, NULL, &dec->bsp_bo[i]);
   if (!ret)
      ret = nouveau_bo_new(screen->device, NOUVEAU_BO_VRAM,
                           0x100, 4 << 20, NULL, &dec->inter_bo[0]);
   if (!ret)
      nouveau_bo_ref(dec->inter_bo[0], &dec->inter_bo[1]);
   if (ret)
      goto fail;

   switch (u_reduce_video_profile(profile)) {
   case PIPE_VIDEO_CODEC_MPEG12: {
      codec = 1;
      assert(max_references <= 2);
      break;
   }
   case PIPE_VIDEO_CODEC_MPEG4: {
      codec = 4;
      tmp_size = mb(height)*16 * mb(width)*16;
      assert(max_references <= 2);
      break;
   }
   case PIPE_VIDEO_CODEC_VC1: {
      ppp_codec = codec = 2;
      tmp_size = mb(height)*16 * mb(width)*16;
      assert(max_references <= 2);
      break;
   }
   case PIPE_VIDEO_CODEC_MPEG4_AVC: {
      codec = 3;
      dec->tmp_stride = 16 * mb_half(width) * nouveau_vp3_video_align(height) * 3 / 2;
      tmp_size = dec->tmp_stride * (max_references + 1);
      assert(max_references <= 16);
      break;
   }
   default:
      fprintf(stderr, "invalid codec\n");
      goto fail;
   }

   ret = nouveau_bo_new(screen->device, NOUVEAU_BO_VRAM, 0,
                           0x4000, NULL, &dec->fw_bo);
   if (ret)
      goto fail;

   ret = nouveau_vp3_load_firmware(dec, profile, screen->device->chipset);
   if (ret)
      goto fw_fail;

   if (codec != 3) {
      ret = nouveau_bo_new(screen->device, NOUVEAU_BO_VRAM, 0,
                           0x400, NULL, &dec->bitplane_bo);
      if (ret)
         goto fail;
   }

   dec->ref_stride = mb(width)*16 * (mb_half(height)*32 + nouveau_vp3_video_align(height)/2);
   ret = nouveau_bo_new(screen->device, NOUVEAU_BO_VRAM, 0,
                        dec->ref_stride * (max_references+2) + tmp_size,
                        &cfg, &dec->ref_bo);
   if (ret)
      goto fail;

   timeout = 0;

   BEGIN_NV04(push[0], SUBC_BSP(0x200), 2);
   PUSH_DATA (push[0], codec);
   PUSH_DATA (push[0], timeout);

   BEGIN_NV04(push[1], SUBC_VP(0x200), 2);
   PUSH_DATA (push[1], codec);
   PUSH_DATA (push[1], timeout);

   BEGIN_NV04(push[2], SUBC_PPP(0x200), 2);
   PUSH_DATA (push[2], ppp_codec);
   PUSH_DATA (push[2], timeout);

   ++dec->fence_seq;

#if NOUVEAU_VP3_DEBUG_FENCE
   ret = nouveau_bo_new(screen->device, NOUVEAU_BO_GART|NOUVEAU_BO_MAP,
                        0, 0x1000, NULL, &dec->fence_bo);
   if (ret)
      goto fail;

   nouveau_bo_map(dec->fence_bo, NOUVEAU_BO_RDWR, screen->client);
   dec->fence_map = dec->fence_bo->map;
   dec->fence_map[0] = dec->fence_map[4] = dec->fence_map[8] = 0;
   dec->comm = (struct comm *)(dec->fence_map + (COMM_OFFSET/sizeof(*dec->fence_map)));

   /* So lets test if the fence is working? */
   nouveau_pushbuf_space(push[0], 6, 1, 0);
   PUSH_REFN (push[0], dec->fence_bo, NOUVEAU_BO_GART|NOUVEAU_BO_RDWR);
   BEGIN_NV04(push[0], SUBC_BSP(0x240), 3);
   PUSH_DATAh(push[0], dec->fence_bo->offset);
   PUSH_DATA (push[0], dec->fence_bo->offset);
   PUSH_DATA (push[0], dec->fence_seq);

   BEGIN_NV04(push[0], SUBC_BSP(0x304), 1);
   PUSH_DATA (push[0], 0);
   PUSH_KICK (push[0]);

   nouveau_pushbuf_space(push[1], 6, 1, 0);
   PUSH_REFN (push[1], dec->fence_bo, NOUVEAU_BO_GART|NOUVEAU_BO_RDWR);
   BEGIN_NV04(push[1], SUBC_VP(0x240), 3);
   PUSH_DATAh(push[1], (dec->fence_bo->offset + 0x10));
   PUSH_DATA (push[1], (dec->fence_bo->offset + 0x10));
   PUSH_DATA (push[1], dec->fence_seq);

   BEGIN_NV04(push[1], SUBC_VP(0x304), 1);
   PUSH_DATA (push[1], 0);
   PUSH_KICK (push[1]);

   nouveau_pushbuf_space(push[2], 6, 1, 0);
   PUSH_REFN (push[2], dec->fence_bo, NOUVEAU_BO_GART|NOUVEAU_BO_RDWR);
   BEGIN_NV04(push[2], SUBC_PPP(0x240), 3);
   PUSH_DATAh(push[2], (dec->fence_bo->offset + 0x20));
   PUSH_DATA (push[2], (dec->fence_bo->offset + 0x20));
   PUSH_DATA (push[2], dec->fence_seq);

   BEGIN_NV04(push[2], SUBC_PPP(0x304), 1);
   PUSH_DATA (push[2], 0);
   PUSH_KICK (push[2]);

   usleep(100);
   while (dec->fence_seq > dec->fence_map[0] ||
          dec->fence_seq > dec->fence_map[4] ||
          dec->fence_seq > dec->fence_map[8]) {
      debug_printf("%u: %u %u %u\n", dec->fence_seq, dec->fence_map[0], dec->fence_map[4], dec->fence_map[8]);
      usleep(100);
   }
   debug_printf("%u: %u %u %u\n", dec->fence_seq, dec->fence_map[0], dec->fence_map[4], dec->fence_map[8]);
#endif

   return &dec->base;

fw_fail:
   debug_printf("Cannot create decoder without firmware..\n");
   dec->base.destroy(&dec->base);
   return NULL;

fail:
   debug_printf("Creation failed: %s (%i)\n", strerror(-ret), ret);
   dec->base.destroy(&dec->base);
   return NULL;
}

struct pipe_video_buffer *
nv98_video_buffer_create(struct pipe_context *pipe,
                         const struct pipe_video_buffer *templat)
{
   return nouveau_vp3_video_buffer_create(
         pipe, templat, NV50_RESOURCE_FLAG_VIDEO);
}