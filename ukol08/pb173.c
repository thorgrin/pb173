#include <linux/io.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <linux/interrupt.h>

#define VENDOR 0x18ec
#define DEVICE 0xc058
#define REGION 0

#define INT_ENABLE(addr)	((addr)+0x0044)
#define INT_RAISED(addr)	((addr)+0x0040)
#define INT_RAISE(addr)		((addr)+0x0060)
#define INT_ACK(addr)		((addr)+0x0064)

#define TIMER_MSEC 100

/* pointer to requested region (static, this will work only for one card!) */
void *regionPtr;

static irqreturn_t my_handler(int irq, void *data, struct pt_regs *ptr)
{
	if (!printk_ratelimit()) {
		printk(KERN_INFO "Combo IRQ: offset 0x0040: %x\n",
			readl(INT_RAISED(regionPtr)));
	}

	if (readl(INT_RAISED(regionPtr))) {
		writel(0x1000, INT_ACK(regionPtr));
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static void my_func(unsigned long data);
static DEFINE_TIMER(my_timer, my_func, 0, 30);

/* timer function */
static void my_func(unsigned long data)
{
	writel(0x1000, INT_RAISE(regionPtr));
	mod_timer(&my_timer, jiffies + msecs_to_jiffies(TIMER_MSEC));
}


int my_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{	/* variable to hold read time field */
	u32 time;
	int ret;

	printk(KERN_INFO "Adding driver for device [%.4x:%.4x]\n",
		VENDOR, DEVICE);

	/* enable device */
	if (pci_enable_device(pdev) != 0)
		return -EIO;

	/* request region, undo previous on fail */
	if (pci_request_region(pdev, REGION, "my_driver") != 0) {
		pci_disable_device(pdev);
		return -EIO;
	}

	/* remap region, undo previous actions on fail */
	regionPtr = pci_ioremap_bar(pdev, REGION);
	if (regionPtr == NULL) {
		pci_disable_device(pdev);
		pci_release_region(pdev, REGION);
		return -EIO;
	}

	/* print physical memory address */
	printk(KERN_INFO "Region %i phys addr: %lx\n", REGION,
		(unsigned long) pci_resource_start(pdev, REGION));

	/* read time data */
	time = readl(regionPtr+4);

	/* print formatted time data */
	printk(KERN_INFO "ID & revision: %.8x, %s %.4i/%.2i/%.2i %.2i:%.2i\n",
		readl(regionPtr), "build time (YYYY/MM/DD hh:mm):",
		((time & 0xF0000000) >> 28) + 2000, (time & 0x0F000000) >> 24,
		(time & 0x00FF0000) >> 16, (time & 0x0000FF00) >> 8,
		time & 0x000000FF);


	/* setup IRQ */
	ret = request_irq(pdev->irq, my_handler, IRQF_SHARED, "my_interrupt",
		(void *) regionPtr);
	if (ret != 0) {
		printk(KERN_INFO "Cannot request irq\n");
		pci_disable_device(pdev);
		pci_release_region(pdev, REGION);
	}

	/* allow interrupts in card */
	writel(0x1000, INT_ENABLE(regionPtr));

	/* start the timer */
	mod_timer(&my_timer, jiffies);

	return 0;
}

void my_remove(struct pci_dev *pdev)
{
	/* stop the timer*/
	del_timer_sync(&my_timer);
	/* remove IRQ */
	free_irq(pdev->irq, (void *) regionPtr);

	printk(KERN_INFO "Removing driver for device [%.4x:%.4x]\n",
		VENDOR, DEVICE);

	/* unmap requested region */
	iounmap(regionPtr);
	/* release the region */
	pci_release_region(pdev, REGION);
	/* disable the device */
	pci_disable_device(pdev);
}

struct pci_device_id my_table[] = {
	{PCI_DEVICE(VENDOR, DEVICE)},
	{0,}
};

struct pci_driver my_pci_driver = {
	.name = "my_driver",
	.id_table = my_table,
	.probe = my_probe,
	.remove = my_remove,
};


MODULE_DEVICE_TABLE(pci, my_table);

static int my_init(void)
{
	return pci_register_driver(&my_pci_driver);

}

static void my_exit(void)
{
	pci_unregister_driver(&my_pci_driver);
}


module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
