#ifndef _KERNEL_COMPONENT_H_
#define _KERNEL_COMPONENT_H_

#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/debugfs.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/completion.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/sched/rt.h>
#include <linux/mutex.h>


enum _logout{
	LOG_INFO = 0,
	LOG_DEBUG = 1,	
	LOG_ERROR = 2,
};

enum _errout{
	COMPONENT_SUCESS = 0,
	COMPONENT_FAIL = 1,	
	COMPONENT_READ_FAIL = 2,	
	COMPONENT_WRITE_FAIL = 3,
};


enum _component{
	KTHREAD = 0,
	COMPLETION = 1,	
	WAITQUEUE = 2,	
	WORKQUEUE = 3,
};

extern uint8_t component_debug_enable;
#define dev_dbginfo(fmt, ...) \
	do { \
		if (component_debug_enable >= LOG_DEBUG) { \
			printk(fmt, ##__VA_ARGS__);	  \
		} \
	} while (0)

#define dev_pinfo(fmt, ...) \
	do { \
		if (component_debug_enable >= LOG_INFO) { \
			printk(fmt, ##__VA_ARGS__);   \
		} \
	} while (0)

#define dev_dbgerr(fmt, ...) printk(fmt, ##__VA_ARGS__)

#define COMPONENT_DEV_MAX 1

#define COMPONENT_NAME "kernel_component"

#define DATA_BASE_SIZE 4096

struct component_kthread{
	struct kthread_worker		kworker;
	struct task_struct		*kworker_task;
	struct kthread_work		rw_messages;
};

struct component_completion{
	struct completion     xfer_completion;
};

struct component_queue{
	spinlock_t			queue_lock;
	struct list_head message_queue;
};

struct component_data_ops{
	int (*rw_x)(struct component_device *dev, char * buf, int offset, int size, int d);
};

struct component_data_base{
	spinlock_t	data_lock;
	char * data_base;
	struct component_data_ops data_ops;
};

struct component_message{
	int d;
	int offset;
	int size;
	char * buf;
	void			(*complete)(void *context);
	void			*context;
	struct list_head	queue;
};

struct component_ops{
	int (*read_write)(struct component_device *dev, char * buf, int offset, int size, int d);
};

struct component_device{
	const char * name;
	struct device *devices;
	struct mutex  mlock;
	struct cdev	 cdev;
	struct class *class;
	struct component_kthread com_kth;
	struct component_completion com_completion;
	struct component_queue com_queue;
	struct component_ops com_ops;
	struct component_data_base data;
#if defined(CONFIG_DEBUG_FS)
	struct dentry		*debugfs_dentry;
#endif
};

#endif
