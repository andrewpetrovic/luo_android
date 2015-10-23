#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

#include "hello.h"

static int hello_major = 0;
static int hello_minor = 0;
static int hello_nr_devs = 1;

static struct hello_android_dev *hello_dev = NULL;

MODULE_AUTHOR("Andrea Ji");
MODULE_DESCRIPTION("First Android Driver");
MODULE_LICENSE("Dual BSD/GPL");

/*设备文件操作方法表*/
struct file_operations hello_fops = {
    .owner  = THIS_MODULE,
    .read   = hello_read,
    .write  = hello_write,
    .open   = hello_open,
    .release= hello_release,
};

int hello_open(struct inode *inode, struct file *filp){
    struct hello_android_dev *dev;
    printk(KERN_ALERT "Debug by andrea: dev will open");
    dev = container_of(inode->i_cdev,
                       struct hello_android_dev,
                       cdev);
    filp->private_data = dev;
    if((filp -> f_flags & O_ACCMODE) == O_WRONLY){
        if(down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        up(&dev->sem);
    }
}

int hello_release(struct inode *inode, struct file *filp){
    printk(KERN_ALERT "Debug by andrea: dev will release");
    return 0;
}

ssize_t hello_read(struct file *filp,
                   char __user *buf,
                   size_t count,
                   loff_t *f_pos){
    ssize_t err = 0;
    struct hello_android_dev *dev = filp->private_data;

    /*同步访问*/
    if(down_interruptible(&(dev->sem))) {
        printk(KERN_WARNING "Debug by andrea: before read, interrupt fail");
        return -ERESTARTSYS;
    }
    if(count < sizeof(dev->val)) {
        printk(KERN_WARNING "Debug by andrea:  before read, overload");
        goto out;
    }
    printk(KERN_ALERT "Debug by andrea: read dev");
    if(copy_to_user(buf, &(dev->val),sizeof(dev->val))){
        printk(KERN_WARNING "Debug by andrea: read fail");
        err = -EFAULT;
        goto out;
    }
    err = sizeof(dev->val);
out:
    up(&(dev->sem));
    return err;
}

ssize_t hello_write(struct file *filp,
                    const char __user *buf,
                    size_t count,
                    loff_t *f_pos){
    struct hello_android_dev *dev = filp->private_data;
    ssize_t err = 0;
    if(down_interruptible(&(dev->sem))){
        printk(KERN_WARNING "Debug by andrea: before write, interrupt fail");
        return -ERESTARTSYS;
    }
    if(count != sizeof(dev->val)){
        printk(KERN_WARNING "Debug by andrea: before write, overload");
        goto out;
    }
    printk(KERN_ALERT "Debug by andrea: write dev");
    if(copy_from_user(&(dev->val),buf,count)){
        printk(KERN_WARNING "Debug by andrea: write fail");
        err = -EFAULT;
        goto out;
    }

    err = sizeof(dev->val);
out:
    up(&(dev->sem));
    return err;
}

static void hello_setup_cdev(struct hello_android_dev *dev,
                             int index){
    int err, devno = MKDEV(hello_major,hello_minor + index);
    printk(KERN_ALERT "Debug by andrea: setup cdev");
    cdev_init(&dev->cdev,&hello_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &hello_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if(err){
        printk(KERN_WARNING "Debug by andrea: Error %d adding hello %d", err, index);
    }
}

void hello_cleanup_module(void){
    int i;
    dev_t devno = MKDEV(hello_major,hello_minor);
    if(hello_dev){
        cdev_del(&hello_dev->cdev);
    }
    kfree(hello_dev);
}

static int __init hello_init(void){
    int result, i;
    dev_t dev = 0;

    printk(KERN_ALERT "Debug by andrea: hello_init()");

    if(hello_major){
        printk(KERN_ALERT "Debug by andrea: static asign dev id");
        dev = MKDEV(hello_major, hello_minor);
        result = register_chrdev_region(dev,
                                         hello_nr_devs,
                                         "hello");
    } else {
        printk(KERN_ALERT "Debug by Andrea: dymanic asing dev id");
        result = alloc_chrdev_region(&dev,
                                     hello_minor,
                                     hello_nr_devs,
                                     "hello");
        hello_major = MAJOR(dev);
    }

    if(result < 0){
        printk(KERN_WARNING "Debug by Andrea: can't get major %d\n", hello_major);
        return result;
    }

    printk(KERN_ALERT "Debug by Andrea: malloc for hello_android_dev");
    hello_dev = kmalloc(
        sizeof(struct hello_android_dev),GFP_KERNEL);
    if(!hello_dev){
        printk(KERN_WARNING "Debug by Andrea: malloc hello_android_dev fail");
        result = -ENOMEM;
        goto fail;
    }
    /*hello_dev 内存清零*/
    memset(hello_dev, 0, sizeof(struct hello_android_dev));
    sema_init(&(hello_dev->sem),1);
    hello_setup_cdev(&hello_dev,0);
    return 0;

fail:
    printk(KERN_WARNING "Debug by andrea: module init fail, program will cleanup now");
    hello_cleanup_module();
    return result;
}

static void __exit hello_exit(void){
    printk(KERN_ALERT "Debug by andrea: module exit, program will cleanup now");
    hello_cleanup_module();
}

module_init(hello_init);
module_exit(hello_exit);
