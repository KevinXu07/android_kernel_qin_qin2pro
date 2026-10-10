// SPDX-License-Identifier: GPL-2.0
/* Handle-based ION ABI used by the stock Spreadtrum camera libraries. */
#include <linux/dma-buf.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "ion.h"

struct ion_legacy_client {
	struct mutex lock;
	struct idr handles;
};

struct ion_legacy_allocation {
	size_t len;
	size_t align;
	__u32 heap_id_mask;
	__u32 flags;
	__s32 handle;
};

struct ion_legacy_allocation32 {
	__u32 len;
	__u32 align;
	__u32 heap_id_mask;
	__u32 flags;
	__s32 handle;
};

struct ion_legacy_fd {
	__s32 handle;
	__s32 fd;
};

#define ION_LEGACY_ALLOC _IOWR('I', 0, struct ion_legacy_allocation)
#define ION_LEGACY_ALLOC32 _IOWR('I', 0, struct ion_legacy_allocation32)
#define ION_LEGACY_FREE _IOWR('I', 1, __s32)
#define ION_LEGACY_MAP _IOWR('I', 2, struct ion_legacy_fd)
#define ION_LEGACY_SHARE _IOWR('I', 4, struct ion_legacy_fd)
#define ION_LEGACY_IMPORT _IOWR('I', 5, struct ion_legacy_fd)
#define ION_LEGACY_SYNC _IOWR('I', 7, struct ion_legacy_fd)

int ion_open(struct inode *inode, struct file *file)
{
	struct ion_legacy_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;
	mutex_init(&client->lock);
	idr_init(&client->handles);
	file->private_data = client;
	return 0;
}

int ion_release(struct inode *inode, struct file *file)
{
	struct ion_legacy_client *client = file->private_data;
	struct dma_buf *dmabuf;
	int id;

	idr_for_each_entry(&client->handles, dmabuf, id)
		dma_buf_put(dmabuf);
	idr_destroy(&client->handles);
	kfree(client);
	return 0;
}

/* On success the handle owns the caller's dma-buf reference. */
static int ion_legacy_add(struct ion_legacy_client *client,
			  struct dma_buf *dmabuf)
{
	int id;

	mutex_lock(&client->lock);
	id = idr_alloc(&client->handles, dmabuf, 1, 0, GFP_KERNEL);
	mutex_unlock(&client->lock);
	if (id < 0)
		dma_buf_put(dmabuf);
	return id;
}

static int ion_legacy_free(struct ion_legacy_client *client, int id)
{
	struct dma_buf *dmabuf;

	mutex_lock(&client->lock);
	dmabuf = idr_remove(&client->handles, id);
	mutex_unlock(&client->lock);
	if (!dmabuf)
		return -EINVAL;
	dma_buf_put(dmabuf);
	return 0;
}

bool ion_is_legacy_ioctl(unsigned int cmd)
{
	return cmd == ION_LEGACY_ALLOC || cmd == ION_LEGACY_ALLOC32 ||
		cmd == ION_LEGACY_FREE || cmd == ION_LEGACY_MAP ||
		cmd == ION_LEGACY_SHARE || cmd == ION_LEGACY_IMPORT ||
		cmd == ION_LEGACY_SYNC;
}

long ion_legacy_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ion_legacy_client *client = file->private_data;
	struct ion_legacy_allocation allocation;
	struct ion_legacy_allocation32 allocation32;
	struct ion_legacy_fd data;
	struct dma_buf *dmabuf;
	void __user *user = (void __user *)arg;
	int ret, handle;

	if (cmd == ION_LEGACY_ALLOC || cmd == ION_LEGACY_ALLOC32) {
		memset(&allocation, 0, sizeof(allocation));
		if (cmd == ION_LEGACY_ALLOC32) {
			if (copy_from_user(&allocation32, user, sizeof(allocation32)))
				return -EFAULT;
			allocation.len = allocation32.len;
			allocation.align = allocation32.align;
			allocation.heap_id_mask = allocation32.heap_id_mask;
			allocation.flags = allocation32.flags;
		} else if (copy_from_user(&allocation, user, sizeof(allocation))) {
			return -EFAULT;
		}
		/* Current heaps guarantee page alignment, not larger alignments. */
		if (allocation.align > PAGE_SIZE ||
		    allocation.len > SIZE_MAX - (PAGE_SIZE - 1))
			return -EINVAL;
		dmabuf = ion_new_alloc(allocation.len, allocation.heap_id_mask,
				       allocation.flags);
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);
		handle = ion_legacy_add(client, dmabuf);
		if (handle < 0)
			return handle;
		allocation.handle = handle;
		if (cmd == ION_LEGACY_ALLOC32) {
			allocation32.handle = handle;
			ret = copy_to_user(user, &allocation32, sizeof(allocation32));
		} else {
			ret = copy_to_user(user, &allocation, sizeof(allocation));
		}
		if (ret)
			ion_legacy_free(client, handle);
		return ret ? -EFAULT : 0;
	}

	if (cmd == ION_LEGACY_FREE) {
		if (copy_from_user(&handle, user, sizeof(handle)))
			return -EFAULT;
		return ion_legacy_free(client, handle);
	}
	if (copy_from_user(&data, user, sizeof(data)))
		return -EFAULT;
	if (cmd == ION_LEGACY_IMPORT) {
		dmabuf = dma_buf_get(data.fd);
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);
		data.handle = ion_legacy_add(client, dmabuf);
		if (data.handle < 0)
			return data.handle;
		if (copy_to_user(user, &data, sizeof(data))) {
			ion_legacy_free(client, data.handle);
			return -EFAULT;
		}
		return 0;
	}
	if (cmd == ION_LEGACY_SYNC) {
		dmabuf = dma_buf_get(data.fd);
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);
		ret = dma_buf_begin_cpu_access(dmabuf, DMA_BIDIRECTIONAL);
		if (!ret)
			ret = dma_buf_end_cpu_access(dmabuf, DMA_BIDIRECTIONAL);
		dma_buf_put(dmabuf);
		return ret;
	}
	if (cmd != ION_LEGACY_MAP && cmd != ION_LEGACY_SHARE)
		return -ENOTTY;
	mutex_lock(&client->lock);
	dmabuf = idr_find(&client->handles, data.handle);
	if (dmabuf)
		get_dma_buf(dmabuf);
	mutex_unlock(&client->lock);
	if (!dmabuf)
		return -EINVAL;
	data.fd = dma_buf_fd(dmabuf, O_CLOEXEC);
	if (data.fd < 0) {
		dma_buf_put(dmabuf);
		return data.fd;
	}
	if (copy_to_user(user, &data, sizeof(data))) {
		__close_fd(current->files, data.fd);
		return -EFAULT;
	}
	return 0;
}
