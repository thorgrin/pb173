#include <linux/io.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>

#define VENDOR 0x18ec
#define DEVICE 0xc058
#define REGION 0

/* pointer to requested region */
void *regionPtr;

int my_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{	/* variable to hold read time field */
	u32 time;

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

	return 0;
}

void my_remove(struct pci_dev *pdev)
{
	printk(KERN_INFO "Removing driver for device [%.4x:%.4x]\n",
		VENDOR, DEVICE);

	iounmap(regionPtr);
	pci_release_region(pdev, REGION);
	pci_disable_device(pdev);
}

DEFINE_PCI_DEVICE_TABLE(my_table) = {
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
