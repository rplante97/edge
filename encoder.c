// A. Sheaff 3/15/2019 - edge driver
// File operations framework
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/gpio/consumer.h>

#include "edge.h"

// Data to be "passed" around to various functions
struct edge_data_t {
	struct gpio_desc *gpio_enable;		// Enable pin
	struct gpio_desc *gpio_clock;		// Clock pin
	struct gpio_desc *gpio_bit0;		// Bit 0 pin
	struct gpio_desc *gpio_bit1;		// Bit 1 pin
	struct gpio_desc *gpio_shutdown;	// Shutdown input
	int major;			// Device major number
	struct class *edge_class;	// Class for auto /dev population
	struct device *edge_dev;	// Device for auto /dev population
			// Write lock
			// Clock delay - should not be less than 100 us.
			// lock for ioctl to change the delay
			// Delay change during transmission does not matter.
};

// EDGE data structure access between functions
static struct edge_data_t *edge_data_fops;


// Used to change the clock (high/low pulse widths) period.  At the moment this ioctl
//   always returns an error.
// More to be added shortly
static long edge_ioctl(struct file * filp, unsigned int cmd, unsigned long arg)
{
	long ret=0;		// Return value

	ret=-EINVAL;
	return ret;
}

// You will need to choose the type of locking yourself.  It may be atmonic variables, spinlocks, mutex, or semaphore.

// Write system call
// If another process is using the pins and the device was opened O_NONBLOCK
//   then return with the appropriate error
// Otherwise
//   If another process is using the pins
//     then block/wait for the pin to be free.  Clean up and return an error if a signal is received.
//   Otherwise
//     Copy the user space data using copy_from_user() to a local kernel space buffer that you allocate
//     Encode to the copied data using our encoding system from homework 4 (your "encoder" code) to another kernel buffer that you allocate
//     Free the first buffer
//     Toggle pins as in homework 05 gpio code.  Go to sleep while delaying. *** SEE TIMERS-HOWTO.TXT IN THE KERNEL DOCUMENTATION ***
//       Use a variable for the clock high and low pulse widths that is shared with the ioctl class.  The
//  CLEAN UP (free memory and any other resouces you allocated) AND RETURN APPROPRAITE VALUE
static ssize_t edge_write(struct file *filp, const char __user * buf, size_t count, loff_t * offp)
{
	struct edge_data_t *edge_dat;   // Driver data - has gpio pins
	ssize_t ret=count;				// Return value
	
	edge_dat=(struct edge_data_t *)filp->private_data;

	return ret;
}

// Open system call
// Open only if the file access flags (NOT permissions) are appropiate as discussed in class
// Return an appropraite error otherwise
static int edge_open(struct inode *inode, struct file *filp)
{


	filp->private_data=edge_data_fops;  // My driver data (edge_dat)

	return 0;
}

// Close system call
// What is there to do?
static int edge_release(struct inode *inode, struct file *filp)
{
    return 0;
}

// File operations for the edge device
static const struct file_operations edge_fops = {
	.owner = THIS_MODULE,	// Us
	.open = edge_open,		// Open
	.release = edge_release,// Close
	.write = edge_write,	// Write
	.unlocked_ioctl=edge_ioctl,	// ioctl
};

static struct gpio_desc *edge_dt_obtain_pin(struct device *dev, struct device_node *parent, char *name, int init_val)
{
	struct device_node *dn_child=NULL;	// DT child
	struct gpio_desc *gpiod_pin=NULL;	// GPIO Descriptor for setting value
	int ret=-1;	// Return value
	int pin=-1;	// Pin number
	char *label=NULL;	// DT Pin label

	// Find the child - release with of_node_put()
	dn_child=of_get_child_by_name(parent,name);
	if (dn_child==NULL) {
		printk(KERN_INFO "No child %s\n",name);
		gpiod_pin=NULL;
		goto fail;
	}

	// Get the child pin number - does not appear to need to be released
	pin=of_get_named_gpio(dn_child,"gpios",0);
	if (pin<0) {
		printk(KERN_INFO "no %s GPIOs\n",name);
		gpiod_pin=NULL;
		goto fail;
	}
	// Verify pin is OK
	if (!gpio_is_valid(pin)) {
		gpiod_pin=NULL;
		goto fail;
	}
	printk(KERN_INFO "Found %s pin %d\n",name,pin);

	// Get the of string tied to pin - Does not appear to need to be released
	ret=of_property_read_string(dn_child,"label",(const char **)&label);
	if (ret<0) {
		printk(KERN_INFO "Cannot find label\n");
		gpiod_pin=NULL;
		goto fail;
	}
	// Request the pin - release with devm_gpio_free() by pin number
	if (init_val>=0) {
		ret=devm_gpio_request_one(dev,pin,GPIOF_OUT_INIT_LOW,label);
		if (ret<0) {
			dev_err(dev,"Cannot get %s gpio pin\n",name);
			gpiod_pin=NULL;
			goto fail;
		}
	} else {
		ret=devm_gpio_request_one(dev,pin,GPIOF_IN,label);
		if (ret<0) {
			dev_err(dev,"Cannot get %s gpio pin\n",name);
			gpiod_pin=NULL;
			goto fail;
		}
	}

	// Get the gpiod pin struct
	gpiod_pin=gpio_to_desc(pin);
	if (gpiod_pin==NULL) {
		printk(KERN_INFO "Failed to acquire enable gpio\n");
		gpiod_pin=NULL;
		goto fail;
	}

	// Make sure the pin is set correctly
	if (init_val>=0) gpiod_set_value(gpiod_pin,init_val);


	// Release the device node
    of_node_put(dn_child);
	
	return gpiod_pin;

fail:
	if (pin>=0) devm_gpio_free(dev,pin);
    if (dn_child) of_node_put(dn_child);

	return gpiod_pin;
}


// Sets device node permission on the /dev device special file
static char *edge_devnode(struct device *dev, umode_t *mode)
{
	if (mode) *mode = 0666;
	return NULL;
}

// My data is going to go in either platform_data or driver_data
//  within &pdev->dev. (dev_set/get_drvdata)
// Called when the device is "found" - for us
// This is called on module load based on ".of_match_table" member
static int edge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;	// Device associcated with platform

	struct edge_data_t *edge_dat;		// Data to be passed around the calls
	struct device_node *dn=NULL;

	int ret=-1;	// Return value


	// Allocate device driver data and save
	edge_dat=kmalloc(sizeof(struct edge_data_t),GFP_ATOMIC);
	if (edge_dat==NULL) {
		printk(KERN_INFO "Memory allocation failed\n");
		return -ENOMEM;
	}

	memset(edge_dat,0,sizeof(struct edge_data_t));
	
	dev_set_drvdata(dev,edge_dat);

// Find my device node
	dn=of_find_node_by_name(NULL,"edge");
	if (dn==NULL) {
		printk(KERN_INFO "Cannot find device\n");
		ret=-ENODEV;
		goto fail;
	}
	edge_dat->gpio_enable=edge_dt_obtain_pin(dev,dn,"Enable",0);
	if (edge_dat->gpio_enable==NULL) goto fail;
	edge_dat->gpio_clock=edge_dt_obtain_pin(dev,dn,"Clock",0);
	if (edge_dat->gpio_clock==NULL) goto fail;
	edge_dat->gpio_bit0=edge_dt_obtain_pin(dev,dn,"Bit0",0);
	if (edge_dat->gpio_bit0==NULL) goto fail;
	edge_dat->gpio_bit1=edge_dt_obtain_pin(dev,dn,"Bit1",0);
	if (edge_dat->gpio_bit1==NULL) goto fail;
	edge_dat->gpio_shutdown=edge_dt_obtain_pin(dev,dn,"Shutdown",-1);
	if (edge_dat->gpio_shutdown==NULL) goto fail;



	// Create the device - automagically assign a major number
	edge_dat->major=register_chrdev(0,"edge",&edge_fops);
	if (edge_dat->major<0) {
		printk(KERN_INFO "Failed to register character device\n");
		ret=edge_dat->major;
		goto fail;
	}

	// Create a class instance
	edge_dat->edge_class=class_create(THIS_MODULE, "edge_class");
	if (IS_ERR(edge_dat->edge_class)) {
		printk(KERN_INFO "Failed to create class\n");
		ret=PTR_ERR(edge_dat->edge_class);
		goto fail;
	}

	// Setup the device so the device special file is created with 0666 perms
	edge_dat->edge_class->devnode=edge_devnode;
	edge_dat->edge_dev=device_create(edge_dat->edge_class,NULL,MKDEV(edge_dat->major,0),(void *)edge_dat,"edge");
	if (IS_ERR(edge_dat->edge_dev)) {
		printk(KERN_INFO "Failed to create device file\n");
		ret=PTR_ERR(edge_dat->edge_dev);
		goto fail;
	}

	edge_data_fops=edge_dat;
	
	// Setup lock(s) & delay

	printk(KERN_INFO "Registered\n");
	dev_info(dev, "Initialized");
	return 0;

fail:
	// Device cleanup
	if (edge_dat->edge_dev) device_destroy(edge_dat->edge_class,MKDEV(edge_dat->major,0));
	// Class cleanup
	if (edge_dat->edge_class) class_destroy(edge_dat->edge_class);
	// char dev clean up
	if (edge_dat->major) unregister_chrdev(edge_dat->major,"edge");

	if (edge_dat->gpio_shutdown) devm_gpio_free(dev,desc_to_gpio(edge_dat->gpio_shutdown));
	if (edge_dat->gpio_bit1) devm_gpio_free(dev,desc_to_gpio(edge_dat->gpio_bit1));
	if (edge_dat->gpio_bit0) devm_gpio_free(dev,desc_to_gpio(edge_dat->gpio_bit0));
	if (edge_dat->gpio_clock) devm_gpio_free(dev,desc_to_gpio(edge_dat->gpio_clock));
	if (edge_dat->gpio_enable) devm_gpio_free(dev,desc_to_gpio(edge_dat->gpio_enable));


	dev_set_drvdata(dev,NULL);
	kfree(edge_dat);
	printk(KERN_INFO "Four FSK Failed\n");
	return ret;
}

// Called when the device is removed or the module is removed
static int edge_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct edge_data_t *edge_dat;	// Data to be passed around the calls

	// Obtain the device driver data
	edge_dat=dev_get_drvdata(dev);

	// Device cleanup
	device_destroy(edge_dat->edge_class,MKDEV(edge_dat->major,0));
	// Class cleanup
	class_destroy(edge_dat->edge_class);
	// Remove char dev
	unregister_chrdev(edge_dat->major,"edge");

	// Free the gpio pins with devm_gpio_free() & gpiod_put()
	devm_gpio_free(dev,desc_to_gpio(edge_dat->gpio_shutdown));
	devm_gpio_free(dev,desc_to_gpio(edge_dat->gpio_bit1));
	devm_gpio_free(dev,desc_to_gpio(edge_dat->gpio_bit0));
	devm_gpio_free(dev,desc_to_gpio(edge_dat->gpio_clock));
	devm_gpio_free(dev,desc_to_gpio(edge_dat->gpio_enable));
#if 0
	// not clear if these are allocated and need to be freed
	gpiod_put(edge_dat->gpio_shutdown);
	gpiod_put(edge_dat->gpio_bit1);
	gpiod_put(edge_dat->gpio_bit0);
	gpiod_put(edge_dat->gpio_clock);
	gpiod_put(edge_dat->gpio_enable);
#endif
	
	// Free the device driver data
	dev_set_drvdata(dev,NULL);
	kfree(edge_dat);

	printk(KERN_INFO "Removed\n");
	dev_info(dev, "GPIO mem driver removed - OK");

	return 0;
}

static const struct of_device_id edge_of_match[] = {
    {.compatible = "brcm,bcm2835-edge",},
    { /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, edge_of_match);

static struct platform_driver edge_driver = {
    .probe = edge_probe,
    .remove = edge_remove,
    .driver = {
           .name = "bcm2835-edge",
           .owner = THIS_MODULE,
           .of_match_table = edge_of_match,
           },
};

module_platform_driver(edge_driver);

MODULE_DESCRIPTION("Edge pin modulator");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Edge");
//MODULE_ALIAS("platform:edge-bcm2835");
