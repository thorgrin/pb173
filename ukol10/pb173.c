#include <linux/io.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>

#define VENDOR 0x18ec
#define DEVICE 0xc058
#define REGION 0

#define INT_ENABLE(addr)	((addr)+0x0044)
#define INT_RAISED(addr)	((addr)+0x0040)
#define INT_RAISE(addr)		((addr)+0x0060)
#define INT_ACK(addr)		((addr)+0x0064)

#define DMA_SRC(addr)		((addr)+0x0080)
#define DMA_DST(addr)		((addr)+0x0084)
#define DMA_COUNT(addr)		((addr)+0x0088)
#define DMA_CMD(addr)		((addr)+0x008c)

#define TIMER_MSEC 100

/* pointer to requested region (static, this will work only for one card!) */
struct holder {
	void *regionPtr;
	dma_addr_t phys;
	void *virt;
	struct tasklet_struct *tasklet;
};

/* store the pointer to virt memory for misc device */
void *miscPtr;

static irqreturn_t my_handler(int irq, void *data, struct pt_regs *ptr)
{
	u32 intr = 0;
	struct holder *holder = (struct holder *) data;

	if (printk_ratelimit()) {
		printk(KERN_INFO "Combo IRQ: offset 0x0040: %x\n",
			readl(INT_RAISED(holder->regionPtr)));
	}

	/* read which interrupt arrived */
	intr = readl(INT_RAISED(holder->regionPtr));
	switch (intr) {
	case 0x0100:
		writel(0x1 << 31, DMA_CMD(holder->regionPtr));
		writel(intr, INT_ACK(holder->regionPtr));
		/* schedule the tasklet to write out the output */
		tasklet_schedule(holder->tasklet);
		return IRQ_HANDLED;
	default: return IRQ_NONE;
	}
}

void tasklet_func(unsigned long data)
{
	printk(KERN_INFO "%s\n", ((char *) data)+20);
}

struct page *my_nopage(struct vm_area_struct *vma, unsigned long address,
	int *type)
{
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	struct page *page = NULL;
	int offset_page;

	offset += address - vma->vm_start;
	offset_page = offset / PAGE_SIZE;

	if (offset_page == 0)
		page = virt_to_page(miscPtr);

	if (!page)
		return NOPAGE_SIGBUS;

	get_page(page);

	if (type)
		*type = VM_FAULT_MINOR;

	return page;
}

struct vm_operations_struct vos = {
	.nopage = &my_nopage,
};

int my_mmap(struct file *filp, struct vm_area_struct *vma)
{
	vma->vm_ops = &vos;
	return 0;
}


static const struct file_operations my_fops = {
	.owner = THIS_MODULE,
	.mmap = my_mmap,
};

static struct miscdevice my_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.fops = &my_fops,
	.name = "my_device",
};


int my_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{	/* variable to hold read time field */
	u32 time;
	int ret;
	struct holder *holder;

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

	/* allocate structure to hold others */
	holder = kmalloc(sizeof(struct holder), GFP_KERNEL);
	if (holder == NULL) {
		pci_disable_device(pdev);
		pci_release_region(pdev, REGION);
		return -EIO;
	}

	/* create new tasklet */
	holder->tasklet = kmalloc(sizeof(struct tasklet_struct), GFP_KERNEL);
	if (holder->tasklet == NULL) {
		kfree(holder);
		pci_disable_device(pdev);
		pci_release_region(pdev, REGION);
		return -EIO;
	}

	/* remap region, undo previous actions on fail */
	holder->regionPtr = pci_ioremap_bar(pdev, REGION);
	if (holder->regionPtr == NULL) {
		kfree(holder->tasklet);
		kfree(holder);
		pci_disable_device(pdev);
		pci_release_region(pdev, REGION);
		return -EIO;
	}

	/* set local data for this device */
	pci_set_drvdata(pdev, (void *) holder);

	/* print physical memory address */
	printk(KERN_INFO "Region %i phys addr: %lx\n", REGION,
		(unsigned long) pci_resource_start(pdev, REGION));

	/* read time data */
	time = readl(holder->regionPtr+4);

	/* print formatted time data */
	printk(KERN_INFO "ID & revision: %.8x, %s %.4i/%.2i/%.2i %.2i:%.2i\n",
		readl(holder->regionPtr), "build time (YYYY/MM/DD hh:mm):",
		((time & 0xF0000000) >> 28) + 2000, (time & 0x0F000000) >> 24,
		(time & 0x00FF0000) >> 16, (time & 0x0000FF00) >> 8,
		time & 0x000000FF);


	/* setup IRQ */
	ret = request_irq(pdev->irq, my_handler, IRQF_SHARED, "my_interrupt",
		(void *) holder);
	if (ret != 0) {
		printk(KERN_INFO "Cannot request irq\n");
		kfree(holder->tasklet);
		kfree(holder);
		pci_disable_device(pdev);
		pci_release_region(pdev, REGION);
		return -EIO;
	}

	/* setup DMA */
	pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	pci_set_master(pdev);
	holder->virt = dma_alloc_coherent(&pdev->dev, PAGE_SIZE, &holder->phys,
		GFP_KERNEL);

	/* initialise tasklet (we have the virt memory pointer now) */
	tasklet_init(holder->tasklet, &tasklet_func,
		(unsigned long) holder->virt);

	/* register the device that allows to mmap the memory */
	/* let the device access the memory: won't work with multiple devs */
	miscPtr = holder->virt;
	ret = misc_register(&my_misc);
	if (ret != 0) {
		dma_free_coherent(&pdev->dev, PAGE_SIZE, holder->virt,
			holder->phys);
		kfree(holder->tasklet);
		kfree(holder);
		pci_disable_device(pdev);
		pci_release_region(pdev, REGION);
		return ret;
	}

	/* allow interrupts in card */
	writel(0x1000|0x0100, INT_ENABLE(holder->regionPtr));

	/* copy to card */
	strcpy(holder->virt, "retezec10b");
	writel(holder->phys, DMA_SRC(holder->regionPtr));
	writel(0x40000, DMA_DST(holder->regionPtr));
	writel(10, DMA_COUNT(holder->regionPtr));
	writel(0x1 | (0x1 << 7) | (0x2 << 1) | (0x4 << 4),
		DMA_CMD(holder->regionPtr));
	/* wait for the transfer to finish */
	while (readl(DMA_CMD(holder->regionPtr)) & 0x01)
		;

	/* copy from card */
	writel(0x40000, DMA_SRC(holder->regionPtr));
	writel(holder->phys+10, DMA_DST(holder->regionPtr));
	writel(10, DMA_COUNT(holder->regionPtr));
	writel(0x1 | (0x1 << 7) | (0x2 << 4) | (0x4 << 1),
		DMA_CMD(holder->regionPtr));
	/* wait for the transfer to finish */
	while (readl(DMA_CMD(holder->regionPtr)) & 0x01)
		;

	/* copy from card with IRQ */
	writel(0x40000, DMA_SRC(holder->regionPtr));
	writel(holder->phys+20, DMA_DST(holder->regionPtr));
	writel(10, DMA_COUNT(holder->regionPtr));
	writel(0x1 | (0x2 << 4) | (0x4 << 1), DMA_CMD(holder->regionPtr));

	return 0;
}

void my_remove(struct pci_dev *pdev)
{
	struct holder *holder = (struct holder *) pci_get_drvdata(pdev);

	printk(KERN_INFO "Removing driver for device [%.4x:%.4x]\n",
		VENDOR, DEVICE);

	/* disable interrups */
	writel(0x0000, INT_ENABLE(holder->regionPtr));

	/* remove IRQ */
	free_irq(pdev->irq, (void *) holder);

	/* remove the device that mmaps the memory */
	misc_deregister(&my_misc);

	/* free DMA memory */
	dma_free_coherent(&pdev->dev, PAGE_SIZE, holder->virt, holder->phys);

	/* unmap requested region */
	iounmap(holder->regionPtr);

	/* stop and delete the tasklet */
	tasklet_kill(holder->tasklet);
	kfree(holder->tasklet);

	/* free the holder structure */
	kfree(holder);

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
