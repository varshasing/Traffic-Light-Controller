/* LAB 4, ENG EC 535 SPRING 2025
 * VARSHA SINGH:    U50072781
 * JAMES KNEE:      U14600603 
 * */

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

enum OpMode {
	NORMAL,
	FLASHING_RED,
	FLASHING_YELLOW,
	PEDESTRIAN
};

struct info{
    enum OpMode mode;
    int cycleRate;
    int counter;
    struct timer_list timer;
};

struct info *timerInfo;

static const char *mode_str[] = {
    "NORMAL",
    "FLASHING_RED",
    "FLASHING_YELLOW",
    "NORMAL"
};

static bool pedestrianWaiting = false;
static bool pedestrian_present = false;



/* function declarations */
static int __init mytrafficInit(void);
static void __exit mytrafficExit(void);

/* file operations for mytraffic_fops */
static ssize_t mytrafficRead(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
static ssize_t mytrafficWrite(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);

//declare the rest of the functions 
static void timeStep(struct timer_list *timer);
static void normalMode(void);
static void flashingRed(void);
static void flashingYellow(void);
static void pedestrainMode(void);
static void lightbulb_check(void);
static irqreturn_t mytraffic_btn0_isr(int irq, void *dev_id);
static irqreturn_t mytraffic_btn1_isr(int irq, void *dev_id);

/* readable and writable character device file operations */
struct file_operations mytraffic_fops = {
    .owner = THIS_MODULE,
    .read = mytrafficRead,
    .write = mytrafficWrite,
};

/* init and exit functions */
module_init(mytrafficInit);
module_exit(mytrafficExit);

/* initalize mytraffic module */
static int __init mytrafficInit(void){
    int ret = 0;
    /* register character device */
    ret = register_chrdev(61, "mytraffic", &mytraffic_fops);
    if (ret < 0) {
        printk(KERN_ALERT "mytraffic: unable to register char device\n");
        return ret;
    }

    /* TODO: register GPIO pins, finish module init */
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
    timerInfo = (struct info*)kmalloc(sizeof(struct info), GFP_KERNEL);
    if (!timerInfo) {
        printk(KERN_ALERT "mytraffic: unable to allocate memory for info struct\n");
        mytrafficExit();
        return -ENOMEM;
    }

    // initialize the global timerInfo 
    timerInfo->mode = NORMAL;
    timerInfo->cycleRate = 1;
    timerInfo->counter = 0;
    timer_setup(&timerInfo->timer, timeStep, 0);
    mod_timer(&timerInfo->timer, jiffies + msecs_to_jiffies(timerInfo->cycleRate * 1000));

    /* inital state for traffic lights */
    gpio_set_value(GPIO_RED, 0);
    gpio_set_value(GPIO_YELLOW, 0);
    gpio_set_value(GPIO_GREEN, 0);

    return 0;
}

void timeStep(struct timer_list* timer){

    if(pedestrianWaiting && timerInfo->mode == NORMAL && timerInfo->counter == 4){ //triggers at the start of a red light 
        timerInfo->mode = PEDESTRIAN;
        timerInfo->counter = 0;
        pedestrianWaiting = false;
        pedestrainMode();

    }else{

        switch(timerInfo->mode){
            case NORMAL:
                normalMode();
                break;
            case FLASHING_RED:
                flashingRed();
                break;
            case FLASHING_YELLOW:
                flashingYellow();
                break;
            case PEDESTRIAN:
                pedestrainMode();
                if(timerInfo->counter == 6){//since it still increments by 1
                    pedestrian_present = false;
                    timerInfo->mode = NORMAL;
                    timerInfo->counter = 0;

                    normalMode();
                }
                break;
        }

    }


    mod_timer(&timerInfo->timer, jiffies + msecs_to_jiffies(1000 / timerInfo->cycleRate));
}

void normalMode(){
    if(timerInfo->counter <= 2){
        gpio_set_value(GPIO_RED, 0);
        gpio_set_value(GPIO_YELLOW, 0);
        gpio_set_value(GPIO_GREEN, 1);
    }else if(timerInfo->counter <= 3){
        gpio_set_value(GPIO_RED, 0);
        gpio_set_value(GPIO_YELLOW, 1);
        gpio_set_value(GPIO_GREEN, 0);
    }else{
        gpio_set_value(GPIO_RED, 1);
        gpio_set_value(GPIO_YELLOW, 0);
        gpio_set_value(GPIO_GREEN, 0);
    }

    timerInfo->counter = (++timerInfo->counter) % 6;
}

void flashingRed(){
    if(timerInfo->counter == 0){
        gpio_set_value(GPIO_RED, 1);
    }else{
        gpio_set_value(GPIO_RED, 0);
    }

    timerInfo->counter = (++timerInfo->counter) % 2;
}

void flashingYellow(){
    if(timerInfo->counter == 0){
        gpio_set_value(GPIO_YELLOW, 1);
    }else{
        gpio_set_value(GPIO_YELLOW, 0);
    }

    timerInfo->counter = (++timerInfo->counter) % 2;
}

void pedestrainMode(){
    
    if(timerInfo->counter == 0){
        gpio_set_value(GPIO_RED, 1);
        gpio_set_value(GPIO_YELLOW, 1);
        gpio_set_value(GPIO_GREEN, 0);
    }

    if(timerInfo->counter == 5){
        gpio_set_value(GPIO_RED, 0);
        gpio_set_value(GPIO_YELLOW, 0);
        gpio_set_value(GPIO_GREEN, 0);

    }

    timerInfo->counter = ++timerInfo->counter;
}

/* lightbulb mode */
void lightbulb_check(){
    while(gpio_get_value(GPIO_BTN0) && gpio_get_value(GPIO_BTN1)){
        gpio_set_value(GPIO_RED, 1);
        gpio_set_value(GPIO_YELLOW, 1);
        gpio_set_value(GPIO_GREEN, 1);
    }
    return;
}

/* read functionality */
static ssize_t mytrafficRead(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    if(* f_pos > 0) {
        return 0; // EOF
    }
    char status[200];
    snprintf(status, sizeof(status), "Operational Mode: %s\nCycle Rate: (HZ): %d\nred: %s\nyellow: %s\ngreen: %s\nPedestrian %s\n", 
        /* TODO: create mode string and cycle rate persistent variables */
        mode_str[timerInfo->mode], timerInfo->cycleRate,
        (gpio_get_value(GPIO_RED) ? "on" : "off"),
        (gpio_get_value(GPIO_YELLOW) ? "on" : "off"),
        (gpio_get_value(GPIO_GREEN) ? "on" : "off"),
        ((pedestrian_present) ? "present" : "absent"));
        /* TODO: the pedestrian will not work, needs to be a persistent variable */
    copy_to_user(buf, status, strlen(status));
    *f_pos = strlen(status);
    return strlen(status);
}

/* write functionality */
static ssize_t mytrafficWrite(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    char kbuf[10];
    int new_rate;
    int i;
    int digit;

    copy_from_user(kbuf, buf, count);
    kbuf[count] = '\0';

    for(i = 0; i < count; i++)
    {
        if(isdigit(kbuf[i]))
        {
            digit = kbuf[i] - '0';
            if(digit >= 1 && digit <= 9)
            {
                new_rate = digit;
                timerInfo->cycleRate = new_rate;
                return count;
            }
        }
    }

    return count;
}

static void __exit mytrafficExit(void)
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

    /* free context variable */
    kfree(timerInfo);

    printk(KERN_INFO "mytraffic: module unloaded\n");
    return;
}

/* pressing btn0: posedge switches mode */
static irqreturn_t mytraffic_btn0_isr(int irq, void * dev_id) {

    /* check for lightbulb mode */
    if( gpio_get_value(GPIO_BTN0) && gpio_get_value(GPIO_BTN1))
    {
        lightbulb_check();
        /* switch back to normal mode */
        timerInfo->mode = NORMAL;
        pedestrianWaiting = false;
        pedestrian_present = false;
        timerInfo->cycleRate = 1;
    }
    else{
    /* dont want pedestrian to be affected */
    if(timerInfo->mode == PEDESTRIAN){
        return IRQ_HANDLED;
    }

        switch(timerInfo->mode){
            case NORMAL:
                timerInfo->mode = FLASHING_RED;
                break;
            case FLASHING_RED:
                timerInfo->mode = FLASHING_YELLOW;
                break;
            case FLASHING_YELLOW:
                timerInfo->mode = NORMAL;
                break;
        }
    }

    //reset the light ready for new mode
    gpio_set_value(GPIO_GREEN, 0);
	gpio_set_value(GPIO_RED, 0);
	gpio_set_value(GPIO_YELLOW, 0);

    //resets counter 
	timerInfo->counter = 0;
	timeStep(&(timerInfo->timer)); //makes it switch modes instantly

	return IRQ_HANDLED;
}

//calling the pedestrian function
static irqreturn_t mytraffic_btn1_isr(int irq, void * dev_id) {

    /* check for lightbulb mode */
    if (gpio_get_value(GPIO_BTN0) && gpio_get_value(GPIO_BTN1))
    {
        lightbulb_check();
        /* switch back to normal mode */
        timerInfo->mode = NORMAL;
        pedestrianWaiting = false;
        pedestrian_present = false;

        //reset the light ready for new mode
        gpio_set_value(GPIO_GREEN, 0);
	    gpio_set_value(GPIO_RED, 0);
	    gpio_set_value(GPIO_YELLOW, 0);

        //resets counter 
	    timerInfo->counter = 0;
        timerInfo->cycleRate = 1;
	    timeStep(&(timerInfo->timer)); //makes it switch modes instantly
    }
    else{
        if(timerInfo->mode == NORMAL || timerInfo->mode == PEDESTRIAN){
            pedestrianWaiting = true;
            pedestrian_present = true;
        }
    }
    return IRQ_HANDLED;
}
