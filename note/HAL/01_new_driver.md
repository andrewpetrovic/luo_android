[TOC]
# 在Ubuntu上为Android系统编写Linux内核驱动程序

为了方便描述为Android系统编写和编译内核驱动程序的过程,
我使用一个虚拟的硬件设备（老罗原文例子在3.4以上的内核把运行有问题,这里我根据ldd3 scull例子重写例子),
这个设备指向一个链表结构，每一个节点有4000000个字节的缓冲区,它可写可读,且一次仅仅可以读写一个节点
这个设备同样命名为hello

## 编写内核

1. 参考[Building Kernel](https://source.android.com/source/building-kernels.html)设置准备好内核构建环境

2. 进入到kernel/msm/drivers，新建hello目录

```shell
mkdir drivers/hello
```

3. 在hello下创建hello.h, 这里定义声明必要的字符串常量宏和函数，
并且定义了设备文件结构体以及该设备文件指向的链表结构

```C
#ifndef _HELLO_ANDROID_H_
#define _HELLO_ANDROID_H_

#include <linux/cdev.h>
#include <linux/semaphore.h>

#define HELLO_DEVICE_NODE_NAME  "hello"
#define HELLO_DEVICE_FILE_NAME  "hello"
#define HELLO_DEVICE_PROC_NAME  "hello"
#define HELLO_DEVICE_CLASS_NAME "hello"

#ifndef HELLO_MAJOR
#define HELLO_MAJOR 0
#endif

#ifndef HELLO_NR_DEVS
#define HELLO_NR_DEVS 1
#endif

#ifndef HELLO_P_NR_DEVS
#define HELLO_P_NR_DEVS 1
#endif

#ifndef HELLO_QUANTUM
#define HELLO_QUANTUM 4000
#endif

#ifndef HELLO_QSET
#define HELLO_QSET    1000
#endif

#ifndef HELLO_P_BUFFER
#define HELLO_P_BUFFER 4000
#endif

extern int hello_major;
extern int hello_nr_devs;
extern int hello_quantum;
extern int hello_qset;

static int __init hello_init(void);
static void __exit hello_exit(void);
void hello_cleanup_module(void);

ssize_t hello_read(struct file *filp,
                    char __user *buf,
                    size_t count,
                    loff_t *f_pos);

ssize_t hello_write(struct file *filp,
                    const char __user *buf,
                    size_t count,
                    loff_t *f_pos);

int hello_open(struct inode *inode,
                   struct file *filp);

int hello_release(struct inode *inode,
                      struct file *filp);

struct hello_qset{
    void **data;
    struct hello_qset *next;
};

struct hello_android_dev {
    struct hello_qset *data; /*指向第一个量子集*/
    int quantum; /*当前量子大小*/
    int qset; /*当前量子集大小*/
    unsigned long size; /*存放在这里的数据量*/
    unsigned int access_key; /*被sculluid和scullpriv使用*/
    struct semaphore sem; /*互斥信号量*/
    struct cdev cdev; /*cdev 结构体*/
};

#endif
```

4. 创建hello.c,首先定义设备文件操作方法表

```C
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
```

5. 实现open、release方法
inode->i_cdev 指向hello_android_dev结构体的cdev成员的地址，通过container_of宏将hello_android_dev结构体变量的地址取出
flip->private_data用于保存设备的地址，在read和write时使用
```C
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
```

6. 实现read、write方法

```C
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
```

7. 实现模块加载和清理方法

```C
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
```

## 配置Hello模块

1. 在hello目录中新增Kconfig，Kconfig在设置内核config时用的到
tristate表示编译选项，HELLO支持在编译内核时支持以模块、内建和不编译三种方式
默认是不编译，可以在稍后执行make menuconfig命令时修改配置。

```
config HELLO
    tristate "First Android Driver"
    default n
    help
    This is the first android driver.
```

2. 在hello目录中新增Makefile，Makefile在执行编译时使用
Makefile根据选项HELLO的值，执行不同的编译方法

```
obj-$(CONFIG_HELLO) += hello.o
```

3. 修改drivers/Kconfig文件，在menu "Device Drivers"和 endmenu之间添加：
这样稍后再执行make menuconfig时，就可以配置hello模块的编译选项了。
```
source "drivers/hello/Kconfig"
```

4. 修改drivers/Makefile文件，添加一行：
```
obj-$(CONFIG_HELLO) += hello/
```

5. 使用make menuconfig修改内核配置。如果内核根目录下存在.config文件,该命令会读取这个文件进menuconfig x界面
在x界面中，进入"Device Drivers"，将"First Android Drivers"设置为*

## 编译内核

1. 在内核根目录执行Make，编译完成后，hello目录下可以 看到hello.o文件，这时，编译出来的zImage已经包含了hello驱动
2. 在Android源码根目录执行Make bootimage，编译成功后，将新生成的boot.img刷入手机

## 测试
1. adb切换成root模式
2. 进入adb shell， ls /dev可以看到模块加载时生成的hello设备文件
3. 访问hello文件,这是hello文件为空
```shell
cat /dev/hello
```
4. 写入一些内容到hello文件
```shell
echo '5' > /dev/hello
```
5. 再一次访问hello文件，这时可以看到输出 5

至此，hello内核驱动已经构建进入内核，并且验证一切正常。
