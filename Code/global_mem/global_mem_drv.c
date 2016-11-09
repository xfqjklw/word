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
#include <linux/errno.h>

#include <asm/uaccess.h>
#include <asm/arch/map.h> 
#include <asm/arch/regs-gpio.h>

#define DEVICE_NAME "global_mem"
#define GLOBAL_MEM_SIZE 4096
#define MEM_CLEAR 		1

struct _global_mem_dev
{
	struct cdev cdev;
	unsigned int current_len;
	unsigned char mem[GLOBAL_MEM_SIZE];
	struct mutex mutex;
	wait_queue_head_t r_wait;
	wait_queue_head_t w_wait;
	struct fasync_struct *async_queue;
}global_mem_dev;

static struct class *global_mem_drv_class;


static int global_mem_drv_fasync(int fd, struct file* file, int mode)
{
	struct _global_mem_dev *dev = file->private_data;
	return fasync_helper(fd,file,mode,&dev->async_queue);
}

static int global_mem_drv_open(struct inode *inode, struct file *file)
{
	struct _global_mem_dev *devp;
	devp = container_of(inode->i_cdev,struct _global_mem_dev,cdev);
	file->private_data = devp;
	
	return 0;
}

static int global_mem_drv_close(struct inode *inode, struct file *file)
{
	global_mem_drv_fasync(-1, file, 0);
	return 0;
}

static ssize_t global_mem_drv_write(struct file *file, const char __user *buf, size_t count, loff_t * ppos)
{
	int ret;
	struct _global_mem_dev *dev = file->private_data;
	DECLARE_WAITQUEUE(wait,current); //�����ȴ�����
	
	mutex_lock(&dev->mutex);
	add_wait_queue(&dev->r_wait,&wait);	//������ȴ�����ͷ
	
	//��������Ѿ�����
	while(dev->current_len == GLOBAL_MEM_SIZE)
	{
		if(file->f_flags & O_NONBLOCK)
		{
			ret = -EAGAIN;
			goto out;
		}
		__set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&dev->mutex);
		
		schedule();
		if(signal_pending(current))
		{
			ret = -ERESTARTSYS;
			goto out2;
		}
		
		//���mem���ˣ���˯��״̬��������������
		mutex_lock(&dev->mutex);
	}
	
	//���д������ݳ�����mem�����ֵ����ֻд�벿��
	if(count > GLOBAL_MEM_SIZE - dev->current_len)
		count = GLOBAL_MEM_SIZE - dev->current_len;
	
	if(copy_from_user(dev->mem+dev->current_len, buf, count))
	{
		ret = -EFAULT;
		goto out;
	}
	else
	{
		//���д��ɹ������³���
		dev->current_len += count;
		printk("write %d bytes,current_len %d\n",count,dev->current_len);
		
		//�����������ΪmemΪ����������ᱻ��д��������
		wake_up_interruptible(&dev->r_wait);
		
		if(dev->async_queue)
		{
			kill_fasync(&dev->async_queue, SIGIO, POLLIN);
			printk(KERN_DEBUG "%s kill SIGIO",__func__);
		}
		
		ret = count;
	}
	
	out:
	mutex_unlock(&dev->mutex);
	out2:
	remove_wait_queue(&dev->w_wait,&wait);
	set_current_state(TASK_RUNNING);
	
	return ret;
}

static ssize_t global_mem_drv_read(struct file *file, char *buf, size_t len, loff_t *off)
{
	int ret;
	struct _global_mem_dev *dev = file->private_data;
	DECLARE_WAITQUEUE(wait,current); //�����ȴ�����
	
	mutex_lock(&dev->mutex);
	add_wait_queue(&dev->r_wait,&wait);	//������ȴ�����ͷ
	
	while(dev->current_len == 0)
	{
		if(file->f_flags & O_NONBLOCK)
		{
			ret = -EAGAIN;
			goto out;
		}
		//���mem��û�����ݣ����ý���״̬Ϊ����
		__set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&dev->mutex);
		
		schedule();//���̵��ȣ�����
		//������źŻ��ѣ����˳�
		if(signal_pending(current))
		{
			ret = -ERESTARTSYS;
			goto out2;
		}
		//����д��������
		//��Ҫ�����ж�current�Ƿ�Ϊ0������
		mutex_lock(&dev->mutex);
		
	}	
	
	if(len > dev->current_len)
		len = dev->current_len;
	
	if(copy_to_user(buf,dev->mem,len))
	{
		//����������û�ʧ�ܣ��˳�
		ret = -EFAULT;
		goto out;
	}
	else
	{
		//����������û��ɹ��������ƶ�mem�е�����
		memcpy(dev->mem,dev->mem+len,dev->current_len-len);
		dev->current_len -= len;
		printk("read %d bytes,current len:%d\n",len,dev->current_len);
		
		//���������һЩ���ݣ�д������������ʱ�����Ի���д����
		wake_up_interruptible(&dev->w_wait);
		ret = len;
	}
	
	out:
	mutex_unlock(&dev->mutex);
	out2:
	remove_wait_queue(&dev->r_wait,&wait);
	set_current_state(TASK_RUNNING);
	
	return ret;

}

static int global_mem_drv_ioctl(struct inode *inode, struct file *file, unsigned int cmd,unsigned long arg)
{
	struct _global_mem_dev *dev = file->private_data;
	switch(cmd)
	{
		case MEM_CLEAR:
			mutex_lock(&dev->mutex);
			memset(dev->mem,0,GLOBAL_MEM_SIZE);
			mutex_unlock(&dev->mutex);
			printk("global_mem clear\n");
			break;
	}
	
	return 0;
}

static int global_mem_drv_poll(struct file *file, struct poll_table_struct *wait)
{
	unsigned int mask = 0;
	struct _global_mem_dev *dev = file->private_data;
	
	mutex_lock(&dev->mutex);
	
	poll_wait(file, &dev->r_wait, wait);
	poll_wait(file, &dev->w_wait, wait);
	
	if(dev->current_len != 0)
	{
		mask |= POLLIN | POLLRDNORM;
	}
	
	if(dev->current_len != GLOBAL_MEM_SIZE)
	{
		mask |= POLLOUT | POLLWRNORM;
	}
	
	mutex_unlock(&dev->mutex);
	
	return mask;
}

static struct file_operations global_mem_drv_fops = {
    .owner   =  THIS_MODULE,
    .open    =  global_mem_drv_open,
	.write	 =  global_mem_drv_write,
	.read	 =	global_mem_drv_read,
	.release =  global_mem_drv_close, 
	.poll    =  global_mem_drv_poll,
	.ioctl	 =  global_mem_drv_ioctl,
	.fasync  =  global_mem_drv_fasync,
};

int global_mem_major;

static int global_mem_drv_init(void)
{
	dev_t dev_id;

	//1.�����豸��
	//ע���ַ��豸��ţ���proc/devices�д���global_memѡ��
	if(global_mem_major)
	{
		//��̬
		dev_id = MKDEV(global_mem_major,0);
		register_chrdev_region(dev_id,1,DEVICE_NAME);
	}
	else
	{
		//��̬
		alloc_chrdev_region(&dev_id,0,1,DEVICE_NAME);
		global_mem_major = MAJOR(dev_id);
	}

	//2.ע���ַ��豸
	//��ʼ������ע��keys cdev
	cdev_init(&global_mem_dev.cdev, &global_mem_drv_fops);
	global_mem_dev.cdev.owner = THIS_MODULE;
	cdev_add(&global_mem_dev.cdev,dev_id,1);
	
	//3.�Զ������豸�ڵ�
	//����һ���࣬���������sysfs���� /sys/class/keys
	global_mem_drv_class = class_create(THIS_MODULE, DEVICE_NAME);
	
	//mdev daemon�ͻ��Զ������ڵ�/dev/keys
	//����ģ���ʱ���û��ռ��е�mdev���Զ���Ӧdevice_create(��)������ȥ/sysfs��Ѱ�Ҷ�Ӧ����Ӷ������豸�ڵ㡣
	class_device_create(global_mem_drv_class, NULL, MKDEV(global_mem_major, 0), NULL, DEVICE_NAME);
	
	//��ʼ���ȴ�����
	init_waitqueue_head(&global_mem_dev.r_wait);
	init_waitqueue_head(&global_mem_dev.w_wait);

	//��ʼ��������
	mutex_init(&global_mem_dev.mutex);
	
	global_mem_dev.current_len = 0;
	
	return 0;
}


static void global_mem_drv_exit(void)
{
	//1.ɾ���豸�ڵ�
	//ɾ��/dev/keys�豸�ڵ�
	class_device_destroy(global_mem_drv_class,MKDEV(global_mem_major,0));
	class_destroy(global_mem_drv_class);

	//2.ɾ���ַ��豸
	//ɾ��cdev
	cdev_del(&global_mem_dev.cdev);
	
	//3.�ͷ��豸��
	unregister_chrdev_region(MKDEV(global_mem_major,0),1);
}

module_init(global_mem_drv_init);
module_exit(global_mem_drv_exit);
MODULE_LICENSE("GPL");
