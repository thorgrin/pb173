#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/kfifo.h>
#include <linux/miscdevice.h>

#define FIFO_SIZE 4096
/* define up structure for eth devise */
struct net_device *myeth;
struct net_device_ops myops;
/* define fifo for data */
DEFINE_KFIFO(myfifo, char, FIFO_SIZE);

/* eth device open function */
int my_open(struct net_device *dev)
{
	printk(KERN_INFO "my_open\n");
	return 0;
}

/* eth device close function */
int my_close(struct net_device *dev)
{
	printk(KERN_INFO "my_close\n");
	return 0;
}

/* eth device xmit function */
int my_xmit(struct sk_buff *buff, struct net_device *dev)
{
	printk(KERN_INFO "my_xmit\n");
	/* just copy the data to the buffer */
	kfifo_in(&myfifo, buff->data, buff->len);

	dev_kfree_skb(buff);
	return NETDEV_TX_OK;
}

static ssize_t my_write(struct file *filp, const char __user *buf, size_t count,
		loff_t *off)
{
	ssize_t ret = count;

	/* create the sk_buff structure */
	struct sk_buff *skb = netdev_alloc_skb(myeth, count);
	skb->len = sizeof(count);

	/* copy the data from user to the structure */
	if (copy_from_user(skb->data, buf, count))
		ret = -EFAULT;

	/* set protocol and transmit the data */
	skb->protocol = (eth_type_trans(skb, myeth));
	netif_rx(skb);

	return ret;
}

static ssize_t my_read(struct file *filp, char __user *buf, size_t count,
		loff_t *off)
{
	int read;
	/* just read the data from fifo to user */
	if (kfifo_to_user(&myfifo, buf, count, &read))
		return -EFAULT;

	return read;
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
	/* setup the eth device */
	myeth = alloc_etherdev(0);
	if (myeth == NULL)
		return -EFAULT;
	myeth->netdev_ops = &myops;
	myops.ndo_open = &my_open;
	myops.ndo_stop = &my_close;
	myops.ndo_start_xmit = &my_xmit;
	random_ether_addr(myeth->dev_addr);

	if (register_netdev(myeth)) {
		free_netdev(myeth);
		return -EFAULT;
	}

	/* register the misc device */
	if (misc_register(&my_misc)) {
		unregister_netdev(myeth);
		free_netdev(myeth);
		return -EFAULT;
	}


	return 0;
}

static void my_exit(void)
{
	/* deregister the devices */
	misc_deregister(&my_misc);
	unregister_netdev(myeth);
	free_netdev(myeth);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
