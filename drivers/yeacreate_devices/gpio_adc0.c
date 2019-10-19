#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/wakelock.h>
#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>
#include <linux/iio/consumer.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>



struct iio_channel *chan;

static int adc_show(struct seq_file *m, void *v)
{
    int value,Vresult;
//	while(1)
//	{
		iio_read_channel_raw(chan, &value);
		Vresult = (1800 * value) / 1023;
		printk("adc_in0:%d mv\n",Vresult);
//		msleep(1000);
//	}

    	return 0;
}

static int adc_open(struct inode *inode, struct file *file)
{
    	return single_open(file, adc_show, NULL);
}

static struct file_operations adc_fops = {
    	.owner   = THIS_MODULE,
    	.open    = adc_open,
    	.release = single_release,
    	.read    = seq_read,
    	.llseek  = seq_lseek,
};

static int adc_probe(struct platform_device *pdev){
	struct proc_dir_entry *file;
	chan= iio_channel_get(&pdev->dev,NULL);

	file = proc_create("gpio_adc0", 0666, NULL, &adc_fops);
    	if(!file)
        	return -ENOMEM;

	return 0;
}

static int adc_remove(struct platform_device *pdev){
	iio_channel_release(chan);
	return 0;

}

static const struct of_device_id adc_of_match[]={
	{.compatible = "gpio_adc0",.data = NULL},
	{},
};

MODULE_DEVICE_TABLE(of,adc_of_match);

static struct platform_driver adc_driver = {
	.probe = adc_probe,
	.remove = adc_remove,
	.driver = {
		.name="gpio_adc0",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(adc_of_match),
	},
};


static int __init adc_init(void){
	printk("gpio_adc0 init\n");
	return platform_driver_register(&adc_driver);
}


static void __exit adc_exit(void){
	platform_driver_unregister(&adc_driver);
}

module_init(adc_init);
module_exit(adc_exit);

MODULE_DESCRIPTION("Yea Ceate adc driver");
MODULE_AUTHOR("Support <support@yeacreate.com>");
MODULE_LICENSE("GPL");

