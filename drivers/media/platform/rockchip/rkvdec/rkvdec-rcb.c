// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip video decoder Rows and Cols Buffers manager
 *
 * Copyright (C) 2025 Collabora, Ltd.
 *  Detlev Casanova <detlev.casanova@collabora.com>
 */

#include "rkvdec.h"
#include "rkvdec-rcb.h"

#include <linux/iommu.h>
#include <linux/genalloc.h>
#include <linux/sizes.h>
#include <linux/types.h>

struct rkvdec_rcb_config {
	struct rkvdec_aux_buf *rcb_bufs;
	size_t rcb_count;
	u32 width;
	u32 height;
};

static size_t rkvdec_rcb_size(const struct rcb_size_info *size_info,
			      unsigned int width, unsigned int height)
{
	return size_info->multiplier * (size_info->axis == PIC_HEIGHT ? height : width);
}

dma_addr_t rkvdec_rcb_buf_dma_addr(struct rkvdec_ctx *ctx, int id)
{
	return ctx->core->rcb_config->rcb_bufs[id].dma;
}

size_t rkvdec_rcb_buf_size(struct rkvdec_ctx *ctx, int id)
{
	return ctx->core->rcb_config->rcb_bufs[id].size;
}

int rkvdec_rcb_buf_count(struct rkvdec_ctx *ctx)
{
	return ctx->core->rcb_config->rcb_count;
}

bool rkvdec_rcb_buf_validate_size(struct rkvdec_ctx *ctx)
{
	struct rkvdec_rcb_config *cfg = ctx->core->rcb_config;

	bool ret = cfg && cfg->height >= ctx->decoded_fmt.fmt.pix_mp.height &&
		   cfg->width >= ctx->decoded_fmt.fmt.pix_mp.width;

	if (!ret && cfg) {
		dev_dbg(ctx->core->dev, "RCB size %ux%u -> %ux%u\n", cfg->width, cfg->height,
			ctx->decoded_fmt.fmt.pix_mp.width, ctx->decoded_fmt.fmt.pix_mp.height);
	}

	return ret;
}

void rkvdec_free_rcb(struct rkvdec_dev *rkvdec, struct rkvdec_core *core)
{
	struct rkvdec_rcb_config *cfg = core->rcb_config;
	unsigned long virt_addr;
	int i;

	if (!cfg)
		return;

	for (i = 0; i < cfg->rcb_count; i++) {
		size_t rcb_size = cfg->rcb_bufs[i].size;

		if (!cfg->rcb_bufs[i].cpu)
			continue;

		switch (cfg->rcb_bufs[i].type) {
		case RKVDEC_ALLOC_SRAM:
			virt_addr = (unsigned long)cfg->rcb_bufs[i].cpu;

			if (rkvdec->iommu_global_domain)
				iommu_unmap(rkvdec->iommu_global_domain, virt_addr, rcb_size);
			gen_pool_free(core->sram_pool, virt_addr, rcb_size);
			break;
		case RKVDEC_ALLOC_DMA:
			dma_free_coherent(rkvdec->main_core->dev,
					  rcb_size,
					  cfg->rcb_bufs[i].cpu,
					  cfg->rcb_bufs[i].dma);
			break;
		}
	}

	if (cfg->rcb_bufs)
		devm_kfree(core->dev, cfg->rcb_bufs);

	devm_kfree(core->dev, cfg);

	core->rcb_config = NULL;
}

int rkvdec_allocate_rcb(struct rkvdec_dev *rkvdec, struct rkvdec_core *core,
			u32 width, u32 height,
			const struct rcb_size_info *size_info,
			size_t rcb_count)
{
	int ret, i;
	struct rkvdec_rcb_config *cfg;

	if (!size_info || !rcb_count) {
		core->rcb_config = NULL;
		return 0;
	}

	core->rcb_config = devm_kzalloc(core->dev, sizeof(*core->rcb_config), GFP_KERNEL);
	if (!core->rcb_config)
		return -ENOMEM;

	cfg = core->rcb_config;

	cfg->rcb_bufs = devm_kzalloc(core->dev, sizeof(*cfg->rcb_bufs) * rcb_count, GFP_KERNEL);
	if (!cfg->rcb_bufs) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	cfg->width = width;
	cfg->height = height;

	for (i = 0; i < rcb_count; i++) {
		void *cpu = NULL;
		dma_addr_t dma;
		size_t rcb_size = rkvdec_rcb_size(&size_info[i], width, height);
		enum rkvdec_alloc_type alloc_type = RKVDEC_ALLOC_SRAM;

		/* Try allocating an SRAM buffer */
		if (core->sram_pool) {
			if (rkvdec->iommu_global_domain)
				rcb_size = ALIGN(rcb_size, SZ_4K);

			cpu = gen_pool_dma_zalloc_align(core->sram_pool,
							rcb_size,
							&dma,
							SZ_4K);
		}

		/* If an IOMMU is used, map the SRAM address through it */
		if (cpu && rkvdec->iommu_global_domain) {
			unsigned long virt_addr = (unsigned long)cpu;
			phys_addr_t phys_addr = dma;

			ret = iommu_map(rkvdec->iommu_global_domain, virt_addr, phys_addr,
					rcb_size, IOMMU_READ | IOMMU_WRITE, 0);
			if (ret) {
				gen_pool_free(core->sram_pool,
					      (unsigned long)cpu,
					      rcb_size);
				cpu = NULL;
				goto ram_fallback;
			}

			/*
			 * The registers will be configured with the virtual
			 * address so that it goes through the IOMMU
			 */
			dma = virt_addr;
		}

ram_fallback:
		/* Fallback to RAM */
		if (!cpu) {
			cpu = dma_alloc_coherent(rkvdec->main_core->dev,
						 rcb_size,
						 &dma,
						 GFP_KERNEL);
			alloc_type = RKVDEC_ALLOC_DMA;
		}

		if (!cpu) {
			ret = -ENOMEM;
			goto err_alloc;
		}

		cfg->rcb_bufs[i].cpu = cpu;
		cfg->rcb_bufs[i].dma = dma;
		cfg->rcb_bufs[i].size = rcb_size;
		cfg->rcb_bufs[i].type = alloc_type;

		cfg->rcb_count += 1;
	}

	return 0;

err_alloc:
	rkvdec_free_rcb(rkvdec, core);

	return ret;
}
