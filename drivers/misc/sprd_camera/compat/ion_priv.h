/*
 * Compat shim: sprd_camera 4.4-era ion client/handle API on top of the
 * dma_buf-based ion API in this 4.14 tree. Only what sprd_camera uses.
 *
 * Old model:   ion_client -> ion_handle -> struct ion_buffer
 * New model:   ion_new_alloc() returns struct dma_buf *, and
 *              dmabuf->priv is the real struct ion_buffer.
 */
#ifndef _SPRD_CAMERA_COMPAT_ION_PRIV_H_
#define _SPRD_CAMERA_COMPAT_ION_PRIV_H_

#include <linux/dma-buf.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include "ion.h"
#include <linux/sprd_ion.h>

struct ion_client {
	atomic_t refcount;
	char name[32];
};

struct ion_handle {
	struct dma_buf *dmabuf;
};

static inline struct ion_client *sprd_ion_client_create(const char *name)
{
	struct ion_client *c = kzalloc(sizeof(*c), GFP_KERNEL);

	if (!c)
		return NULL;
	atomic_set(&c->refcount, 1);
	if (name)
		strlcpy(c->name, name, sizeof(c->name));
	return c;
}

static inline struct ion_client *sprd_ion_client_get(unsigned long fd)
{
	return sprd_ion_client_create(NULL);
}

static inline void sprd_ion_client_put(struct ion_client *client)
{
	if (client && atomic_dec_and_test(&client->refcount))
		kfree(client);
}

static inline void ion_client_destroy(struct ion_client *client)
{
	sprd_ion_client_put(client);
}

static inline struct ion_handle *cam_compat_ion_alloc(
		struct ion_client *client, size_t len,
		unsigned long align, unsigned int heap_id_mask,
		unsigned int flags)
{
	struct ion_handle *handle;
	struct dma_buf *dmabuf;

	dmabuf = ion_new_alloc(len, heap_id_mask, flags);
	if (IS_ERR(dmabuf))
		return (struct ion_handle *)dmabuf;
	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle) {
		dma_buf_put(dmabuf);
		return ERR_PTR(-ENOMEM);
	}
	handle->dmabuf = dmabuf;
	return handle;
}
#define ion_alloc(client, len, align, heap_id_mask, flags) \
	cam_compat_ion_alloc(client, len, align, heap_id_mask, flags)

static inline void cam_compat_ion_free(struct ion_client *client,
		struct ion_handle *handle)
{
	if (!handle)
		return;
	if (handle->dmabuf)
		dma_buf_put(handle->dmabuf);
	kfree(handle);
}
#define ion_free(client, handle) cam_compat_ion_free(client, handle)

static inline struct ion_buffer *ion_handle_buffer(struct ion_handle *handle)
{
	if (!handle || !handle->dmabuf)
		return NULL;
	return handle->dmabuf->priv;
}

static inline void *cam_compat_ion_map_kernel(struct ion_client *client,
		struct ion_handle *handle)
{
	if (!handle || !handle->dmabuf)
		return ERR_PTR(-EINVAL);
	return sprd_ion_map_kernel(handle->dmabuf, 0);
}
#define ion_map_kernel(client, handle) cam_compat_ion_map_kernel(client, handle)

static inline int cam_compat_ion_unmap_kernel(struct ion_client *client,
		struct ion_handle *handle)
{
	if (!handle || !handle->dmabuf)
		return -EINVAL;
	return sprd_ion_unmap_kernel(handle->dmabuf, 0);
}
#define ion_unmap_kernel(client, handle) \
	cam_compat_ion_unmap_kernel(client, handle)

static inline int cam_compat_ion_phys(struct ion_client *client,
		struct ion_handle *handle, unsigned long *phys_addr,
		size_t *size)
{
	if (!handle || !handle->dmabuf)
		return -EINVAL;
	return sprd_ion_get_phys_addr_by_db(handle->dmabuf, phys_addr, size);
}
#define ion_phys(client, handle, addr, size) \
	cam_compat_ion_phys(client, handle, addr, size)

static inline struct sg_table *ion_sg_table(struct ion_client *client,
		struct ion_handle *handle)
{
	struct ion_buffer *buf = ion_handle_buffer(handle);

	return buf ? buf->sg_table : NULL;
}

#endif
