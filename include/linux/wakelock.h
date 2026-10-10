#ifndef _LINUX_WAKELOCK_COMPAT_H
#define _LINUX_WAKELOCK_COMPAT_H

#include <linux/device.h>
#include <linux/pm_wakeup.h>

enum {
	WAKE_LOCK_SUSPEND,
	WAKE_LOCK_IDLE,
};

struct wake_lock {
	struct wakeup_source ws;
};

static inline void wake_lock_init(struct wake_lock *lock, int type,
				  const char *name)
{
	wakeup_source_init(&lock->ws, name);
}

static inline void wake_lock_destroy(struct wake_lock *lock)
{
	wakeup_source_trash(&lock->ws);
}

static inline void wake_lock(struct wake_lock *lock)
{
	__pm_stay_awake(&lock->ws);
}

static inline void wake_lock_timeout(struct wake_lock *lock, long msecs)
{
	__pm_wakeup_event(&lock->ws, msecs_to_jiffies(msecs));
}

static inline void wake_unlock(struct wake_lock *lock)
{
	__pm_relax(&lock->ws);
}

#endif
