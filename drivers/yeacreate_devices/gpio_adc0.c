/*
please read the data from /proc/gpio_adc0
*/
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



struct iio_channel *chan; //定义 IIO 通道结构体；

static int adc_show(struct seq_file *m, void *v)
{
    int value,Vresult;
		iio_read_channel_raw(chan, &value);//读取 AD 采集到的原始数据,原始数据并存入value中；
		/*
		    转换数据,计算公式:Vresult = (Vref * value) / (2^n-1)
			========> 用户需要的电压 = (rk3288标准电压 * 采集的原始数据) / (2^AD位数-1)；
			现采用AD 10位精度采集；
		*/
		Vresult = (1800 * value) / 1023;
		printk("adc_in0:%d mv\n",Vresult);
		seq_printf(m, "%d\n",Vresult);

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
	chan= iio_channel_get(&pdev->dev,NULL); 	//获取 IIO 通道结构体；
	if(!proc_create("gpio_adc0", 0666, NULL, &adc_fops))
		return -ENOMEM;
	return 0;
}

static int adc_remove(struct platform_device *pdev){
	iio_channel_release(chan);
	return 0;

}

static const struct of_device_id adc_of_match[]={
	{.compatible = "yeacreate_devices,gpio_adc0"},
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
	printk("gpio_adc0 init, please read the data from /proc/gpio_adc0 \n");
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

