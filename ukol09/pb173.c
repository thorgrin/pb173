#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/gfp.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/uaccess.h>


void *ptr[4];

struct page *my_nopage(struct vm_area_struct *vma, unsigned long address,
	int *type)
{
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	struct page *page = NULL;
	int offset_page;

	offset += address - vma->vm_start;
	offset_page = offset / PAGE_SIZE;

	if (offset_page < 2)
		page = vmalloc_to_page(ptr[offset_page]);
	else if (offset_page < 4)
		page = virt_to_page(ptr[offset_page]);

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
	.name = "my_name",
};

static int my_init(void)
{
	int i;
	for (i = 0; i < 2; i++)
		ptr[i] = vmalloc_user(PAGE_SIZE);

	for (i = 2; i < 4; i++)
		ptr[i] = (void *) __get_free_page(GFP_KERNEL);

	strcpy(ptr[0], "nazdar");
	strcpy(ptr[1], "cau");
	strcpy(ptr[2], "ahoj");
	strcpy(ptr[3], "bye");


	return misc_register(&my_misc);
}

static void my_exit(void)
{
	int i;

	for (i = 0; i < 2; i++)
		vfree(ptr[i]);
	for (i = 2; i < 4; i++)
		free_page((unsigned long) ptr[i]);

	misc_deregister(&my_misc);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
