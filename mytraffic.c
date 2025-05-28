/* LAB 4, ENG EC 535 SPRING 2025
 * VARSHA SINGH:    U50072781
 * JAMES KNEE:      Uxxxxxxxx */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h> /* kmalloc() */
#include <linux/fs.h> /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/types.h> /* size_t */
#include <linux/fcntl.h> /* O_ACCMODE */
#include <asm/system_misc.h> /* cli(), *_flags */
#include <linux/uaccess.h>
#include <asm/uaccess.h> /* copy_from/to_user */
#include <linux/sched.h> //for the timer
#include <linux/jiffies.h> //for the timer
#include <linux/ctype.h> 
#include <linux/timer.h>

#include <linux/proc_fs.h> 
#include <linux/vmalloc.h>
#include <linux/seq_file.h>
#include <linux/sched/signal.h>

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/delay.h>

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Varsha Singh, James Knee");
MODULE_DESCRIPTION("ENG EC535 Lab4: Traffic Light Module");

/* PINOUT
 * P8
 * 2: DGND      :ground
 * 8: GPIO_67   :red
 * 10: GPIO_68  :yellow
 * 12: GPIO_44  :green
 * 14: GPIO_26  :BTN0
 * 16: GPIO_46  :BTN1
 * 
 * P9
 * 4:DC_3.3V    :3.3V
*/
#define GPIO_RED 67
#define GPIO_YELLOW 68
#define GPIO_GREEN 44
#define GPIO_BTN0 26
#define GPIO_BTN1 46



/* function declarations */
static int __init mytraffic_init(void);
static void __exit mytraffic_exit(void);

/* file operations for mytraffic_fops */
static ssize_t mytraffic_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
static ssize_t mytraffic_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);

/* readable and writable character device file operations */
struct file_operations mytraffic_fops = {
    .owner = THIS_MODULE,
    .read = mytraffic_read,
    .write = mytraffic_write,
};

/* init and exit functions */
module_init(mytraffic_init);
module_exit(mytraffic_exit);

/* initalize mytraffic module */
static int __init mytraffic_init(void);
{
    int ret = 0;
    /* register character device */
    ret = register_chrdev(0, "mytraffic", &mytraffic_fops);
    if (ret < 0) {
        printk(KERN_ALERT "mytraffic: unable to register char device\n");
        return ret;
    }

    /* TODO: register GPIO pins, finish module init */


}


/* read functionality */
static ssize_t mytraffic_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    char status[100];
    snprintf(status, sizeof(status), "Operational Mode %s\tCycle Rate (HZ): %d\red %s\tyellow: %s\tgreen: %s\nPedestrian %s", 
        /* TODO: create mode string and cycle rate persistent variables */
        mode_str[current_mode], cycle_rate,
        (gpio_get_value(GPIO_RED) ? "on" : "off"),
        (gpio_get_value(GPIO_YELLOW) ? "on" : "off"),
        (gpio_get_value(GPIO_GREEN) ? "on" : "off"),
        (gpio_get_value(GPIO_BTN1) ? "on" : "off"));
        /* TODO: the pedestrian will not work, needs to be a persistent variable */
    copy_to_user(buf, status, strlen(status));
    return strlen(status);
}

/* write functionality */
static ssize_t mytraffic_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    char kbuf[10];
    int new_rate;

    copy_from_user(kbuf, buf, count);
    kbuf[count] = '\0';

    /* convert string to int */
    if(kstrtoint(input, 10, &new_rate) == 0)
    {
        if(new_rate >= 1 && new_rate <= 9)
        {
            cycle_rate = nwe_rate;
            pr_info("mytraffic: cycle rate set to %d\n", cycle_rate);
            return count;
        }
    }

    /* ignore other inputs */
    return count;
}