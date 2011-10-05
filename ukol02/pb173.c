#include <linux/module.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

int my_len = 4;

ssize_t my_read(struct file *filp, char __user *ptr, size_t count, loff_t *off)
{
	char ret[] = "Ahoj";
	ret[my_len] = '\0'; /* always terminate the string */

	if (copy_to_user(ptr, ret, my_len+1) != 0)
		return -EFAULT;

	return my_len+1;
}

ssize_t my_write(struct file *filp, const char __user *ptr, size_t count,
	loff_t *off)
{
	char buf[100];
	int read = (count>99)? 99 : count;

	if (copy_from_user(buf, ptr, read) != 0)
		return -EFAULT;

	buf[read] = '\0';

	printk(KERN_INFO "User wrote: \"%s\"\n", buf);

	return read;
}

int my_open(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "File opened\n");
	return 0;
}

int my_release(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "Closing file\n");
	return 0;
}

long my_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	/* check command number */
	if (_IOW('t', 1, int) == cmd) {
		if ((int) arg > 4 || (int) arg < 1)
			return -EINVAL;
		my_len = (int) arg;
	} else if (_IOR('t', 2, int*) == cmd) {
		if (copy_to_user((int*) arg, &my_len, sizeof(my_len)) != 0)
			return -EFAULT;
	} else {
		return -EINVAL;
	}

	return 0;
}

struct file_operations myfops = {
	.owner = THIS_MODULE,
	.read = my_read,
	.open = my_open,
	.write = my_write,
	.release = my_release,
	.unlocked_ioctl = my_ioctl,
};

struct miscdevice mydevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mydevice",
	.fops = &myfops,
};

static int my_init(void)
{
	misc_register(&mydevice);

	return 0;
}

static void my_exit(void)
{
	misc_deregister(&mydevice);
}

module_init(my_init);

module_exit(my_exit);

MODULE_LICENSE("GPL");
