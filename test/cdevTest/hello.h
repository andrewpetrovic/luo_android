#ifndef _HELLO_ANDROID_H_
#define _HELLO_ANDROID_H_

#include <linux/cdev.h>
#include <linux/semaphore.h>

#define HELLO_DEVICE_NODE_NAME  "hello"
#define HELLO_DEVICE_FILE_NAME  "hello“
#define HELLO_DEVICE_PROC_NAME  "hello"
#define HELLO_DEVICE_CLASS_NAME "hello"

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

struct hello_android_dev {
    int val;
    struct semaphore sem;
    struct cdev cdev;
};

#endif
