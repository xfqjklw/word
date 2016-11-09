#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/cdev.h>    //cdev_init�Ⱥ�������
#include <asm/uaccess.h>
#include <asm/arch/map.h>  //������S3C24XX_VA_GPIO����

#define DEVICE_NAME "leds"
#define MAX_LEDS_NUM 3

//leds�ṹ��
//status: 1 �� 0 ��
struct leds_dev_t
{
	struct cdev cdev;
	int status[MAX_LEDS_NUM];
}leds_dev;

//led1/2/3�ṹ��
//status: 1 �� 0 ��
struct led_dev_t
{
	struct cdev cdev;
	int status;
} *led_dev;

//class�ṹ�壬�����Զ������豸�ڵ�
static struct class 		*ledsdrv_class;

//gpio�Ĵ��������ַ
volatile unsigned long *gpfcon = NULL;
volatile unsigned long *gpfdat = NULL;

//�������豸��
static int leds_major;

static int leds_drv_ioctl(struct inode *inode, struct file *file, unsigned int cmd,unsigned long arg)
{
	
	int i;
	int minor = iminor(inode);
	printk("leds_drv_ioctl,minor:%d\n",minor);
	
	if(minor == 0)
	{
		struct leds_dev_t *dev = file->private_data;
		if (cmd == 1)
		{
			//ȫ������
			*gpfdat &= ~((1<<4) | (1<<5) | (1<<6));
			for(i = 0; i <  MAX_LEDS_NUM; i++)
			{
				dev->status[i] = 1;
			}	
		}
		else
		{
			//ȫ��Ϩ��
			*gpfdat |= (1<<4) | (1<<5) | (1<<6);
			for(i = 0; i <  MAX_LEDS_NUM; i++)
			{
				dev->status[i] = 0;
			}
		}
	}	
	else
	{	
		struct led_dev_t *dev = file->private_data;
		if(cmd == 1)
		{
			//ĳ������
			*gpfdat &= ~(0x1<<(minor + 3));
			dev->status = 1;
			leds_dev.status[minor-1] = 1;
		}
		else
		{
			//ĳ��Ϩ��
			*gpfdat |= (0x1<<(minor + 3));
			dev->status = 0;
			leds_dev.status[minor-1] = 0;
		}
	}
}
		


static int leds_drv_open(struct inode *inode, struct file *file)
{
	struct leds_dev_t *dev1;
	struct led_dev_t  *dev2;

	//��ȡ���豸��
	int minor = iminor(inode);
	printk("leds_drv_open,minor:%d\n",minor);
	
	if(minor == 0)
	{
		//��struct file���ļ�˽������ָ�뱣��struct mycdev�ṹ��ָ��
		dev1 = container_of(inode->i_cdev,struct leds_dev_t,cdev);
		file->private_data = dev1;
		
		//����GPF4,5,6Ϊ���
		*gpfcon &= ~((0x3<<(4*2)) | (0x3<<(5*2)) | (0x3<<(6*2)));
		*gpfcon |= ((0x1<<(4*2)) | (0x1<<(5*2)) | (0x1<<(6*2)));
	}	
	else
	{	
		//��struct file���ļ�˽������ָ�뱣��struct mycdev�ṹ��ָ��
		dev2=container_of(inode->i_cdev,struct led_dev_t,cdev);
		file->private_data = dev2;
		
		//���򵥶�����ĳ������Ϊ�������
		*gpfcon &= ~(0x3 << ((minor+3)*2)); 
		*gpfcon |= (0x1 << ((minor+3)*2));
	}	

	return 0;
}

static ssize_t leds_drv_write(struct file *file, const char __user *buf, size_t count, loff_t * ppos)
{
	int i;
	int val;

	int minor = iminor(file->f_path.dentry->d_inode);
	printk("leds_drv_write,minor:%d\n",minor);
	
	copy_from_user(&val, buf, count);

	if(minor == 0)
	{
		//��ȡ˽������
		struct leds_dev_t *dev = file->private_data;
		if (val == 1)
		{
			//ȫ������
			*gpfdat &= ~((1<<4) | (1<<5) | (1<<6));
			for(i = 0; i <  MAX_LEDS_NUM; i++)
			{
				dev->status[i] = 1;
			}	
		}
		else
		{
			//ȫ��Ϩ��
			*gpfdat |= (1<<4) | (1<<5) | (1<<6);
			for(i = 0; i <  MAX_LEDS_NUM; i++)
			{
				dev->status[i] = 0;
			}
		}
	}
	else
	{
		struct led_dev_t *dev = file->private_data;
		if(val == 1)
		{
			//ĳ������
			*gpfdat &= ~(0x1<<(minor + 3));
			dev->status = 1;
			leds_dev.status[minor-1] = 1;
		}
		else
		{
			//ĳ��Ϩ��
			*gpfdat |= (0x1<<(minor + 3));
			dev->status = 0;
			leds_dev.status[minor-1] = 0;
		}
	}
			
	return 0;
}

ssize_t leds_drv_read(struct file *file, char *buf, size_t len, loff_t *off)
{
	int minor = iminor(file->f_path.dentry->d_inode);
	printk("leds_drv_read,minor:%d\n",minor);

	if(minor == 0)
	{
		struct leds_dev_t *dev = file->private_data;
		copy_to_user(buf, dev->status, MAX_LEDS_NUM*sizeof(int));
		return (MAX_LEDS_NUM*sizeof(int));
	}
	else
	{
		struct led_dev_t *dev = file->private_data;
		copy_to_user(buf, &dev->status, sizeof(int));
		return sizeof(int);
	}
	
}

//led�ַ��豸������Ӧ�Ĳ�������
static struct file_operations leds_drv_fops = {
    .owner  =   THIS_MODULE,
    .open   =   leds_drv_open,
	.write	=	leds_drv_write,
	.read	=	leds_drv_read,
	.ioctl  =   leds_drv_ioctl,
};


static int leds_drv_init(void)
{	
	int i;
	dev_t dev_id;
	
	//1.�����豸��
	//ע���ַ��豸��ţ���proc/devvices�д���ledsѡ��
	if(leds_major)
	{
		//��̬
		dev_id = MKDEV(leds_major,0);
		register_chrdev_region(dev_id,(1+MAX_LEDS_NUM),DEVICE_NAME);
	}
	else
	{
		//��̬
		alloc_chrdev_region(&dev_id,0,(1+MAX_LEDS_NUM),DEVICE_NAME);
		leds_major = MAJOR(dev_id);
	}
	
	//2.ע���ַ��豸
	//��ʼ������ע��leds cdev
	cdev_init(&leds_dev.cdev, &leds_drv_fops);
	leds_dev.cdev.owner = THIS_MODULE;
	cdev_add(&leds_dev.cdev,dev_id,1);
	
	//��ʼ����ע��led0/1/2/3
	led_dev = kmalloc(MAX_LEDS_NUM*sizeof(struct led_dev_t),GFP_KERNEL);
	for(i = 0; i < MAX_LEDS_NUM; i++)
	{
		dev_id = MKDEV(leds_major,i+1);
		cdev_init(&led_dev[i].cdev, &leds_drv_fops);
		cdev_add(&led_dev[i].cdev, dev_id,1);
	}
	
	//3.�Զ������豸�ڵ�
	//����һ���࣬���������sysfs���� /sys/class/leds
	ledsdrv_class = class_create(THIS_MODULE, DEVICE_NAME);
	
	//mdev daemon�ͻ��Զ������ڵ�/dev/leds
	//����ģ���ʱ���û��ռ��е�mdev���Զ���Ӧdevice_create(��)������ȥ/sysfs��Ѱ�Ҷ�Ӧ����Ӷ������豸�ڵ㡣
	class_device_create(ledsdrv_class, NULL, MKDEV(leds_major, 0), NULL, DEVICE_NAME);
	
	//�����ڵ�/dev/led1/2/3
	for(i = 0; i < MAX_LEDS_NUM; i++)
	{
		class_device_create(ledsdrv_class, NULL, MKDEV(leds_major, i+1), NULL, "led%d", i);
	}
	
	//4.�����ַ��̬ӳ��
	gpfcon = (volatile unsigned long *)ioremap(0x56000050, 16);
	gpfdat = gpfcon + 1;
	
	//Ҳ���Բ��þ�̬�ķ���
	//gpfcon = S3C24XX_VA_GPIO+0x50;
	//gpfdat = gpfcon + 1;

	return 0;
}

static void leds_drv_exit(void)
{
	int i;

	//1.ɾ���豸�ڵ�
	//ɾ��/dev/leds�豸�ڵ�
	class_device_destroy(ledsdrv_class,MKDEV(leds_major,0));
	
	//ɾ��/dev/led1/2/3�豸�ڵ�
	for(i = 0; i < MAX_LEDS_NUM; i++)
	{
		class_device_destroy(ledsdrv_class,MKDEV(leds_major,i+1));
	}
	class_destroy(ledsdrv_class);

	//2.ɾ���ַ��豸
	//ɾ��cdev
	cdev_del(&leds_dev.cdev);
	for(i = 0; i < MAX_LEDS_NUM; i++)
	{
		cdev_del(&led_dev[i].cdev);
	}
	
	//3.�ͷ��豸��
	unregister_chrdev_region(MKDEV(leds_major,0),1+MAX_LEDS_NUM);
	
	//4.�ͷ�����io�ռ�
	iounmap(gpfcon);
	
	kfree(led_dev);
}

module_init(leds_drv_init);
module_exit(leds_drv_exit);

MODULE_LICENSE("GPL");

