#ifndef _HELLO_ANDROID_H_
#define _HELLO_ANDROID_H_

#include <linux/cdev.h>
#include <linux/semaphore.h>

#define HELLO_DEVICE_NODE_NAME  "hello"
#define HELLO_DEVICE_FILE_NAME  "hello“
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
