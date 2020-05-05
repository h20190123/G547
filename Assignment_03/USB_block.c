
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include<linux/slab.h>
#include <stdarg.h>
#include<linux/blkdev.h>
#include<linux/bio.h>
#include<linux/genhd.h>
#include<linux/types.h>
#include<linux/init.h>
#include<linux/fs.h>
#include<linux/errno.h>
#include<linux/uaccess.h>
#include<linux/sched.h>
#include<linux/workqueue.h>

#define BULK_EP_OUT 0x02
#define BULK_EP_IN 0x81

#define DEV_NAME "PEN_DRIVE"
#pragma pack(1)
#define READ10                     0x28
#define WRITE10                    0x2A
#define USB_capacity               30031872    //16GB

#define USB_VID 0x0781
#define USB_PID 0x5567 

sector_t start_sector;
sector_t xfer_sectors;
unsigned char* buffer ;
unsigned int offset;
size_t xfer_len;
int major;
static struct usb_device *device;
void block_data_transfer(struct work_struct  *work);
struct block_device_operations my_block_ops =
{
  .owner = THIS_MODULE,

};
struct my_block_struct
{
  spinlock_t lock;
  struct gendisk* gd;
  struct request_queue* queue;
  struct workqueue_struct *usbworkqueue;
};
struct my_block_struct *mydevice=NULL;


struct dwork
{
  struct work_struct work;
  void *rq;
};

struct command_block_wrapper
{
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};


struct command_status_wrapper
{
	uint8_t dCSWSignature[4];
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
};

static uint8_t cdb_length[256] =
{
//	 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  0
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  1
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  2
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  3
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  4
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  5
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  6
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  7
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  8
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  9
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  A
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  B
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  C
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  D
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  E
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  F
};

static int send_mass_storage_command(struct usb_device *udev, uint8_t endpoint, uint8_t lun,uint8_t *cdb, uint8_t direction, int data_length, uint32_t *ret_tag)
{
	static uint32_t tag = 1;
	uint8_t cdb_len;
	int i, r, size;
	struct command_block_wrapper *cbw;
  cbw = (struct command_block_wrapper *)kmalloc(sizeof(struct command_block_wrapper), GFP_KERNEL);

	if (cdb == NULL)
  {
		return -1;
	}

	if (endpoint & BULK_EP_IN)
  {

		return -1;
	}

	cdb_len = cdb_length[cdb[0]];
	if ((cdb_len == 0) || (cdb_len > sizeof(cbw->CBWCB)))
  {
		return -1;
	}

	memset(cbw, 0, sizeof(struct command_block_wrapper));
	cbw->dCBWSignature[0] = 'U';
	cbw->dCBWSignature[1] = 'S';
	cbw->dCBWSignature[2] = 'B';
	cbw->dCBWSignature[3] = 'C';
	*ret_tag = tag;
	cbw->dCBWTag = tag++;
	cbw->dCBWDataTransferLength = data_length;
	cbw->bmCBWFlags = direction;
	cbw->bCBWLUN = lun;
	cbw->bCBWCBLength = cdb_len;
	memcpy(cbw->CBWCB, cdb, cdb_len);

	i = 0;
	do {
		r = usb_bulk_msg(udev, usb_sndbulkpipe(udev, endpoint), (void *)cbw, 31, &size, 1000);
		if (r != 0) {
			usb_clear_halt(udev, usb_sndbulkpipe(udev, endpoint));
		}
		i++;
	} while ((r != 0) && (i<5));


	printk(KERN_INFO "read count = %d\n", size);
	return 0;
}

static int get_mass_storage_status(struct usb_device *udev, uint8_t endpoint, uint32_t expected_tag)
{
	int i, r, size;
	struct command_status_wrapper *csw;
    	csw = (struct command_status_wrapper *)kmalloc(sizeof(struct command_status_wrapper), GFP_KERNEL);
	i = 0;
	do {
		r = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, endpoint), (void *)csw, 13, &size, 1000);
		if (r != 0) {
			usb_clear_halt(udev, endpoint);
		}
		i++;
	} while ((r != 0) && (i<5));
	if (r != 0)
  {
		printk(KERN_INFO " get_mass_storage_status: %d\n", r);
	}
	if (size != 13)
  {
		printk(KERN_INFO " get_mass_storage_status: received %d bytes (expected 13)\n", size);

	}
	if (csw->dCSWTag != expected_tag)
  {
		printk(KERN_INFO " get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n", expected_tag, csw->dCSWTag);

	}
	
	printk(KERN_INFO "Mass Storage Status: %02X (%s)\n", csw->bCSWStatus, csw->bCSWStatus?"FAILED":"Success");
	if (csw->dCSWTag != expected_tag)
		return -1;
	if (csw->bCSWStatus)
  {

		if (csw->bCSWStatus == 1)
			return -2;
		else
			return -1;
	}

	return 0;
}


void block_request(struct request_queue* q)
{

struct request *req;

  struct dwork *dwork = NULL;
  while((req = blk_fetch_request(q)) != NULL)
  {

  if(blk_rq_is_passthrough(req) == 1)
  {
    printk(KERN_INFO "non FS request\n");
    __blk_end_request_all(req, -EIO);
    continue;
  }
  dwork = (struct dwork*)kmalloc(sizeof(struct dwork), GFP_ATOMIC);
if(dwork == NULL)
{
	printk(KERN_INFO "memory allocation for deferred work failed\n");
	__blk_end_request_all(req, -EIO);
	continue;
}
  dwork->rq =req;
  INIT_WORK(&dwork->work,block_data_transfer);
  queue_work(mydevice->usbworkqueue, &dwork->work);

}
}

void block_data_transfer(struct work_struct  *work)
{
  struct bio_vec bvec;
  struct req_iterator iter;
  struct dwork *dwork;


  dwork=container_of(work, struct dwork ,work);
  struct request *req = dwork->rq;
   uint32_t expected_tag;
	uint8_t cdb[16];
	buffer = (char *)kmalloc(sizeof(char), GFP_KERNEL);
	int size, dir;
	int r = 0;
	memset(buffer, 0, sizeof(char));
   rq_for_each_segment(bvec, req, iter)
	{
		start_sector = iter.iter.bi_sector;
		buffer = kmap_atomic(bvec.bv_page);
		offset = bvec.bv_offset;
		xfer_len = bvec.bv_len;
		xfer_sectors = xfer_len/ 512;
		dir = rq_data_dir(req);

		printk(KERN_INFO "start_sector = %d, dir = %d\n", start_sector, dir);

		memset(cdb, 0, sizeof(cdb));

	    	cdb[2] = (start_sector >> 24) & 0xFF ;
        cdb[3] = (start_sector >> 16) & 0xFF ;
        cdb[4] = (start_sector >> 8) & 0xFF ;
        cdb[5] = (start_sector >> 0) & 0xFF ;
        cdb[7] = (xfer_sectors >> 8) & 0xFF ;
        cdb[8] = (xfer_sectors >> 0) & 0xFF ;

		if(dir == 1)                            //Write
		{
			cdb[0] =WRITE10;

			r = send_mass_storage_command(device, BULK_EP_OUT, 0, cdb, 0x00, xfer_len, &expected_tag);
			r = usb_bulk_msg(device, usb_sndbulkpipe(device, BULK_EP_OUT), ((void *) (buffer+offset)) , xfer_len, &size, 0);
			if(r != 0)
			{
				printk(KERN_INFO "writing into drive failed\n");
				usb_clear_halt(device, usb_sndbulkpipe(device, BULK_EP_OUT));
			}
			if (get_mass_storage_status(device, BULK_EP_IN, expected_tag) == -2) {

			}
			printk(KERN_INFO "write to disk complete\n");

		}
		else                                  //Read
		{
			cdb[0] = READ10;

			r = send_mass_storage_command(device, BULK_EP_OUT, 0, cdb, 0x80, xfer_len, &expected_tag);
			r = usb_bulk_msg(device, usb_rcvbulkpipe(device, BULK_EP_IN), ((void *) (buffer+offset)), xfer_len, &size, 0);
			if(r != 0)
			{
				printk(KERN_INFO "reading from drive failed\n");
				usb_clear_halt(device, usb_rcvbulkpipe(device, BULK_EP_IN));
			}
			if (get_mass_storage_status(device, BULK_EP_IN, expected_tag) == -2) {

			}
			printk(KERN_INFO "reading from disk complete\n");

		}
	}
	__blk_end_request_cur(req, 0);
	kunmap_atomic(buffer);
	kfree(dwork);
	return;
}



static int pen_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
   device = interface_to_usbdev(interface);
  mydevice = kmalloc(sizeof(struct my_block_struct),GFP_KERNEL);
  if( (major=register_blkdev(0,DEV_NAME))<0)
  {
    printk(KERN_INFO"Unable to register block device : PEN_DRIVE\n");
    return -EBUSY;
  }
    printk("Major_number:%d\n",major);
    spin_lock_init(&mydevice->lock);
	mydevice->gd = alloc_disk(2);
  if(!mydevice->gd)
  {
    printk(KERN_INFO"Gendisk is not allocated\n");
    unregister_blkdev(major,DEV_NAME);
    kfree(mydevice);
    return -ENOMEM;
  }
    printk(KERN_INFO"Gendisk is allocated\n");

  strcpy(mydevice->gd->disk_name,DEV_NAME);
  mydevice->gd->first_minor = 0;
  mydevice->gd->major = major;
  mydevice->gd->fops = &my_block_ops;

  if(!(mydevice->queue = blk_init_queue(block_request,&mydevice->lock)))
  {
    printk("Request_queue allocated failed\n");
    del_gendisk(mydevice->gd);
    unregister_blkdev(major,DEV_NAME);
    kfree(mydevice);
    return -ENOMEM;
 }
printk("Request_queue allocated \n");

mydevice->usbworkqueue=create_workqueue("usbworkqueue");
if (! mydevice->usbworkqueue)
{
    printk(KERN_INFO"creation of workqueue failed\n");
      return -EBUSY;
}

printk(KERN_INFO"creation of workqueue \n");

  mydevice->gd->queue = mydevice->queue;
  set_capacity(mydevice->gd, 30031872);      //Device Capacity
  mydevice->gd->private_data = mydevice;
  add_disk(mydevice->gd);
  printk(KERN_INFO"Block device successfully registered\n");

  return 0;
}

static void pen_disconnect(struct usb_interface *interface)
{
  blk_cleanup_queue(mydevice->queue);
  del_gendisk(mydevice->gd);
  unregister_blkdev(major,DEV_NAME);
  kfree(mydevice);
  printk(KERN_INFO"Block device unregistered successfully\n");
  printk(KERN_INFO"Device disconnected\n");
   printk(KERN_INFO "Pen drive removed\n");
}

void block_request(struct request_queue* que);


static struct usb_device_id pen_table[] =
{
    { USB_DEVICE(USB_VID, USB_PID) },
    {}
};
MODULE_DEVICE_TABLE (usb, pen_table);

static struct usb_driver pen_driver =
{
    .name = "pen_driver",
    .id_table = pen_table,
    .probe = pen_probe,
    .disconnect = pen_disconnect,
};

static int __init pen_init(void)
{
printk(KERN_INFO "UAS READ Capacity Driver Inserted");
    return usb_register(&pen_driver);
}

static void __exit pen_exit(void)
{
    usb_deregister(&pen_driver);
}

module_init(pen_init);
module_exit(pen_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("H20190122_&_H20190123");
MODULE_DESCRIPTION("USB PenDriver");

