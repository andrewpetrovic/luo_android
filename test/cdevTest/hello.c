#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <asm/uaccess.h>

#include "hello.h"

int hello_major = HELLO_MAJOR;
int hello_minor = 0;
int hello_nr_devs = HELLO_NR_DEVS;

int hello_quantum = HELLO_QUANTUM;
int hello_qset = HELLO_QSET;

struct class *hello_class;
struct hello_android_dev *hello_dev;

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

int hello_trim(struct hello_android_dev *dev){
    struct hello_qset *next, *dptr;
    int qset = dev->qset;
    int i;

    for(dptr = dev->data; dptr; dptr = next){
        if(dptr->data){
            for(i = 0; i < qset; i++){
                kfree(dptr->data[i]);
            }
            kfree(dptr->data);
            dptr->data = NULL;
        }
        next = dptr->next;
        kfree(dptr);
    }
    dev->size = 0;
    dev->quantum = hello_quantum;
    dev->qset = hello_qset;
    dev->data = NULL;
    return 0;
}

int hello_open(struct inode *inode, struct file *filp){
    struct hello_android_dev *dev;
    printk(KERN_ALERT "Debug by andrea: dev will open");
    dev = container_of(inode->i_cdev, struct hello_android_dev, cdev);
    filp->private_data = dev;
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY){
        if (down_interruptible(&dev->sem)){
            printk(KERN_WARNING "Debug by andrea: interrupt error when open");
            return -ERESTARTSYS;
        }
        hello_trim(dev);
        up(&dev->sem);
    }
    return 0;
}

int hello_release(struct inode *inode, struct file *filp){
    printk(KERN_ALERT "Debug by andrea: dev will release");
    return 0;
}

struct hello_qset *hello_follow(struct hello_android_dev *dev, int n){
    struct hello_qset *qs = dev->data;
    if(!qs){
        printk(KERN_ALERT "Debug by andrea: need re-malloc first item of qset list");
        qs = dev->data = kmalloc(sizeof(struct hello_qset),GFP_KERNEL);
        if(qs == NULL){
            printk(KERN_WARNING "Debug by andrea: qset first item re-malloc fail ");
            return NULL;
        }
        memset(qs, 0, sizeof(struct hello_qset));
    }

    while(n--){
        if(!qs->next) {
            printk(KERN_ALERT "Debug by andrea: need re-malloc other qset item");
            qs -> next = kmalloc(sizeof(struct hello_qset),GFP_KERNEL);
            if(qs->next == NULL){
                printk(KERN_WARNING "Debug by andrea: other qset item re-malloc fail");
                return NULL;
            }
            memset(qs->next, 0, sizeof(struct hello_qset));
        }
        qs = qs->next;
        continue;
    }
    return qs;
}

ssize_t hello_read(struct file *filp,
                   char __user *buf,
                   size_t count,
                   loff_t *f_pos){
    struct hello_android_dev *dev = filp->private_data;
    struct hello_qset *dptr;

    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset;
    int item, s_pos, q_pos, rest;
    ssize_t retval = 0;

    if(down_interruptible(&dev->sem)){
        printk(KERN_WARNING "Debug by andrea: interrupt fail before read");
        return -ERESTARTSYS;
    }
    if(*f_pos >= dev->size){
        printk(KERN_WARNING "Debug by andrea: read position is overflow");
        goto out;
    }
    if(*f_pos + count > dev->size){
        printk(KERN_ALERT "Debug by andrea: read count is too much,need re-calculate");
        count = dev->size - *f_pos;
    }
    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum;
    q_pos = rest % quantum;

    dptr = hello_follow(dev, item);

    if(dptr == NULL || !dptr->data || !dptr->data[s_pos]){
        printk(KERN_WARNING "Debug by andrea: qset is null, or data point is null, or quantum is null");
        goto out;
    }

    if(count > quantum - q_pos){
        printk(KERN_ALERT "Debug by andrea: read only up to the end of this quantum");
        count = quantum - q_pos;
    }

    if(copy_to_user(buf, dptr->data[s_pos] + q_pos, count)){
        printk(KERN_WARNING "Debug by andrea: read fail");
        retval = -EFAULT;
        goto out;
    }

    *f_pos += count;
    retval = count;
out:
    up(&dev->sem);
    return retval;
}

ssize_t hello_write(struct file *filp,
                    const char __user *buf,
                    size_t count,
                    loff_t *f_pos){
    struct hello_android_dev *dev = filp->private_data;
    struct hello_qset *dptr;

    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset;
    int item, s_pos, q_pos, rest;
    ssize_t retval = -ENOMEM;

    if(down_interruptible(&dev->sem)){
        printk(KERN_WARNING "Debug by andrea: before write, interrupt fail");
        return -ERESTARTSYS;
    }
    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum;
    q_pos = rest % quantum;

    dptr = hello_follow(dev, item);
    if(dptr == NULL)
        goto out;
    if(!dptr->data){
        printk(KERN_ALERT "Debug by andrea: need re-malloc qset->data");
        dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
        if(!dptr -> data){
            printk(KERN_WARNING "Debug by andrea: re-malloc qset->data fail");
            goto out;
        }
        memset(dptr->data,0,qset * sizeof(char *));
    }
    if (!dptr->data[s_pos]) {
        printk(KERN_ALERT "Debug by andrea: need re-malloc quantum");
        dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
        if (!dptr->data[s_pos]){
            printk(KERN_WARNING "Debug by andrea: re-malloc quantum fail");
            goto out;
        }
    }
    if(count > quantum - q_pos){
        printk(KERN_ALERT "Debug by andrea: write only up to the end of this quantum");
        count = quantum -q_pos;
    }
    if(copy_from_user(dptr->data[s_pos]+q_pos,buf,count)){
        printk(KERN_WARNING "Debug by andrea: write fail");
        retval = -EFAULT;
        goto out;
    }
    *f_pos += count;
    retval = count;
    if(dev->size < *f_pos){
        printk(KERN_ALERT "Debug by andrea: update the size");
        dev->size = *f_pos;
    }
out:
    up(&dev->sem);
    return retval;
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
    if(hello_class){
        class_destroy(hello_class);
    }
    if(hello_dev){
        for(i = 0; i < hello_nr_devs; i++){
            hello_trim(hello_dev + i);
            cdev_del(&hello_dev[i].cdev);
        }
        kfree(hello_dev);
    }
    unregister_chrdev_region(devno,hello_nr_devs);
}

static int __init hello_init(void){
    int result, i;
    dev_t dev = 0;
    struct device *temp = NULL;

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
    hello_dev = kmalloc(hello_nr_devs *
                        sizeof(struct hello_android_dev),GFP_KERNEL);

    if(!hello_dev){
        printk(KERN_WARNING "Debug by Andrea: malloc hello_android_dev fail");
        result = -ENOMEM;
        goto fail;
    }
    /*hello_dev 内存清零*/
    memset(hello_dev, 0, sizeof(struct hello_android_dev));
    for(i = 0; i < hello_nr_devs; i++){
        hello_dev[i].quantum = hello_quantum;
        hello_dev[i].qset = hello_qset;
        sema_init(&hello_dev[i].sem,1);
        hello_setup_cdev(&hello_dev[i],i);
    }

    hello_class = class_create(THIS_MODULE,HELLO_DEVICE_CLASS_NAME);
    if(IS_ERR(hello_class)){
        result = PTR_ERR(temp);
        printk(KERN_WARNING "Debug by andrea: Failed to create hello device");
        goto destroy_cdev;
    }

    temp = device_create(hello_class, NULL, dev, "%s", HELLO_DEVICE_FILE_NAME);
    if(IS_ERR(temp)) {
        result = PTR_ERR(temp);
        printk(KERN_ALERT"Failed to create hello device.");
        goto destroy_class;
    }
    return 0;

destroy_cdev:
    hello_cleanup_module();
destroy_class:
    class_destroy(hello_class);
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
