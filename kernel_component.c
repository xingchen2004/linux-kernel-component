
#include "kernel_component.h"

uint8_t component_debug_enable = 2;

static dev_t component_devt = 0;
struct component_device * gdev = NULL;


static int component_device_open(struct inode *inode, struct file *filp)
{
	return 0;

}

static int component_device_release(struct inode *inode, struct file *file)
{
	return 0;

}

static long component_device_ioctl(struct file *file, unsigned int ioctl_num,
				 unsigned long ioctl_param)
{
	return 0;
}

static const struct file_operations fops = {
	.open = component_device_open,
	.release = component_device_release,
	.unlocked_ioctl = component_device_ioctl,
	.compat_ioctl = component_device_ioctl,
};


static int register_chardev_component(struct component_device * dev)
{
	int ret = COMPONENT_SUCESS;

	ret = alloc_chrdev_region(&component_devt, 0, COMPONENT_DEV_MAX, COMPONENT_NAME);
	if (ret < 0) {
		dev_dbgerr("%s %d failed alloc char dev region\n",__func__,__LINE__);
		goto error_free_dev;
	}

	cdev_init(&dev->cdev, &fops);
	dev->cdev.owner = THIS_MODULE;
	ret = cdev_add(&dev->cdev, MKDEV(MAJOR(component_devt), 0), COMPONENT_DEV_MAX);
	if (ret) {
		dev_dbgerr("%s %d failed add char dev \n",__func__,__LINE__);
		goto error_free_region;
	}

	dev->class = class_create(THIS_MODULE, COMPONENT_NAME);
	if (!dev->class)
		goto class_create_bail;

	dev->devices = device_create(dev->class, NULL,
						MKDEV(MAJOR(component_devt), 0),
						NULL, COMPONENT_NAME);
	if (!dev->devices) {		
		goto device_create_bail;
	}

	return ret;
	
device_create_bail:
	device_destroy(dev->class, MKDEV(MAJOR(component_devt), 0));
	class_destroy(dev->class);
class_create_bail:
	cdev_del(&dev->cdev);
error_free_region:
	unregister_chrdev_region(component_devt, COMPONENT_DEV_MAX);
	
error_free_dev:
	return ret;
}

static int component_lowlevel_rw(struct component_device *dev, char * buf, int offset, int size, int d)
{
	int ret = COMPONENT_SUCESS;

	if(!dev->data.data_base || !buf){
		dev_dbgerr("%s %d data base failed\n", __func__, __LINE__);
		return -COMPONENT_FAIL;
	}

	if (offset < 0 || offset >= DATA_BASE_SIZE || (size + offset > DATA_BASE_SIZE)) {
		dev_dbgerr("%s %d data base offset %d wrong\n", __func__, __LINE__, offset);
		return -COMPONENT_FAIL;
	}

	if (size <= 0)
		return ret;
	
	/*
	*d=0 read
	*d=1 write
	*/
	if (d) {
		memcpy(dev->data.data_base+offset, buf, size);
	} else {
		memcpy(buf, dev->data.data_base+offset, size);
	}
	
	return ret;
}

static int component_rw_one_messages(struct component_device *dev, struct component_message *message)
{
	int ret = COMPONENT_SUCESS;
	unsigned long flags;

	spin_lock(&dev->data.data_lock);
	ret = component_lowlevel_rw(dev, message->buf, message->offset, message->size, message->d);
	if (ret) {
		dev_dbgerr("%s %d rw lowlevel failed\n",__func__,__LINE__);
	}
	spin_unlock(&dev->data.data_lock);

	spin_lock_irqsave(&dev->com_queue.queue_lock, flags);
	queue_kthread_work(&dev->com_kth.kworker, &dev->com_kth.rw_messages);
	spin_unlock_irqrestore(&dev->com_queue.queue_lock, flags);

	if (message->complete){
		message->complete(message->context);
	}
	
	return ret;
}

/*pump message and do rw*/
static void component_rw_messages(struct kthread_work *work)
{
	struct component_device *dev = gdev;
	struct component_message *cur_message;
	unsigned long flags;
	int ret =0;
	
	spin_lock_irqsave(&dev->com_queue.queue_lock, flags);

	if(list_empty(&dev->com_queue.message_queue)){
		spin_unlock_irqrestore(&dev->com_queue.queue_lock, flags);
		return;
	}

	cur_message =
		list_first_entry(&dev->com_queue.message_queue, struct component_queue, message_queue);
	if (!cur_message) {
		dev_dbgerr("%s %d no cur message failed\n",__func__,__LINE__);
		spin_unlock_irqrestore(&dev->com_queue.queue_lock, flags);
		return;
	}

	ret = component_rw_one_messages(dev, cur_message);
	if (ret) {
		dev_dbgerr("%s %d rw one message failed\n",__func__,__LINE__);
	}
	
	spin_unlock_irqrestore(&dev->com_queue.queue_lock, flags);
	return 0;
}

/*add message into component queue */
static int component_queue_message(struct component_device *dev, struct component_message *message)
{
	int ret = COMPONENT_SUCESS;
	unsigned long flags;

	if (!dev) {
		dev_dbgerr("%s %d no dev failed\n",__func__,__LINE__);
	}
	
	spin_lock_irqsave(&dev->com_queue.queue_lock, flags);
	
	list_add_tail(&message->queue, &dev->com_queue.message_queue);
	queue_kthread_work(&dev->com_kth.kworker, &dev->com_kth.rw_messages);
	
	spin_unlock_irqrestore(&dev->com_queue.queue_lock, flags);

	return ret;
}

static void component_complete(void *arg)
{
	complete(arg);
}

/*add message into component queue */
static int component_read_write(struct component_device *dev, char * buf, int offset, int size, int d)
{
	int ret = COMPONENT_SUCESS;
	struct component_message message;

	if (!dev || !buf || !size || (offset > DATA_BASE_SIZE) ) {
		return 0;		
	}
	
	message.buf = buf;
	message.offset = offset;
	message.size = size;
	message.d = d;
	
	DECLARE_COMPLETION_ONSTACK(done);
	message.context = &done;
	message.complete = component_complete;

	ret = component_queue_message(dev, &message);
	if (ret == 0) {
		wait_for_completion(&done);
	}
	message.context = NULL;
	
	return ret;
}

static int init_component_kthread(struct component_device * dev)
{
	int ret = COMPONENT_SUCESS;

	if(!dev)
		return -COMPONENT_FAIL;
	
	init_kthread_worker(&dev->com_kth.kworker);
	dev->com_kth.kworker_task = kthread_run(kthread_worker_fn,
					   &dev->com_kth.kworker, "%s",
					   COMPONENT_NAME);
	if (IS_ERR(dev->com_kth.kworker_task)) {
		dev_dbgerr("%s %d init_component_kthread failed\n",__func__,__LINE__);
		return -ENOMEM;
	}
	init_kthread_work(&dev->com_kth.rw_messages, component_rw_messages);

	queue_kthread_work(&dev->com_kth.kworker, &dev->com_kth.rw_messages);

	return ret;
}

static void exit_component_kthread(struct component_device * dev)
{
	flush_kthread_worker(&dev->com_kth.kworker);
	kthread_stop(dev->com_kth.kworker_task);
}

static int __init kernel_component_init(void)
{
	int ret = COMPONENT_SUCESS;
	struct component_device * dev = NULL;

	dev_pinfo("%s %d init start\n",__func__,__LINE__);
	dev = kzalloc(sizeof(struct component_device), GFP_ATOMIC);
	if (!dev) {
		dev_dbgerr("%s %d init malloc dev failed\n",__func__,__LINE__);
		return -COMPONENT_FAIL;
	}

	gdev = dev;
	mutex_init(&dev->mlock);
	
	/*init queue*/
	INIT_LIST_HEAD(&dev->com_queue.message_queue);
	spin_lock_init(&dev->com_queue.queue_lock);
	
	dev->com_ops.read_write = component_read_write;

	/*init data base*/
	dev->data.data_base =  kzalloc(DATA_BASE_SIZE, GFP_ATOMIC);
	if (!dev->data.data_base) {
		dev_dbgerr("%s %d init malloc dev failed\n",__func__,__LINE__);
		goto	error_free_dev;
	}
	dev->data.data_ops.rw_x = component_lowlevel_rw;
	spin_lock_init(&dev->data.data_lock);

	/*init device node*/
	ret = register_chardev_component(dev);
	if (ret) {
		goto	error_free_dev;
	}

	/*init kthread worker*/
	ret = init_component_kthread(dev);
	if (ret) {
		goto	error_free_dev;
	}
	
	dev_pinfo("%s %d init finished\n",__func__,__LINE__);
	return ret;

error_free_dev:
	kfree(dev);
error_free_data:
	kfree(dev->data.data_base);
	
	return ret;
}

static void __exit kernel_component_exit(void)
{
	device_destroy(gdev->class, MKDEV(MAJOR(component_devt), 0));
	class_destroy(gdev->class);
	cdev_del(&gdev->cdev);
	unregister_chrdev_region(component_devt, COMPONENT_DEV_MAX);
	kfree(gdev);
}

module_init(kernel_component_init);
module_exit(kernel_component_exit);
MODULE_DESCRIPTION("kernel component driver");
MODULE_LICENSE("GPL v2");
