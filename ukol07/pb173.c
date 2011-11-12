#include <linux/io.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/pci.h>

/* structure in the list */
struct my_struct {
	struct pci_dev *pdev;
	struct list_head list;
};

LIST_HEAD(device_list);

/* help function printing info about the device */
static void print_dev(struct pci_dev *pdev)
{
	 printk(KERN_INFO "%.2x:%.2x.%.2x, vendor: %.4x, device %.4x\n",
			pdev->bus->number, PCI_SLOT(pdev->devfn),
			PCI_FUNC(pdev->devfn), pdev->vendor, pdev->device);
}

static int my_init(void)
{
	struct my_struct *s;
	struct pci_dev *pdev = NULL;

	/* go over all pci devices */
	while ((pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev))) {
		/* allocate memory for list item */
		s = kmalloc(sizeof(*s), GFP_KERNEL);

		/* just stop looping on error. */
		if (s == NULL) {
			pci_dev_put(pdev);
			break;
		}

		/* add the item to the list */
		pci_dev_get(pdev);
		s->pdev = pdev;
		list_add(&s->list, &device_list);
	}

	return 0;
}

/* this function loops simultaneously through both stored and current
pci device list. both lists are sorted by domain, bus, slot and function
(at least it appears that it is always so). */
static void my_exit(void)
{
	struct my_struct *s, *s1;
	struct pci_dev *pdev = NULL;

	/* get first current item */
	pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev);

	/* loop over stored list */
	list_for_each_entry_safe_reverse(s, s1, &device_list, list) {
		/* while current item is < than stored, print it and try next */
		/* this is most ugly and unreadable and deserves own function */
		while (pdev &&
			((pci_domain_nr(pdev->bus) <
			pci_domain_nr(s->pdev->bus)) ||
			(pci_domain_nr(pdev->bus) <=
			pci_domain_nr(s->pdev->bus) &&
			(pdev->bus->number < s->pdev->bus->number)) ||
			((pci_domain_nr(pdev->bus) <=
			pci_domain_nr(s->pdev->bus) &&
			(pdev->bus->number <= s->pdev->bus->number)) &&
			(PCI_SLOT(pdev->devfn) < PCI_SLOT(s->pdev->devfn))) ||
			((pci_domain_nr(pdev->bus) <=
			pci_domain_nr(s->pdev->bus) &&
			(pdev->bus->number <= s->pdev->bus->number) &&
			(PCI_SLOT(pdev->devfn) <=
			PCI_SLOT(s->pdev->devfn)) &&
			(PCI_FUNC(pdev->devfn) <
			PCI_FUNC(s->pdev->devfn)))))) {

			print_dev(pdev);
			pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev);
		}

		/* when items are different, stored item is smaller */
		if (!pdev ||
			pci_domain_nr(s->pdev->bus) !=
			pci_domain_nr(pdev->bus) ||
			s->pdev->bus->number != pdev->bus->number ||
			PCI_SLOT(s->pdev->devfn) != PCI_SLOT(pdev->devfn) ||
			PCI_FUNC(s->pdev->devfn) != PCI_FUNC(pdev->devfn)) {

			print_dev(s->pdev);
		} else if (pdev) { /* otherwise increment current item */
			pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev);
		}

		/* always remove stored item fromthe list */
		pci_dev_put(s->pdev);
		list_del(&s->list);
		kfree(s);
	}

	/* there still might be some new devices, print them */
	while (pdev && (pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev)))
		print_dev(pdev);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
