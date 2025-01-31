/**
 * \file drm_irq.c
 * IRQ support
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Fri Mar 19 14:30:16 1999 by faith@valinux.com
 *
 * Copyright 1999, 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"

#include <linux/interrupt.h>	/* For task queue support */

/**
 * Get interrupt from bus id.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_irq_busid structure.
 * \return zero on success or a negative number on failure.
 *
 * Finds the PCI device with the specified bus id and gets its IRQ number.
 * This IOCTL is deprecated, and will now return EINVAL for any busid not equal
 * to that of the device that this DRM instance attached to.
 */
int drm_irq_by_busid(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct drm_irq_busid *p = data;

	if (!drm_core_check_feature(dev, DRIVER_HAVE_IRQ))
		return -EINVAL;

	if ((p->busnum >> 8) != drm_get_pci_domain(dev) ||
	    (p->busnum & 0xff) != dev->pdev->bus->number ||
	    p->devnum != PCI_SLOT(dev->pdev->devfn) || p->funcnum != PCI_FUNC(dev->pdev->devfn))
		return -EINVAL;

	p->irq = dev->pdev->irq;

	DRM_DEBUG("%d:%d:%d => IRQ %d\n", p->busnum, p->devnum, p->funcnum,
		  p->irq);

	return 0;
}

static void vblank_disable_fn(unsigned long arg)
{
	struct drm_device *dev = (struct drm_device *)arg;
	unsigned long irqflags;
	int i;

	if (!dev->vblank_disable_allowed)
		return;

	for (i = 0; i < dev->num_crtcs; i++) {
		spin_lock_irqsave(&dev->vbl_lock, irqflags);
		if (atomic_read(&dev->vblank_refcount[i]) == 0 &&
		    dev->vblank_enabled[i]) {
			DRM_DEBUG("disabling vblank on crtc %d\n", i);
			dev->last_vblank[i] =
				dev->driver->get_vblank_counter(dev, i);
			dev->driver->disable_vblank(dev, i);
			dev->vblank_enabled[i] = 0;
		}
		spin_unlock_irqrestore(&dev->vbl_lock, irqflags);
	}
}

void drm_vblank_cleanup(struct drm_device *dev)
{
	/* Bail if the driver didn't call drm_vblank_init() */
	if (dev->num_crtcs == 0)
		return;

	del_timer(&dev->vblank_disable_timer);

	vblank_disable_fn((unsigned long)dev);

	drm_free(dev->vbl_queue, sizeof(*dev->vbl_queue) * dev->num_crtcs,
		 DRM_MEM_DRIVER);
	drm_free(dev->vbl_sigs, sizeof(*dev->vbl_sigs) * dev->num_crtcs,
		 DRM_MEM_DRIVER);
	drm_free(dev->_vblank_count, sizeof(*dev->_vblank_count) *
		 dev->num_crtcs, DRM_MEM_DRIVER);
	drm_free(dev->vblank_refcount, sizeof(*dev->vblank_refcount) *
		 dev->num_crtcs, DRM_MEM_DRIVER);
	drm_free(dev->vblank_enabled, sizeof(*dev->vblank_enabled) *
		 dev->num_crtcs, DRM_MEM_DRIVER);
	drm_free(dev->last_vblank, sizeof(*dev->last_vblank) * dev->num_crtcs,
		 DRM_MEM_DRIVER);
	drm_free(dev->last_vblank_wait,
		 sizeof(*dev->last_vblank_wait) * dev->num_crtcs,
		 DRM_MEM_DRIVER);
	drm_free(dev->vblank_inmodeset, sizeof(*dev->vblank_inmodeset) *
		 dev->num_crtcs, DRM_MEM_DRIVER);

	dev->num_crtcs = 0;
}

int drm_vblank_init(struct drm_device *dev, int num_crtcs)
{
	int i, ret = -ENOMEM;

	setup_timer(&dev->vblank_disable_timer, vblank_disable_fn,
		    (unsigned long)dev);
	spin_lock_init(&dev->vbl_lock);
	atomic_set(&dev->vbl_signal_pending, 0);
	dev->num_crtcs = num_crtcs;

	dev->vbl_queue = drm_alloc(sizeof(wait_queue_head_t) * num_crtcs,
				   DRM_MEM_DRIVER);
	if (!dev->vbl_queue)
		goto err;

	dev->vbl_sigs = drm_alloc(sizeof(struct list_head) * num_crtcs,
				  DRM_MEM_DRIVER);
	if (!dev->vbl_sigs)
		goto err;

	dev->_vblank_count = drm_alloc(sizeof(atomic_t) * num_crtcs,
				      DRM_MEM_DRIVER);
	if (!dev->_vblank_count)
		goto err;

	dev->vblank_refcount = drm_alloc(sizeof(atomic_t) * num_crtcs,
					 DRM_MEM_DRIVER);
	if (!dev->vblank_refcount)
		goto err;

	dev->vblank_enabled = drm_calloc(num_crtcs, sizeof(int),
					 DRM_MEM_DRIVER);
	if (!dev->vblank_enabled)
		goto err;

	dev->last_vblank = drm_calloc(num_crtcs, sizeof(u32), DRM_MEM_DRIVER);
	if (!dev->last_vblank)
		goto err;

	dev->last_vblank_wait = drm_calloc(num_crtcs, sizeof(u32),
					   DRM_MEM_DRIVER);
	if (!dev->last_vblank_wait)
		goto err;

	dev->vblank_inmodeset = drm_calloc(num_crtcs, sizeof(int),
					 DRM_MEM_DRIVER);
	if (!dev->vblank_inmodeset)
		goto err;

	/* Zero per-crtc vblank stuff */
	for (i = 0; i < num_crtcs; i++) {
		init_waitqueue_head(&dev->vbl_queue[i]);
		INIT_LIST_HEAD(&dev->vbl_sigs[i]);
		atomic_set(&dev->_vblank_count[i], 0);
		atomic_set(&dev->vblank_refcount[i], 0);
	}

	dev->vblank_disable_allowed = 0;

	return 0;

err:
	drm_vblank_cleanup(dev);
	return ret;
}
EXPORT_SYMBOL(drm_vblank_init);

/**
 * Install IRQ handler.
 *
 * \param dev DRM device.
 *
 * Initializes the IRQ related data. Installs the handler, calling the driver
 * \c drm_driver_irq_preinstall() and \c drm_driver_irq_postinstall() functions
 * before and after the installation.
 */
int drm_irq_install(struct drm_device *dev)
{
	int ret = 0;
	unsigned long sh_flags = 0;

	if (!drm_core_check_feature(dev, DRIVER_HAVE_IRQ))
		return -EINVAL;

	if (dev->pdev->irq == 0)
		return -EINVAL;

	mutex_lock(&dev->struct_mutex);

	/* Driver must have been initialized */
	if (!dev->dev_private) {
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	if (dev->irq_enabled) {
		mutex_unlock(&dev->struct_mutex);
		return -EBUSY;
	}
	dev->irq_enabled = 1;
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG("irq=%d\n", dev->pdev->irq);

	/* Before installing handler */
	dev->driver->irq_preinstall(dev);

	/* Install handler */
	if (drm_core_check_feature(dev, DRIVER_IRQ_SHARED))
		sh_flags = IRQF_SHARED;

	ret = request_irq(drm_dev_to_irq(dev), dev->driver->irq_handler,
			  sh_flags, dev->devname, dev);

	if (ret < 0) {
		mutex_lock(&dev->struct_mutex);
		dev->irq_enabled = 0;
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	/* After installing handler */
	ret = dev->driver->irq_postinstall(dev);
	if (ret < 0) {
		mutex_lock(&dev->struct_mutex);
		dev->irq_enabled = 0;
		mutex_unlock(&dev->struct_mutex);
	}

	return ret;
}
EXPORT_SYMBOL(drm_irq_install);

/**
 * Uninstall the IRQ handler.
 *
 * \param dev DRM device.
 *
 * Calls the driver's \c drm_driver_irq_uninstall() function, and stops the irq.
 */
int drm_irq_uninstall(struct drm_device * dev)
{
	unsigned long irqflags;
	int irq_enabled, i;

	if (!drm_core_check_feature(dev, DRIVER_HAVE_IRQ))
		return -EINVAL;

	mutex_lock(&dev->struct_mutex);
	irq_enabled = dev->irq_enabled;
	dev->irq_enabled = 0;
	mutex_unlock(&dev->struct_mutex);

	/*
	 * Wake up any waiters so they don't hang.
	 */
	spin_lock_irqsave(&dev->vbl_lock, irqflags);
	for (i = 0; i < dev->num_crtcs; i++) {
		DRM_WAKEUP(&dev->vbl_queue[i]);
		dev->vblank_enabled[i] = 0;
	}
	spin_unlock_irqrestore(&dev->vbl_lock, irqflags);

	if (!irq_enabled)
		return -EINVAL;

	DRM_DEBUG("irq=%d\n", dev->pdev->irq);

	dev->driver->irq_uninstall(dev);

	free_irq(dev->pdev->irq, dev);

	return 0;
}
EXPORT_SYMBOL(drm_irq_uninstall);

/**
 * IRQ control ioctl.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_control structure.
 * \return zero on success or a negative number on failure.
 *
 * Calls irq_install() or irq_uninstall() according to \p arg.
 */
int drm_control(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_control *ctl = data;

	/* if we haven't irq we fallback for compatibility reasons - this used to be a separate function in drm_dma.h */


	switch (ctl->func) {
	case DRM_INST_HANDLER:
		if (!drm_core_check_feature(dev, DRIVER_HAVE_IRQ))
			return 0;
		if (drm_core_check_feature(dev, DRIVER_MODESET))
			return 0;
		if (dev->if_version < DRM_IF_VERSION(1, 2) &&
		    ctl->irq != dev->pdev->irq)
			return -EINVAL;
		return drm_irq_install(dev);
	case DRM_UNINST_HANDLER:
		if (!drm_core_check_feature(dev, DRIVER_HAVE_IRQ))
			return 0;
		if (drm_core_check_feature(dev, DRIVER_MODESET))
			return 0;
		return drm_irq_uninstall(dev);
	default:
		return -EINVAL;
	}
}

/**
 * drm_vblank_count - retrieve "cooked" vblank counter value
 * @dev: DRM device
 * @crtc: which counter to retrieve
 *
 * Fetches the "cooked" vblank count value that represents the number of
 * vblank events since the system was booted, including lost events due to
 * modesetting activity.
 */
u32 drm_vblank_count(struct drm_device *dev, int crtc)
{
	return atomic_read(&dev->_vblank_count[crtc]);
}
EXPORT_SYMBOL(drm_vblank_count);

/**
 * drm_update_vblank_count - update the master vblank counter
 * @dev: DRM device
 * @crtc: counter to update
 *
 * Call back into the driver to update the appropriate vblank counter
 * (specified by @crtc).  Deal with wraparound, if it occurred, and
 * update the last read value so we can deal with wraparound on the next
 * call if necessary.
 *
 * Only necessary when going from off->on, to account for frames we
 * didn't get an interrupt for.
 *
 * Note: caller must hold dev->vbl_lock since this reads & writes
 * device vblank fields.
 */
static void drm_update_vblank_count(struct drm_device *dev, int crtc)
{
	u32 cur_vblank, diff;

	/*
	 * Interrupts were disabled prior to this call, so deal with counter
	 * wrap if needed.
	 * NOTE!  It's possible we lost a full dev->max_vblank_count events
	 * here if the register is small or we had vblank interrupts off for
	 * a long time.
	 */
	cur_vblank = dev->driver->get_vblank_counter(dev, crtc);
	diff = cur_vblank - dev->last_vblank[crtc];
	if (cur_vblank < dev->last_vblank[crtc]) {
		diff += dev->max_vblank_count;

		DRM_DEBUG("last_vblank[%d]=0x%x, cur_vblank=0x%x => diff=0x%x\n",
			  crtc, dev->last_vblank[crtc], cur_vblank, diff);
	}

	DRM_DEBUG("enabling vblank interrupts on crtc %d, missed %d\n",
		  crtc, diff);

	atomic_add(diff, &dev->_vblank_count[crtc]);
}

/**
 * drm_vblank_get - get a reference count on vblank events
 * @dev: DRM device
 * @crtc: which CRTC to own
 *
 * Acquire a reference count on vblank events to avoid having them disabled
 * while in use.
 *
 * RETURNS
 * Zero on success, nonzero on failure.
 */
int drm_vblank_get(struct drm_device *dev, int crtc)
{
	unsigned long irqflags;
	int ret = 0;

	spin_lock_irqsave(&dev->vbl_lock, irqflags);
	/* Going from 0->1 means we have to enable interrupts again */
	if (atomic_add_return(1, &dev->vblank_refcount[crtc]) == 1 &&
	    !dev->vblank_enabled[crtc]) {
		ret = dev->driver->enable_vblank(dev, crtc);
		DRM_DEBUG("enabling vblank on crtc %d, ret: %d\n", crtc, ret);
		if (ret)
			atomic_dec(&dev->vblank_refcount[crtc]);
		else {
			dev->vblank_enabled[crtc] = 1;
			drm_update_vblank_count(dev, crtc);
		}
	}
	spin_unlock_irqrestore(&dev->vbl_lock, irqflags);

	return ret;
}
EXPORT_SYMBOL(drm_vblank_get);

/**
 * drm_vblank_put - give up ownership of vblank events
 * @dev: DRM device
 * @crtc: which counter to give up
 *
 * Release ownership of a given vblank counter, turning off interrupts
 * if possible.
 */
void drm_vblank_put(struct drm_device *dev, int crtc)
{
	/* Last user schedules interrupt disable */
	if (atomic_dec_and_test(&dev->vblank_refcount[crtc]))
		mod_timer(&dev->vblank_disable_timer, jiffies + 5*DRM_HZ);
}
EXPORT_SYMBOL(drm_vblank_put);

/**
 * drm_vblank_pre_modeset - account for vblanks across mode sets
 * @dev: DRM device
 * @crtc: CRTC in question
 * @post: post or pre mode set?
 *
 * Account for vblank events across mode setting events, which will likely
 * reset the hardware frame counter.
 */
void drm_vblank_pre_modeset(struct drm_device *dev, int crtc)
{
	/*
	 * To avoid all the problems that might happen if interrupts
	 * were enabled/disabled around or between these calls, we just
	 * have the kernel take a reference on the CRTC (just once though
	 * to avoid corrupting the count if multiple, mismatch calls occur),
	 * so that interrupts remain enabled in the interim.
	 */
	if (!dev->vblank_inmodeset[crtc]) {
		dev->vblank_inmodeset[crtc] = 1;
		drm_vblank_get(dev, crtc);
	}
}
EXPORT_SYMBOL(drm_vblank_pre_modeset);

void drm_vblank_post_modeset(struct drm_device *dev, int crtc)
{
	unsigned long irqflags;

	if (dev->vblank_inmodeset[crtc]) {
		spin_lock_irqsave(&dev->vbl_lock, irqflags);
		dev->vblank_disable_allowed = 1;
		dev->vblank_inmodeset[crtc] = 0;
		spin_unlock_irqrestore(&dev->vbl_lock, irqflags);
		drm_vblank_put(dev, crtc);
	}
}
EXPORT_SYMBOL(drm_vblank_post_modeset);

/**
 * drm_modeset_ctl - handle vblank event counter changes across mode switch
 * @DRM_IOCTL_ARGS: standard ioctl arguments
 *
 * Applications should call the %_DRM_PRE_MODESET and %_DRM_POST_MODESET
 * ioctls around modesetting so that any lost vblank events are accounted for.
 *
 * Generally the counter will reset across mode sets.  If interrupts are
 * enabled around this call, we don't have to do anything since the counter
 * will have already been incremented.
 */
int drm_modeset_ctl(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct drm_modeset_ctl *modeset = data;
	int crtc, ret = 0;

	/* If drm_vblank_init() hasn't been called yet, just no-op */
	if (!dev->num_crtcs)
		goto out;

	crtc = modeset->crtc;
	if (crtc >= dev->num_crtcs) {
		ret = -EINVAL;
		goto out;
	}

	switch (modeset->cmd) {
	case _DRM_PRE_MODESET:
		drm_vblank_pre_modeset(dev, crtc);
		break;
	case _DRM_POST_MODESET:
		drm_vblank_post_modeset(dev, crtc);
		break;
	default:
		ret = -EINVAL;
		break;
	}

out:
	return ret;
}

/**
 * Wait for VBLANK.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param data user argument, pointing to a drm_wait_vblank structure.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the IRQ is installed.
 *
 * If a signal is requested checks if this task has already scheduled the same signal
 * for the same vblank sequence number - nothing to be done in
 * that case. If the number of tasks waiting for the interrupt exceeds 100 the
 * function fails. Otherwise adds a new entry to drm_device::vbl_sigs for this
 * task.
 *
 * If a signal is not requested, then calls vblank_wait().
 */
int drm_wait_vblank(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	union drm_wait_vblank *vblwait = data;
	int ret = 0;
	unsigned int flags, seq, crtc;

	if ((!dev->pdev->irq) || (!dev->irq_enabled))
		return -EINVAL;

	if (vblwait->request.type &
	    ~(_DRM_VBLANK_TYPES_MASK | _DRM_VBLANK_FLAGS_MASK)) {
		DRM_ERROR("Unsupported type value 0x%x, supported mask 0x%x\n",
			  vblwait->request.type,
			  (_DRM_VBLANK_TYPES_MASK | _DRM_VBLANK_FLAGS_MASK));
		return -EINVAL;
	}

	flags = vblwait->request.type & _DRM_VBLANK_FLAGS_MASK;
	crtc = flags & _DRM_VBLANK_SECONDARY ? 1 : 0;

	if (crtc >= dev->num_crtcs)
		return -EINVAL;

	ret = drm_vblank_get(dev, crtc);
	if (ret) {
		DRM_ERROR("failed to acquire vblank counter, %d\n", ret);
		return ret;
	}
	seq = drm_vblank_count(dev, crtc);

	switch (vblwait->request.type & _DRM_VBLANK_TYPES_MASK) {
	case _DRM_VBLANK_RELATIVE:
		vblwait->request.sequence += seq;
		vblwait->request.type &= ~_DRM_VBLANK_RELATIVE;
	case _DRM_VBLANK_ABSOLUTE:
		break;
	default:
		ret = -EINVAL;
		goto done;
	}

	if ((flags & _DRM_VBLANK_NEXTONMISS) &&
	    (seq - vblwait->request.sequence) <= (1<<23)) {
		vblwait->request.sequence = seq + 1;
	}

	if (flags & _DRM_VBLANK_SIGNAL) {
		unsigned long irqflags;
		struct list_head *vbl_sigs = &dev->vbl_sigs[crtc];
		struct drm_vbl_sig *vbl_sig;

		spin_lock_irqsave(&dev->vbl_lock, irqflags);

		/* Check if this task has already scheduled the same signal
		 * for the same vblank sequence number; nothing to be done in
		 * that case
		 */
		list_for_each_entry(vbl_sig, vbl_sigs, head) {
			if (vbl_sig->sequence == vblwait->request.sequence
			    && vbl_sig->info.si_signo ==
			    vblwait->request.signal
			    && vbl_sig->task == current) {
				spin_unlock_irqrestore(&dev->vbl_lock,
						       irqflags);
				vblwait->reply.sequence = seq;
				goto done;
			}
		}

		if (atomic_read(&dev->vbl_signal_pending) >= 100) {
			spin_unlock_irqrestore(&dev->vbl_lock, irqflags);
			ret = -EBUSY;
			goto done;
		}

		spin_unlock_irqrestore(&dev->vbl_lock, irqflags);

		vbl_sig = drm_calloc(1, sizeof(struct drm_vbl_sig),
				     DRM_MEM_DRIVER);
		if (!vbl_sig) {
			ret = -ENOMEM;
			goto done;
		}

		/* Get a refcount on the vblank, which will be released by
		 * drm_vbl_send_signals().
		 */
		ret = drm_vblank_get(dev, crtc);
		if (ret) {
			drm_free(vbl_sig, sizeof(struct drm_vbl_sig),
				 DRM_MEM_DRIVER);
			goto done;
		}

		atomic_inc(&dev->vbl_signal_pending);

		vbl_sig->sequence = vblwait->request.sequence;
		vbl_sig->info.si_signo = vblwait->request.signal;
		vbl_sig->task = current;

		spin_lock_irqsave(&dev->vbl_lock, irqflags);

		list_add_tail(&vbl_sig->head, vbl_sigs);

		spin_unlock_irqrestore(&dev->vbl_lock, irqflags);

		vblwait->reply.sequence = seq;
	} else {
		DRM_DEBUG("waiting on vblank count %d, crtc %d\n",
			  vblwait->request.sequence, crtc);
		dev->last_vblank_wait[crtc] = vblwait->request.sequence;
		DRM_WAIT_ON(ret, dev->vbl_queue[crtc], 3 * DRM_HZ,
			    (((drm_vblank_count(dev, crtc) -
			       vblwait->request.sequence) <= (1 << 23)) ||
			     !dev->irq_enabled));

		if (ret != -EINTR) {
			struct timeval now;

			do_gettimeofday(&now);

			vblwait->reply.tval_sec = now.tv_sec;
			vblwait->reply.tval_usec = now.tv_usec;
			vblwait->reply.sequence = drm_vblank_count(dev, crtc);
			DRM_DEBUG("returning %d to client\n",
				  vblwait->reply.sequence);
		} else {
			DRM_DEBUG("vblank wait interrupted by signal\n");
		}
	}

done:
	drm_vblank_put(dev, crtc);
	return ret;
}

/**
 * Send the VBLANK signals.
 *
 * \param dev DRM device.
 * \param crtc CRTC where the vblank event occurred
 *
 * Sends a signal for each task in drm_device::vbl_sigs and empties the list.
 *
 * If a signal is not requested, then calls vblank_wait().
 */
static void drm_vbl_send_signals(struct drm_device *dev, int crtc)
{
	struct drm_vbl_sig *vbl_sig, *tmp;
	struct list_head *vbl_sigs;
	unsigned int vbl_seq;
	unsigned long flags;

	spin_lock_irqsave(&dev->vbl_lock, flags);

	vbl_sigs = &dev->vbl_sigs[crtc];
	vbl_seq = drm_vblank_count(dev, crtc);

	list_for_each_entry_safe(vbl_sig, tmp, vbl_sigs, head) {
	    if ((vbl_seq - vbl_sig->sequence) <= (1 << 23)) {
		vbl_sig->info.si_code = vbl_seq;
		send_sig_info(vbl_sig->info.si_signo,
			      &vbl_sig->info, vbl_sig->task);

		list_del(&vbl_sig->head);

		drm_free(vbl_sig, sizeof(*vbl_sig),
			 DRM_MEM_DRIVER);
		atomic_dec(&dev->vbl_signal_pending);
		drm_vblank_put(dev, crtc);
	    }
	}

	spin_unlock_irqrestore(&dev->vbl_lock, flags);
}

/**
 * drm_handle_vblank - handle a vblank event
 * @dev: DRM device
 * @crtc: where this event occurred
 *
 * Drivers should call this routine in their vblank interrupt handlers to
 * update the vblank counter and send any signals that may be pending.
 */
void drm_handle_vblank(struct drm_device *dev, int crtc)
{
	atomic_inc(&dev->_vblank_count[crtc]);
	DRM_WAKEUP(&dev->vbl_queue[crtc]);
	drm_vbl_send_signals(dev, crtc);
}
EXPORT_SYMBOL(drm_handle_vblank);
