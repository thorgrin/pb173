#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/completion.h>
#include <linux/wait.h>
#include <linux/kthread.h>

static DEFINE_MUTEX(my_lock);
static char str[10];
/* signal for read */
static DECLARE_COMPLETION(my_comp);
/* signal for thread */
static DECLARE_WAIT_QUEUE_HEAD(my_thread_wait);
/* thread task structure */
static struct task_struct *task;
/* variable to signal to thread that it can work */
static atomic_t my_ready = ATOMIC_INIT(0);

/* thread that rewrites 'a' to 'b' */
static int my_thread(void *data)
{
	int i;
	while (1) {
		/* wait till ready or stop */
		wait_event(my_thread_wait,
			kthread_should_stop() || atomic_read(&my_ready));

		/* check for stop, continue otherwise */
		if (kthread_should_stop())
			break;

		/* lower the ready counter */
		atomic_dec(&my_ready);
		/* lock string */
		mutex_lock(&my_lock);
		/* replace characters */
		for (i = 0; i < strlen(str); i++)
			if (str[i] == 'a')
				str[i] = 'b';
		mutex_unlock(&my_lock);
		/* signal to read */
		complete(&my_comp);
	}
	return 0;
}

static ssize_t my_read(struct file *filp, char __user *buf, size_t count,
		loff_t *off)
{
	ssize_t ret = 0;

	/* when something was read, don't wait */
	mutex_lock(&my_lock);
	if (*off > 0) {
		mutex_unlock(&my_lock);
		return 0;
	}
	mutex_unlock(&my_lock);

	/* wait for signal from the thread */
	if (wait_for_completion_interruptible(&my_comp))
		return -EINTR;

	/* lock string and copy from buffer */
	mutex_lock(&my_lock);
	ret = simple_read_from_buffer(buf, count, off, str, strlen(str));
	mutex_unlock(&my_lock);

	return ret;
}

static ssize_t my_write(struct file *filp, const char __user *buf, size_t count,
		loff_t *off)
{
	ssize_t ret = count;

	mutex_lock(&my_lock);
	if (copy_from_user(str, buf, min_t(size_t, count, sizeof str - 1)))
		ret = -EFAULT;

	str[sizeof str - 1] = 0;
	mutex_unlock(&my_lock);

	/* set the ready variable and signal the thread */
	atomic_inc(&my_ready);
	wake_up(&my_thread_wait);

	return ret;
}

static const struct file_operations my_fops = {
	.owner = THIS_MODULE,
	.read = my_read,
	.write = my_write,
};

static struct miscdevice my_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.fops = &my_fops,
	.name = "my_name",
};

static int my_init(void)
{
	/* start the thread */
	task = kthread_run(&my_thread, NULL, "my_thread");
	if (IS_ERR(task))
		return -EFAULT;

	return misc_register(&my_misc);
}

static void my_exit(void)
{
	/* stop the thread */
	kthread_stop(task);
	misc_deregister(&my_misc);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
