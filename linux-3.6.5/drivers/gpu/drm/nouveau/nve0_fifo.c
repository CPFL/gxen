/*
 * Copyright 2010 Red Hat Inc.
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
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_mm.h"
#include "nouveau_fifo.h"

#define NVE0_FIFO_ENGINE_NUM 32

static void nve0_fifo_isr(struct drm_device *);

struct nve0_fifo_engine {
	struct nouveau_gpuobj *playlist[2];
	int cur_playlist;
};

struct nve0_fifo_priv {
	struct nouveau_fifo_priv base;
	struct nve0_fifo_engine engine[NVE0_FIFO_ENGINE_NUM];
	struct {
		struct nouveau_gpuobj *mem;
		struct nouveau_vma bar;
	} user;
	int spoon_nr;
};

struct nve0_fifo_chan {
	struct nouveau_fifo_chan base;
	u32 engine;
};

static void
nve0_fifo_playlist_update(struct drm_device *dev, u32 engine)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *pinstmem = &dev_priv->engine.instmem;
	struct nve0_fifo_priv *priv = nv_engine(dev, NVOBJ_ENGINE_FIFO);
	struct nve0_fifo_engine *peng = &priv->engine[engine];
	struct nouveau_gpuobj *cur;
	u32 match = (engine << 16) | 0x00000001;
	int ret, i, p;

	cur = peng->playlist[peng->cur_playlist];
	if (unlikely(cur == NULL)) {
		ret = nouveau_gpuobj_new(dev, NULL, 0x8000, 0x1000, 0, &cur);
		if (ret) {
			NV_ERROR(dev, "PFIFO: playlist alloc failed\n");
			return;
		}

		peng->playlist[peng->cur_playlist] = cur;
	}

	peng->cur_playlist = !peng->cur_playlist;

	for (i = 0, p = 0; i < priv->base.channels; i++) {
		u32 ctrl = nv_rd32(dev, 0x800004 + (i * 8)) & 0x001f0001;
		if (ctrl != match)
			continue;
		nv_wo32(cur, p + 0, i);
		nv_wo32(cur, p + 4, 0x00000000);
		p += 8;
	}
	pinstmem->flush(dev);

	nv_wr32(dev, 0x002270, cur->vinst >> 12);
	nv_wr32(dev, 0x002274, (engine << 20) | (p >> 3));
	if (!nv_wait(dev, 0x002284 + (engine * 4), 0x00100000, 0x00000000))
		NV_ERROR(dev, "PFIFO: playlist %d update timeout\n", engine);
}

static int
nve0_fifo_context_new(struct nouveau_channel *chan, int engine)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *pinstmem = &dev_priv->engine.instmem;
	struct nve0_fifo_priv *priv = nv_engine(dev, engine);
	struct nve0_fifo_chan *fctx;
	u64 usermem = priv->user.mem->vinst + chan->id * 512;
	u64 ib_virt = chan->pushbuf_base + chan->dma.ib_base * 4;
	int ret = 0, i;

	fctx = chan->engctx[engine] = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return -ENOMEM;

	fctx->engine = 0; /* PGRAPH */

	/* allocate vram for control regs, map into polling area */
	chan->user = ioremap_wc(pci_resource_start(dev->pdev, 1) +
				priv->user.bar.offset + (chan->id * 512), 512);
	if (!chan->user) {
		ret = -ENOMEM;
		goto error;
	}

	for (i = 0; i < 0x100; i += 4)
		nv_wo32(chan->ramin, i, 0x00000000);
	nv_wo32(chan->ramin, 0x08, lower_32_bits(usermem));
	nv_wo32(chan->ramin, 0x0c, upper_32_bits(usermem));
	nv_wo32(chan->ramin, 0x10, 0x0000face);
	nv_wo32(chan->ramin, 0x30, 0xfffff902);
	nv_wo32(chan->ramin, 0x48, lower_32_bits(ib_virt));
	nv_wo32(chan->ramin, 0x4c, drm_order(chan->dma.ib_max + 1) << 16 |
				     upper_32_bits(ib_virt));
	nv_wo32(chan->ramin, 0x84, 0x20400000);
	nv_wo32(chan->ramin, 0x94, 0x30000001);
	nv_wo32(chan->ramin, 0x9c, 0x00000100);
	nv_wo32(chan->ramin, 0xac, 0x0000001f);
	nv_wo32(chan->ramin, 0xe4, 0x00000000);
	nv_wo32(chan->ramin, 0xe8, chan->id);
	nv_wo32(chan->ramin, 0xf8, 0x10003080); /* 0x002310 */
	nv_wo32(chan->ramin, 0xfc, 0x10000010); /* 0x002350 */
	pinstmem->flush(dev);

	nv_wr32(dev, 0x800000 + (chan->id * 8), 0x80000000 |
						(chan->ramin->vinst >> 12));
	nv_mask(dev, 0x800004 + (chan->id * 8), 0x00000400, 0x00000400);
	nve0_fifo_playlist_update(dev, fctx->engine);
	nv_mask(dev, 0x800004 + (chan->id * 8), 0x00000400, 0x00000400);

error:
	if (ret)
		priv->base.base.context_del(chan, engine);
	return ret;
}

static void
nve0_fifo_context_del(struct nouveau_channel *chan, int engine)
{
	struct nve0_fifo_chan *fctx = chan->engctx[engine];
	struct drm_device *dev = chan->dev;

	nv_mask(dev, 0x800004 + (chan->id * 8), 0x00000800, 0x00000800);
	nv_wr32(dev, 0x002634, chan->id);
	if (!nv_wait(dev, 0x0002634, 0xffffffff, chan->id))
		NV_WARN(dev, "0x2634 != chid: 0x%08x\n", nv_rd32(dev, 0x2634));
	nve0_fifo_playlist_update(dev, fctx->engine);
	nv_wr32(dev, 0x800000 + (chan->id * 8), 0x00000000);

	if (chan->user) {
		iounmap(chan->user);
		chan->user = NULL;
	}

	chan->engctx[NVOBJ_ENGINE_FIFO] = NULL;
	kfree(fctx);
}

static int
nve0_fifo_init(struct drm_device *dev, int engine)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nve0_fifo_priv *priv = nv_engine(dev, engine);
	struct nve0_fifo_chan *fctx;
	int i;

	/* reset PFIFO, enable all available PSUBFIFO areas */
	nv_mask(dev, 0x000200, 0x00000100, 0x00000000);
	nv_mask(dev, 0x000200, 0x00000100, 0x00000100);
	nv_wr32(dev, 0x000204, 0xffffffff);

	priv->spoon_nr = hweight32(nv_rd32(dev, 0x000204));
	NV_DEBUG(dev, "PFIFO: %d subfifo(s)\n", priv->spoon_nr);

	/* PSUBFIFO[n] */
	for (i = 0; i < priv->spoon_nr; i++) {
		nv_mask(dev, 0x04013c + (i * 0x2000), 0x10000100, 0x00000000);
		nv_wr32(dev, 0x040108 + (i * 0x2000), 0xffffffff); /* INTR */
		nv_wr32(dev, 0x04010c + (i * 0x2000), 0xfffffeff); /* INTR_EN */
	}

	nv_wr32(dev, 0x002254, 0x10000000 | priv->user.bar.offset >> 12);

	nv_wr32(dev, 0x002a00, 0xffffffff);
	nv_wr32(dev, 0x002100, 0xffffffff);
	nv_wr32(dev, 0x002140, 0xbfffffff);

	/* restore PFIFO context table */
	for (i = 0; i < priv->base.channels; i++) {
		struct nouveau_channel *chan = dev_priv->channels.ptr[i];
		if (!chan || !(fctx = chan->engctx[engine]))
			continue;

		nv_wr32(dev, 0x800000 + (i * 8), 0x80000000 |
						 (chan->ramin->vinst >> 12));
		nv_mask(dev, 0x800004 + (i * 8), 0x00000400, 0x00000400);
		nve0_fifo_playlist_update(dev, fctx->engine);
		nv_mask(dev, 0x800004 + (i * 8), 0x00000400, 0x00000400);
	}

	return 0;
}

static int
nve0_fifo_fini(struct drm_device *dev, int engine, bool suspend)
{
	struct nve0_fifo_priv *priv = nv_engine(dev, engine);
	int i;

	for (i = 0; i < priv->base.channels; i++) {
		if (!(nv_rd32(dev, 0x800004 + (i * 8)) & 1))
			continue;

		nv_mask(dev, 0x800004 + (i * 8), 0x00000800, 0x00000800);
		nv_wr32(dev, 0x002634, i);
		if (!nv_wait(dev, 0x002634, 0xffffffff, i)) {
			NV_INFO(dev, "PFIFO: kick ch %d failed: 0x%08x\n",
				i, nv_rd32(dev, 0x002634));
			return -EBUSY;
		}
	}

	nv_wr32(dev, 0x002140, 0x00000000);
	return 0;
}

struct nouveau_enum nve0_fifo_fault_unit[] = {
	{}
};

struct nouveau_enum nve0_fifo_fault_reason[] = {
	{ 0x00, "PT_NOT_PRESENT" },
	{ 0x01, "PT_TOO_SHORT" },
	{ 0x02, "PAGE_NOT_PRESENT" },
	{ 0x03, "VM_LIMIT_EXCEEDED" },
	{ 0x04, "NO_CHANNEL" },
	{ 0x05, "PAGE_SYSTEM_ONLY" },
	{ 0x06, "PAGE_READ_ONLY" },
	{ 0x0a, "COMPRESSED_SYSRAM" },
	{ 0x0c, "INVALID_STORAGE_TYPE" },
	{}
};

struct nouveau_enum nve0_fifo_fault_hubclient[] = {
	{}
};

struct nouveau_enum nve0_fifo_fault_gpcclient[] = {
	{}
};

struct nouveau_bitfield nve0_fifo_subfifo_intr[] = {
	{ 0x00200000, "ILLEGAL_MTHD" },
	{ 0x00800000, "EMPTY_SUBC" },
	{}
};

static void
nve0_fifo_isr_vm_fault(struct drm_device *dev, int unit)
{
	u32 inst = nv_rd32(dev, 0x2800 + (unit * 0x10));
	u32 valo = nv_rd32(dev, 0x2804 + (unit * 0x10));
	u32 vahi = nv_rd32(dev, 0x2808 + (unit * 0x10));
	u32 stat = nv_rd32(dev, 0x280c + (unit * 0x10));
	u32 client = (stat & 0x00001f00) >> 8;

	NV_INFO(dev, "PFIFO: %s fault at 0x%010llx [",
		(stat & 0x00000080) ? "write" : "read", (u64)vahi << 32 | valo);
	nouveau_enum_print(nve0_fifo_fault_reason, stat & 0x0000000f);
	printk("] from ");
	nouveau_enum_print(nve0_fifo_fault_unit, unit);
	if (stat & 0x00000040) {
		printk("/");
		nouveau_enum_print(nve0_fifo_fault_hubclient, client);
	} else {
		printk("/GPC%d/", (stat & 0x1f000000) >> 24);
		nouveau_enum_print(nve0_fifo_fault_gpcclient, client);
	}
	printk(" on channel 0x%010llx\n", (u64)inst << 12);
}

static int
nve0_fifo_page_flip(struct drm_device *dev, u32 chid)
{
	struct nve0_fifo_priv *priv = nv_engine(dev, NVOBJ_ENGINE_FIFO);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = NULL;
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&dev_priv->channels.lock, flags);
	if (likely(chid >= 0 && chid < priv->base.channels)) {
		chan = dev_priv->channels.ptr[chid];
		if (likely(chan))
			ret = nouveau_finish_page_flip(chan, NULL);
	}
	spin_unlock_irqrestore(&dev_priv->channels.lock, flags);
	return ret;
}

static void
nve0_fifo_isr_subfifo_intr(struct drm_device *dev, int unit)
{
	u32 stat = nv_rd32(dev, 0x040108 + (unit * 0x2000));
	u32 addr = nv_rd32(dev, 0x0400c0 + (unit * 0x2000));
	u32 data = nv_rd32(dev, 0x0400c4 + (unit * 0x2000));
	u32 chid = nv_rd32(dev, 0x040120 + (unit * 0x2000)) & 0x7f;
	u32 subc = (addr & 0x00070000);
	u32 mthd = (addr & 0x00003ffc);
	u32 show = stat;

	if (stat & 0x00200000) {
		if (mthd == 0x0054) {
			if (!nve0_fifo_page_flip(dev, chid))
				show &= ~0x00200000;
		}
	}

	if (show) {
		NV_INFO(dev, "PFIFO%d:", unit);
		nouveau_bitfield_print(nve0_fifo_subfifo_intr, show);
		NV_INFO(dev, "PFIFO%d: ch %d subc %d mthd 0x%04x data 0x%08x\n",
			unit, chid, subc, mthd, data);
	}

	nv_wr32(dev, 0x0400c0 + (unit * 0x2000), 0x80600008);
	nv_wr32(dev, 0x040108 + (unit * 0x2000), stat);
}

static void
nve0_fifo_isr(struct drm_device *dev)
{
	u32 mask = nv_rd32(dev, 0x002140);
	u32 stat = nv_rd32(dev, 0x002100) & mask;

	if (stat & 0x00000100) {
		NV_INFO(dev, "PFIFO: unknown status 0x00000100\n");
		nv_wr32(dev, 0x002100, 0x00000100);
		stat &= ~0x00000100;
	}

	if (stat & 0x10000000) {
		u32 units = nv_rd32(dev, 0x00259c);
		u32 u = units;

		while (u) {
			int i = ffs(u) - 1;
			nve0_fifo_isr_vm_fault(dev, i);
			u &= ~(1 << i);
		}

		nv_wr32(dev, 0x00259c, units);
		stat &= ~0x10000000;
	}

	if (stat & 0x20000000) {
		u32 units = nv_rd32(dev, 0x0025a0);
		u32 u = units;

		while (u) {
			int i = ffs(u) - 1;
			nve0_fifo_isr_subfifo_intr(dev, i);
			u &= ~(1 << i);
		}

		nv_wr32(dev, 0x0025a0, units);
		stat &= ~0x20000000;
	}

	if (stat & 0x40000000) {
		NV_INFO(dev, "PFIFO: unknown status 0x40000000\n");
		nv_mask(dev, 0x002a00, 0x00000000, 0x00000000);
		stat &= ~0x40000000;
	}

	if (stat) {
		NV_INFO(dev, "PFIFO: unhandled status 0x%08x\n", stat);
		nv_wr32(dev, 0x002100, stat);
		nv_wr32(dev, 0x002140, 0);
	}
}

static void
nve0_fifo_destroy(struct drm_device *dev, int engine)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nve0_fifo_priv *priv = nv_engine(dev, engine);
	int i;

	nouveau_vm_put(&priv->user.bar);
	nouveau_gpuobj_ref(NULL, &priv->user.mem);

	for (i = 0; i < NVE0_FIFO_ENGINE_NUM; i++) {
		nouveau_gpuobj_ref(NULL, &priv->engine[i].playlist[0]);
		nouveau_gpuobj_ref(NULL, &priv->engine[i].playlist[1]);
	}

	dev_priv->eng[engine] = NULL;
	kfree(priv);
}

int
nve0_fifo_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nve0_fifo_priv *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.base.destroy = nve0_fifo_destroy;
	priv->base.base.init = nve0_fifo_init;
	priv->base.base.fini = nve0_fifo_fini;
	priv->base.base.context_new = nve0_fifo_context_new;
	priv->base.base.context_del = nve0_fifo_context_del;
	priv->base.channels = 4096;
	dev_priv->eng[NVOBJ_ENGINE_FIFO] = &priv->base.base;

	ret = nouveau_gpuobj_new(dev, NULL, priv->base.channels * 512, 0x1000,
				 NVOBJ_FLAG_ZERO_ALLOC, &priv->user.mem);
	if (ret)
		goto error;

	ret = nouveau_vm_get(dev_priv->bar1_vm, priv->user.mem->size,
			     12, NV_MEM_ACCESS_RW, &priv->user.bar);
	if (ret)
		goto error;

	nouveau_vm_map(&priv->user.bar, *(struct nouveau_mem **)priv->user.mem->node);

	nouveau_irq_register(dev, 8, nve0_fifo_isr);
error:
	if (ret)
		priv->base.base.destroy(dev, NVOBJ_ENGINE_FIFO);
	return ret;
}
