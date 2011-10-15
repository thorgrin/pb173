#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

char buff[128];
DEFINE_MUTEX(lock);

static ssize_t my_read(struct file *filp, char __user *buf, size_t count,
		loff_t *off)
{
	int c;

	if (*off >= sizeof(buff) || !count)
		return 0;

	c = (count + *off >= sizeof(buff)) ? sizeof(buff) - *off : count;

	/* lock before reading the buffer */
	mutex_lock(&lock);
	if (copy_to_user(buf, &buff[*off], c) != 0) {
		mutex_unlock(&lock);
		return -EFAULT;
	}
	/* unlock after reading the buffer */
	mutex_unlock(&lock);

	*off += c;
	return c;
}

ssize_t my_write(struct file *file, const char __user *ptr, size_t count,
		 loff_t *off)
{
	int c, i;

	if (*off >= sizeof(buff) || !count)
		return 0;

	/* allow at most 5 characters */
	c = (count > 5) ? 5 : count;


	/* check how much space remains in the buffer */
	if (sizeof(buff) - *off < c)
		c = sizeof(buff) - *off;

	/* critical section: work with buffer */
	mutex_lock(&lock);

	/* copy to buffer one character per iteration */
	for (i = 0; i < c; i++) {
		if (copy_from_user(&buff[*off+i], ptr+i, 1) != 0) {
			/* don't forget to release the lock on error */
			mutex_unlock(&lock);
			return -EFAULT;
		}
		msleep(10);
	}

	*off += c;
	/* unlock after write */
	mutex_unlock(&lock);

	/* debug output */
	printk(KERN_INFO "User wrote: \"%s\"\n", buff);

	return c;
}


static const struct file_operations my_fops_read = {
	.owner = THIS_MODULE,
	.read = my_read,
};

static struct miscdevice my_misc_read = {
	.minor = MISC_DYNAMIC_MINOR,
	.fops = &my_fops_read,
	.name = "mydeviceR",
};

static const struct file_operations my_fops_write = {
	.owner = THIS_MODULE,
	.write = my_write,
};

static struct miscdevice my_misc_write = {
	.minor = MISC_DYNAMIC_MINOR,
	.fops = &my_fops_write,
	.name = "mydeviceW",
};


static int my_init(void)
{
	int err;
	err = misc_register(&my_misc_write);
	if (err != 0)
		return err;
	return misc_register(&my_misc_read);
}

static void my_exit(void)
{
	misc_deregister(&my_misc_write);
	misc_deregister(&my_misc_read);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
