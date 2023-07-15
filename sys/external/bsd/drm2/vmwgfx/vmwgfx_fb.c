/* $NetBSD$ */

/*-
 * Copyright (c) 2023 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/systm.h>

#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>

#include "vmwgfx_drv.h"
#include "vmwgfx_kms.h"
#include "vmwgfxfb.h"

#include <linux/nbsd-namespace.h>

struct vmw_fbdev {
	struct drm_fb_helper	helper; /* must be first */
	struct vmw_private	*vmw_priv;
	struct vmw_framebuffer	*vfb;
};

static size_t
vmw_fb_pitch(struct drm_fb_helper_surface_size *sizes)
{
	return sizes->surface_width * sizes->surface_bpp / 8;
}

/*
 * Allocate a framebuffer in system memory, not VRAM. This is because the
 * vmwgfx driver does not support allocating a framebuffer in VRAM and
 * mapping it in the kernel virtual address space. Instead we have to
 * notify the GPU every time something changes in the buffer.
 */
static int
vmw_fb_create_pinned_object(struct vmw_fbdev *vfbdev,
    const struct drm_mode_fb_cmd2 *mode_cmd, struct vmw_buffer_object **vbo)
{
	struct vmw_private *vmw_priv = vfbdev->vmw_priv;
	int ret;

	ttm_write_lock(&vmw_priv->reservation_sem, false);

	/*
	 * This has to be kmalloc() because vmw_bo_bo_free() frees it with
	 * kfree().
	 */
	struct vmw_buffer_object* const vmw_bo =
	    kmalloc(sizeof(*vmw_bo), GFP_KERNEL);
	if (!vmw_bo) {
		ret = -ENOMEM;
		goto err_unlock;
	}

	const size_t size = mode_cmd->pitches[0] * mode_cmd->height;
	ret = vmw_bo_init(vmw_priv, vmw_bo, size,
			  &vmw_mob_placement,
			  false,
			  &vmw_bo_bo_free);
	if (__predict_false(ret != 0))
		goto err_unlock; /* init frees the buffer on failure */

	ttm_bo_reserve(&vmw_bo->base, false, false, NULL);
	vmw_bo_pin_reserved(vmw_bo, true);
	ttm_bo_unreserve(&vmw_bo->base);

	*vbo = vmw_bo;
	ret = 0;

err_unlock:
	ttm_write_unlock(&vmw_priv->reservation_sem);
	return ret;
}

static int
vmw_fb_create(struct drm_fb_helper *helper,
    struct drm_fb_helper_surface_size *sizes)
{
	struct vmw_fbdev *vfbdev =
		container_of(helper, typeof(*vfbdev), helper);
	struct vmw_private *vmw_priv = vfbdev->vmw_priv;
	int ret;

	const struct drm_mode_fb_cmd2 mode_cmd = {
		.width = sizes->surface_width,
		.height = sizes->surface_height,
		.pitches = { vmw_fb_pitch(sizes) },
		.pixel_format = drm_mode_legacy_fb_format(
		    sizes->surface_bpp, sizes->surface_depth)
	};

	struct vmw_buffer_object *vbo;
	ret = vmw_fb_create_pinned_object(vfbdev, &mode_cmd, &vbo);
	if (ret) {
		DRM_ERROR("failed to create framebuffer object: %d\n", ret);
		return ret;
	}

	vfbdev->vfb = vmw_kms_new_framebuffer(vmw_priv, vbo, NULL, true,
	    &mode_cmd);
	ret = PTR_ERR_OR_ZERO(vfbdev->vfb);
	if (ret) {
		DRM_ERROR("failed to create framebuffer: %d\n", ret);
		goto out;
	}

	struct vmwgfxfb_attach_args vfa = {
		.vfa_fb_helper = helper,
		.vfa_fb_sizes = *sizes,
		.vfa_fb_ptr = vmw_bo_map_and_cache(vbo),
		.vfa_fb_linebytes = mode_cmd.pitches[0]
	};
	KERNEL_LOCK(1, NULL);
	helper->fbdev = config_found(vmw_priv->dev->dev, &vfa, NULL,
	    CFARGS(.iattr = "vmwgfxfbbus"));
	KERNEL_UNLOCK_ONE(NULL);
	if (!helper->fbdev) {
		DRM_ERROR("failed to attach genfb\n");
		goto out;
	}

	/* Setup helper */
	vfbdev->helper.fb = &vfbdev->vfb->base;

	return 0;
out:
	if (vfbdev->helper.fb)
		drm_framebuffer_put(vfbdev->helper.fb);
	if (vbo)
		ttm_bo_put(&vbo->base);
	return ret;
}

static const struct drm_fb_helper_funcs vmw_fb_helper_funcs = {
	.fb_probe = vmw_fb_create,
};

int vmw_fb_init(struct vmw_private *vmw_priv)
{
	int ret;

	struct vmw_fbdev *vfbdev = kzalloc(sizeof(*vfbdev), GFP_KERNEL);
	if (!vfbdev)
		return -ENOMEM;

	vfbdev->vmw_priv = vmw_priv;
	vmw_priv->fbdev = vfbdev;

	drm_fb_helper_prepare(vmw_priv->dev, &vfbdev->helper,
	    &vmw_fb_helper_funcs);

	ret = drm_fb_helper_init(vmw_priv->dev, &vfbdev->helper, 1);
	if (ret)
		goto free;

	ret = drm_fb_helper_single_add_all_connectors(&vfbdev->helper);
	if (ret)
		goto fini;

	ret = drm_fb_helper_initial_config(&vfbdev->helper, 32);
	if (ret)
		goto fini;

	return 0;

fini:
	drm_fb_helper_fini(&vfbdev->helper);
free:
	kfree(vfbdev);
	return ret;
}

int vmw_fb_close(struct vmw_private *vmw_priv)
{
	panic("XXX %s not implemented", __func__);
}

int vmw_fb_off(struct vmw_private *vmw_priv)
{
	panic("XXX %s not implemented", __func__);
}
