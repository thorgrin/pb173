#include <linux/module.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/io.h>

#define MY_SET_LEN _IOW('t', 1, uint32_t)
#define MY_GET_LEN _IOR('t', 2, uint64_t)
#define BUF_SIZE 20971520 /* 20*2^20 */

atomic_t my_len = ATOMIC_INIT(4);
atomic_t my_opened = ATOMIC_INIT(0);
DEFINE_MUTEX(lock);
char *buf;

ssize_t my_read(struct file *filp, char __user *ptr, size_t count, loff_t *off)
{
	int read;

	if (*off >= BUF_SIZE || !count)
		return 0;

	read = (count + *off >= BUF_SIZE) ? BUF_SIZE - *off : count;

	/* lock before reading the buffer */
	mutex_lock(&lock);
	if (copy_to_user(ptr, &buf[*off], read) != 0) {
		mutex_unlock(&lock);
		return -EFAULT;
	}
	/* unlock after reading the buffer */
	mutex_unlock(&lock);

	*off += read;
	return read;
}

ssize_t my_write(struct file *filp, const char __user *ptr, size_t count,
	loff_t *off)
{
	int write;

	if (*off >= BUF_SIZE || !count)
		return 0;

	write = (count + *off >= BUF_SIZE) ? BUF_SIZE - *off : count;

	/* lock lock before writing, we want some consistency... */
	mutex_lock(&lock);
	if (copy_from_user(&buf[*off], ptr, write) != 0) {
		mutex_unlock(&lock);
		return -EFAULT;
	}
	/* no more work with buffer, free the lock */
	mutex_unlock(&lock);

	*off += count;

	printk(KERN_INFO "User wrote: \"%s\"\n", buf);

	return write;
}

int my_open(struct inode *inode, struct file *filp)
{
	if ((filp->f_mode & FMODE_WRITE) != 0 &&
			atomic_add_unless(&my_opened, 1, 1) == 0) {
		printk(KERN_INFO "Device is already opened for writing.\n");
		return -EBUSY;
	}

	printk(KERN_INFO "Device opened.\n");
	return 0;
}

int my_release(struct inode *inode, struct file *filp)
{
	if ((filp->f_mode & FMODE_WRITE) != 0)
		atomic_set(&my_opened, 0);

	printk(KERN_INFO "Closing device.\n");
	return 0;
}

long my_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	uint32_t len;
	/* check command number */
	switch (cmd) {
	case MY_SET_LEN:
		len = (uint32_t) arg;
		if (len > 4 || len < 1)
			return -EINVAL;
		atomic_set(&my_len, len);
	break;
	case MY_GET_LEN:
		/* read my_len atomicaly and work with local copy */
		len = atomic_read(&my_len);
		if (copy_to_user((uint32_t *) arg, &len,
				sizeof(len)) != 0)
			return -EFAULT;
	break;
	default:
		return -EINVAL;
	break;
	}

	return 0;
}

const struct file_operations myfops = {
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
	char str[100], *ptr;

	buf = vzalloc(BUF_SIZE);
	if (buf == NULL)
		return -ENOMEM;

	/* write page virtual and physical address to every page */
	for (ptr = buf; ptr < buf + BUF_SIZE; ptr += PAGE_SIZE) {
		sprintf(str, "%p: %lx\n", ptr,
			(unsigned long) virt_to_phys(ptr));
		strcpy(ptr, str);
	}

	misc_register(&mydevice);
	printk(KERN_INFO "MY_SET_LEN: %u\n", MY_SET_LEN);
	printk(KERN_INFO "MY_GET_LEN: %u\n", MY_GET_LEN);

	return 0;
}

static void my_exit(void)
{
	misc_deregister(&mydevice);

	vfree((void *) buf);
}

module_init(my_init);

module_exit(my_exit);

MODULE_LICENSE("GPL");
