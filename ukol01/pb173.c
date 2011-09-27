#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/jiffies.h>

void my_function(void)
{
}

static int my_init(void)
{
	void *p = kmalloc(10, GFP_KERNEL);
	char c;

	if (p)
		printk(KERN_INFO "%p", p);
	kfree(p);

	printk(KERN_INFO "%p", &c);
	printk(KERN_INFO "%p", &jiffies);
	printk(KERN_INFO "%p", &my_function);
	printk(KERN_INFO "%p", &bus_register);
	printk(KERN_INFO "%pF", __builtin_return_address(0));

	return 0;
}

static void my_exit(void)
{
	char *str = kmalloc(100, GFP_KERNEL);

	if (str) {
		strcpy(str, "Bye");
		printk(KERN_INFO "%s", str);
	}
	kfree(str);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
