/* LAB 4, ENG EC 535 SPRING 2025
 * VARSHA SINGH:    U50072781
 * JAMES KNEE:      U14600603 */

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
#include <linux/gpio/driver.h>
#include <linux/delay.h>
#include <stdbool.h>

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

/* GPIO pin definitions */
#define GPIO_RED 67
#define GPIO_YELLOW 68
#define GPIO_GREEN 44
#define GPIO_BTN0 26
#define GPIO_BTN1 46

/* enumerate operational modes */
enum operational_modes {
    NORMAL,
    FLASHING_RED,
    FLASHING_YELLOW,
    PEDESTRIAN
};

/* context structure */
struct info {
    enum operational_modes mode;
    int cycle_rate;
    int counter;
    struct timer_list timer;
};

/* global variables */
struct info *mytraffic_info;
static const char *mode_str[] = {
    "NORMAL",
    "FLASHING_RED",
    "FLASHING_YELLOW",
    "PEDESTRIAN"
};
static bool pedestrian_flag = false;

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

/* TODO: create signal handlers for operational mode switching */
/* signal handlers */
/* BTN0: switches modes */
static irqreturn_t mytraffic_btn0_isr(int irq, void *dev_id)
{
    /* ignore mode-switching for pedestrian mode */
    if (mytraffic_info->mode == PEDESTRIAN) {
        return IRQ_HANDLED;
    }
    /* FSM implementation */
    switch (mytraffic_info->mode)
    {
        /* normal -> flashing red */
        case NORMAL:
            mytraffic_info->mode = FLASHING_RED;
            break;
        /* flashing red -> flashing yellow */
        case FLASHING_RED:
            mytraffic_info->mode = FLASHING_YELLOW;
            break;
        /* flashing yellow -> normal */
        case FLASHING_YELLOW:
            mytraffic_info->mode = NORMAL;
            break;
    }

    /* reset lights for new mode */
    gpio_set_value(GPIO_RED, 0);
    gpio_set_value(GPIO_YELLOW, 0);
    gpio_set_value(GPIO_GREEN, 0);

    /* reset counter */
    mytraffic_info->counter = 0;

    /* set traffic lights */
    time_step(&mytraffic_info->timer);
    return IRQ_HANDLED;
}

/* BTN1: pedestrial call button */
static irqreturn_t mytraffic_btn1_isr(int irq, void *dev_id)
{
    /* set pedestrian flag if in normal mode */
    if (mytraffic_info->mode == NORMAL)
    {
        pedestrian_flag = true;
    }
    return IRQ_HANDLED;
}

/* init and exit functions */
module_init(mytraffic_init);
module_exit(mytraffic_exit);

/* initalize mytraffic module */
static int __init mytraffic_init(void);
{
    int ret = 0;
    /* register character device */
    ret = register_chrdev(61, "mytraffic", &mytraffic_fops);
    if (ret < 0) {
        printk(KERN_ALERT "mytraffic: unable to register char device\n");
        return ret;
    }

    /* TODO: check that init is complete */
    /* request GPIO access */
    if (gpio_request(GPIO_RED, "GPIO_RED") < 0) {
        printk(KERN_ALERT "mytraffic: unable to request GPIO_RED\n");
        return -1;
    }
    if (gpio_request(GPIO_YELLOW, "GPIO_YELLOW") < 0) {
        printk(KERN_ALERT "mytraffic: unable to request GPIO_YELLOW\n");
        return -1;
    }
    if (gpio_request(GPIO_GREEN, "GPIO_GREEN") < 0) {
        printk(KERN_ALERT "mytraffic: unable to request GPIO_GREEN\n");
        return -1;
    }
    if (gpio_request(GPIO_BTN0, "GPIO_BTN0") < 0) {
        printk(KERN_ALERT "mytraffic: unable to request GPIO_BTN0\n");
        return -1;
    }
    if (gpio_request(GPIO_BTN1, "GPIO_BTN1") < 0) {
        printk(KERN_ALERT "mytraffic: unable to request GPIO_BTN1\n");
        return -1;
    }

    /* set GPIO direction */
    if (gpio_direction_output(GPIO_RED, 0) < 0) {
        printk(KERN_ALERT "mytraffic: unable to set GPIO_RED direction\n");
        return -1;
    }
    if (gpio_direction_output(GPIO_YELLOW, 0) < 0) {
        printk(KERN_ALERT "mytraffic: unable to set GPIO_YELLOW direction\n");
        return -1;
    }
    if (gpio_direction_output(GPIO_GREEN, 0) < 0) {
        printk(KERN_ALERT "mytraffic: unable to set GPIO_GREEN direction\n");
        return -1;
    }
    if (gpio_direction_input(GPIO_BTN0) < 0) {
        printk(KERN_ALERT "mytraffic: unable to set GPIO_BTN0 direction\n");
        return -1;
    }
    if (gpio_direction_input(GPIO_BTN1) < 0) {
        printk(KERN_ALERT "mytraffic: unable to set GPIO_BTN1 direction\n");
        return -1;
    }
    
    /* register interrupts for buttons */
    if (request_irq(gpio_to_irq(GPIO_BTN0), (irq_handler_t)mytraffic_btn0_isr, IRQF_TRIGGER_RISING, "mytraffic_btn0", NULL) < 0) {
        printk(KERN_ALERT "mytraffic: unable to request IRQ for GPIO_BTN0\n");
        return -1;
    }
    if (request_irq(gpio_to_irq(GPIO_BTN1), (irq_handler_t)mytraffic_btn1_isr, IRQF_TRIGGER_RISING, "mytraffic_btn1", NULL) < 0) {
        printk(KERN_ALERT "mytraffic: unable to request IRQ for GPIO_BTN1\n");
        return -1;
    }

    /* global context variable setup */
    mytraffic_info = (struct info*)kmalloc(sizeof(struct info), GFP_KERNEL);
    if (!mytraffic_info) {
        printk(KERN_ALERT "mytraffic: unable to allocate memory for info struct\n");
        mytraffic_exit();
        return -ENOMEM;
    }

    /* inital state for traffic lights */
    gpio_set_value(GPIO_RED, 0);
    gpio_set_value(GPIO_YELLOW, 0);
    gpio_set_value(GPIO_GREEN, 0);
    
    /* initalize timer information */
    mytraffic_info->mode = NORMAL;
    mytraffic_info->cycle_rate = 1; /* default cycle rate */
    mytraffic_info->counter = 0;
    timer_setup(&mytraffic_info->timer, time_step, 0);
    mod_timer(&mytraffic_info->timer, jiffies + msecs_to_jiffies(1000));
}

/* exit module for mytraffic module */
static void __exit mytraffic_exit(void)
{
    /* free GPIO pins */
    gpio_free(GPIO_RED);
    gpio_free(GPIO_YELLOW);
    gpio_free(GPIO_GREEN);
    gpio_free(GPIO_BTN0);
    gpio_free(GPIO_BTN1);

    /* free IRQs */
    free_irq(gpio_to_irq(GPIO_BTN0), NULL);
    free_irq(gpio_to_irq(GPIO_BTN1), NULL);

    /* unregister character device */
    unregister_chrdev(61, "mytraffic");

    /* free timer */
    del_timer(&mytraffic_info->timer);

    /* free context variable */
    kfree(mytraffic_info);

    printk(KERN_INFO "mytraffic: module unloaded\n");
    return;
}

/* updates state of traffic lights every clock tick */
void time_step(struct timer_list* timer)
{
    /* pedestrian is waiting in normal mode */
    if(pedestrian_flag && mytraffic_info->mode == NORMAL && mytraffic_info->counter == 4)
    {
        mytraffic_info->mode = PEDESTRIAN;
        mytraffic_info->counter = 0;
        pedestrian_flag = false;
        pedestrian_mode();
    }
    else
    {
        switch(mytraffic_info->mode)
        {
            case NORMAL:
                normal_mode();
                break;
            case FLASHING_RED:
                flashing_red_mode();
                break;
            case FLASHING_YELLOW:
                flashing_yellow_mode();
                break;
            case PEDESTRIAN:
                pedestrian_mode();
                break;
            default:
                /* invalid state */
                printk(KERN_ALERT "mytraffic: invalid mode\n");
                break;
        }
    }

    /* TODO: verify mod_timer */
    mod_timer(&mytraffic_info->timer, jiffies + msecs_to_jiffies(1000 / mytraffic_info->cycle_rate));
}

/* traffic light mode declarations */
void normal_mode()
{
    /* light cycles */
    if(mytraffic_info->counter <= 2)
    {
        gpio_set_value(GPIO_RED, 0);
        gpio_set_value(GPIO_YELLOW, 0);
        gpio_set_value(GPIO_GREEN, 1);
    }
    else if(mytraffic_info->counter <= 3)
    {
        gpio_set_value(GPIO_RED, 0);
        gpio_set_value(GPIO_YELLOW, 1);
        gpio_set_value(GPIO_GREEN, 0);
    }
    else
    {
        gpio_set_value(GPIO_RED, 1);
        gpio_set_value(GPIO_YELLOW, 0);
        gpio_set_value(GPIO_GREEN, 0);
    }

    /* increment counter */
    mytraffic_info->counter = (mytraffic_info->counter + 1) % 6;
}

void flashing_red_mode()
{
    /* light cycles */
    if(mytraffic_info->counter == 0)
    {
        gpio_set_value(GPIO_RED, 1);
        gpio_set_value(GPIO_YELLOW, 0);
        gpio_set_value(GPIO_GREEN, 0);
    }
    else
    {
        gpio_set_value(GPIO_RED, 0);
        gpio_set_value(GPIO_YELLOW, 0);
        gpio_set_value(GPIO_GREEN, 0);
    }

    /* increment counter */
    mytraffic_info->counter = (mytraffic_info->counter + 1) % 2;
}

void flashing_yellow_mode()
{
    /* light cycles */
    if(mytraffic_info->counter == 0)
    {
        gpio_set_value(GPIO_RED, 0);
        gpio_set_value(GPIO_YELLOW, 1);
        gpio_set_value(GPIO_GREEN, 0);
    }
    else
    {
        gpio_set_value(GPIO_RED, 0);
        gpio_set_value(GPIO_YELLOW, 0);
        gpio_set_value(GPIO_GREEN, 0);
    }

    /* increment counter */
    mytraffic_info->counter = (mytraffic_info->counter + 1) % 2;
}

/* TODO: pedestrian mode, if we choose to make it a mode */
void pedestrian_mode()
{
    /* light cycles */
    if(mytraffic_info->counter == 0)
    {
        gpio_set_value(GPIO_RED, 1);
        gpio_set_value(GPIO_YELLOW, 1);
        gpio_set_value(GPIO_GREEN, 0);
    }

    if(mytraffic_info->counter == 5)
    {
        gpio_set_value(GPIO_RED, 0);
        gpio_set_value(GPIO_YELLOW, 0);
        gpio_set_value(GPIO_GREEN, 0);
        /* switch back to normal mode */
        mytraffic_info->mode = NORMAL;
        mytraffic_info->counter = 0;
    }

    /* increment counter */
    mytraffic_info->counter = (mytraffic_info->counter + 1) % 6;
}

/* read functionality */
static ssize_t mytraffic_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    char status[100];
    snprintf(status, sizeof(status), "Operational Mode %s\tCycle Rate (HZ): %d\red %s\tyellow: %s\tgreen: %s\nPedestrian %s", 
        mode_str[mytraffic_info->mode], mytraffic_info->cycle_rate,
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
    if(kstrtoint(kbuf, 10, &new_rate) == 0)
    {
        if(new_rate >= 1 && new_rate <= 9)
        {
            mytraffic_info->cycle_rate = new_rate;
            pr_info("mytraffic: cycle rate set to %d\n", mytraffic_info->cycle_rate);
            return count;
        }
    }

    /* ignore other inputs */
    return count;
}