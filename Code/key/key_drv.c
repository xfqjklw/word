#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/cdev.h>   
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/poll.h>

#include <asm/uaccess.h>
#include <asm/arch/map.h> 
#include <asm/arch/regs-gpio.h>

#define DEVICE_NAME "keys"
#define MAX_KEY_NUM 	4
#define KEY_DOWN		1//�������� 
#define KEY_UP 			2//����̧�� 
#define KEY_UNCERTAIN 	3//������ȷ��

struct keys_dev_t
{
	struct cdev cdev;

}keys_dev;

static volatile int key_value;						//ȷ�Ϻõļ�ֵ
static volatile int key_status[MAX_KEY_NUM];		//��¼����״̬
static volatile int key_count[MAX_KEY_NUM];			//��¼��������
static volatile int key_last[MAX_KEY_NUM];			//˫���ж�
static struct timer_list keys_timer[MAX_KEY_NUM];	//����ȥ����ʱ��
static struct timer_list keys_double_timer[MAX_KEY_NUM];	//˫���ж϶�ʱ��

static struct class *keysdrv_class;

static DECLARE_WAIT_QUEUE_HEAD(button_waitq);

/* �ж��¼���־, �жϷ����������1��key_drv_read������0 */
static volatile int ev_press = 0;

struct pin_desc{
	unsigned int pin;
	unsigned int irq;
	char name[8];
};

struct pin_desc pins_desc[4] = {
	{S3C2410_GPF0, IRQ_EINT0, "key1"},
	{S3C2410_GPF2, IRQ_EINT2, "key2"},
	{S3C2410_GPG3, IRQ_EINT11,"key3"},
	{S3C2410_GPG11,IRQ_EINT19,"key4"},
};

struct work_struct my_wq;/*����һ����������*/
void my_wq_func(unsigned long);/*����һ��������*/

//�жϴ���װ벿
void my_wq_func(unsigned long t)
{
	printk("one key preesed:%d\n",key_value);
}

static void keys_double_timer_handle(unsigned long data)
{
	//�ȴ�500ms�������ʱ����δ��ɾ��������Ϊ�Ƕ̰�
	int key = data;
	
	key_last[key] = 0;
	key_status[key] = KEY_UP;
	key_count[key] = 0;
	ev_press = 1;
	schedule_work(&my_wq);//��������
	wake_up_interruptible(&button_waitq);  /* �������ߵĽ��� */
}

static void keys_timer_handle(unsigned long data)
{
	
	int key = data;
	int ret;
	unsigned int pinval;
	
	pinval = s3c2410_gpio_getpin(pins_desc[key].pin);

	if(pinval)
	{
		/* �ɿ� */
		if(key_status[key] == KEY_UNCERTAIN)
		{
			//����ɿ��ˣ����count��ֵ���������50(5��)��
			//����Ϊ�ǳ���
			if(key_count[key] > 50)
			{
				key_value = key+10;			//�����ļ�ֵ
				key_status[key] = KEY_UP;
				key_count[key] = 0;
				ev_press = 1;
				schedule_work(&my_wq);//��������
				wake_up_interruptible(&button_waitq);  /* �������ߵĽ��� */
				
			}
			else 
			{
				//����ɿ��ˣ�count<50,����Ϊ�Ƕ̰�����˫��
				if(key_last[key] == 1)
				{
					//���500ms����һ�α�����
					del_timer(&keys_double_timer[key]);
					key_last[key] = 0;
					key_value = key+20;
					
					key_status[key] = KEY_UP;
					key_count[key] = 0;
					ev_press = 1;
					schedule_work(&my_wq);//��������
					wake_up_interruptible(&button_waitq);  /* �������ߵĽ��� */
				}
				else
				{
					//�̰������ǲ�����ȷ����������500ms�᲻���ٴΰ��£��γ�˫��
					//���Եȴ�500ms�������Ƿ�ᰴ�¡�
					key_value = key;
					key_last[key] = 1;
					mod_timer(&keys_double_timer[key], jiffies+HZ/2);
				}
			}	
			
		}
 	}
	else
	{
		/* ���� */
		if(key_status[key] == KEY_UNCERTAIN)
		{
			//ȷ�ϰ����Ѿ�������
			key_status[key] = KEY_DOWN;
			//����������ʱ�����ж��Ƿ񳤰�
			mod_timer(&keys_timer[key], jiffies+HZ/100);
		}
		else if(key_status[key] == KEY_DOWN)
		{
			//�����ʱ�����ˣ������ڰ��µ�״̬��count++
			//������������ʱ��
			mod_timer(&keys_timer[key], jiffies+HZ/100);
			//printk("%d\n",key_count[key]);
			key_count[key]++;
		}	
	}
}

/*
  * ȷ������ֵ
  */
static irqreturn_t buttons_irq(int irq, void *dev_id)
{
	int key = (int)dev_id;
		
	//���õ�ǰ������״̬Ϊ��ȷ��
	key_status[key] = KEY_UNCERTAIN;
	//���õ�ǰ��������ȥ����ʱ������ʱ��������ʱ��
	mod_timer(&keys_timer[key], jiffies+HZ/100); //����100ms��ʱ 1HZ=1S
			
	return IRQ_RETVAL(IRQ_HANDLED);
}


ssize_t keys_drv_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	if (size != 1)
		return -EINVAL;
	
	//�жϷ�������
	if(file->f_flags & O_NONBLOCK)
	{
		if(ev_press == 0)
			return -EAGAIN;
	}
	else
	{
		/* ���û�а�������, ���� */
		//ev_pressΪ0ʱ������
		wait_event_interruptible(button_waitq, ev_press);
	}
	
	/* ����а�������, ���ؼ�ֵ */
	copy_to_user(buf, &key_value, sizeof(key_value));
	ev_press = 0;
	
	return 0;
}

static int keys_drv_open(struct inode *inode, struct file *file)
{
	/* ����GPF0,2Ϊ�������� */
	/* ����GPG3,11Ϊ�������� */
	//�����ж�
	int i;
	for(i = 0; i < MAX_KEY_NUM; i++)
	{
		request_irq(pins_desc[i].irq,  buttons_irq, IRQT_BOTHEDGE, pins_desc[i].name, (void *)i);
		key_status[i] = KEY_UP;
		key_count[i] = 0;
		key_last[i] = 0;
		setup_timer(&keys_timer[i], keys_timer_handle, i);
		setup_timer(&keys_double_timer[i], keys_double_timer_handle, i);
	}

	return 0;
}

int keys_drv_close(struct inode *inode, struct file *file)
{
	int i;
	for(i = 0; i < MAX_KEY_NUM; i++)
	{
		free_irq(pins_desc[i].irq,  (void *)i);
		del_timer(&keys_timer[i]);
		del_timer(&keys_double_timer[i]);
	}	

	return 0;
}

static int keys_drv_poll(struct file *file, struct poll_table_struct *wait)
{
	unsigned int mask = 0;

	//��ӵȴ����е��ȴ����б���(poll_table)
	poll_wait(file, &button_waitq, wait);
	
	if(ev_press)
	{
		//��ʶ���ݿ��Ի��
		mask |= POLLIN | POLLRDNORM;
	}

	return mask;
} 

static struct file_operations keys_drv_fops = {
    .owner   =  THIS_MODULE,    /* ����һ���꣬�������ģ��ʱ�Զ�������__this_module���� */
    .open    =  keys_drv_open,
	.read	 =	keys_drv_read,
	.release =  keys_drv_close,	 
	.poll    =  keys_drv_poll,
};

int keys_major;
static int keys_drv_init(void)
{
	dev_t dev_id;

	//1.�����豸��
	//ע���ַ��豸��ţ���proc/devices�д���keysѡ��
	if(keys_major)
	{
		//��̬
		dev_id = MKDEV(keys_major,0);
		register_chrdev_region(dev_id,1,DEVICE_NAME);
	}
	else
	{
		//��̬
		alloc_chrdev_region(&dev_id,0,1,DEVICE_NAME);
		keys_major = MAJOR(dev_id);
	}

	//2.ע���ַ��豸
	//��ʼ������ע��keys cdev
	cdev_init(&keys_dev.cdev, &keys_drv_fops);
	keys_dev.cdev.owner = THIS_MODULE;
	cdev_add(&keys_dev.cdev,dev_id,1);
	
	//3.�Զ������豸�ڵ�
	//����һ���࣬���������sysfs���� /sys/class/keys
	keysdrv_class = class_create(THIS_MODULE, DEVICE_NAME);
	
	//mdev daemon�ͻ��Զ������ڵ�/dev/keys
	//����ģ���ʱ���û��ռ��е�mdev���Զ���Ӧdevice_create(��)������ȥ/sysfs��Ѱ�Ҷ�Ӧ����Ӷ������豸�ڵ㡣
	class_device_create(keysdrv_class, NULL, MKDEV(keys_major, 0), NULL, DEVICE_NAME);
	
	INIT_WORK(&my_wq,(void (*)(void*))my_wq_func);/*��ʼ���������в������봦������*/
	
	return 0;
}

static void keys_drv_exit(void)
{
	//1.ɾ���豸�ڵ�
	//ɾ��/dev/keys�豸�ڵ�
	class_device_destroy(keysdrv_class,MKDEV(keys_major,0));
	class_destroy(keysdrv_class);

	//2.ɾ���ַ��豸
	//ɾ��cdev
	cdev_del(&keys_dev.cdev);
	
	//3.�ͷ��豸��
	unregister_chrdev_region(MKDEV(keys_major,0),1);
		
	return;
}

module_init(keys_drv_init);
module_exit(keys_drv_exit);
MODULE_LICENSE("GPL");
