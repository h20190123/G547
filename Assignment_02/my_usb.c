#include<linux/module.h>
#include<linux/kernel.h>
#include <linux/usb.h>
#include <linux/types.h>
#include<linux/init.h>
#include<linux/slab.h>

#define BAYADER_PD_VID  0x058f  //idVendor=058f, idProduct=6387, bcdDevice= 1.00
#define BAYADER_PD_PID  0x6387

#define SANDISK_MEDIA_VID  0x0781	 //idVendor=0781, idProduct=5567, bcdDevice= 1.26
#define SANDISK_MEDIA_PID  0x5567

#define MOTO_VID 0x22b8  //idVendor=22b8, idProduct=2e82, bcdDevice= 3.18
#define MOTO_PID 0x2e82
#define USB_IN_ENDPOINT  0x80 //IN ENDPOINT

//#define BOMS_GET_MAX_LUN              0xFE
#define READ_CAPACITY_LENGTH          13
#define REQUEST_SENSE_LENGTH          0x12

#define RETRY_MAX 5
#define SUCCESS 0

#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])


static void usbdev_disconnect(struct usb_interface *interface)
{
	printk(KERN_INFO "USBDEV Device Removed\n");
	return;
}

static struct usb_device_id usbdev_table [] = {
	{USB_DEVICE(BAYADER_PD_VID, BAYADER_PD_PID)},
	{USB_DEVICE(SANDISK_MEDIA_VID, SANDISK_MEDIA_PID)},
	{USB_DEVICE(MOTO_VID, MOTO_PID)},
	{} /*terminating entry*/	
};
// Section 5.1: Command Block Wrapper (CBW)
struct command_block_wrapper {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};

// Section 5.2: Command Status Wrapper (CSW)
struct command_status_wrapper {
	uint8_t dCSWSignature[4];
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
};

static uint8_t cdb_length[256] = {
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


//function to send mass storage command from host to device//
static int send_mass_storage_command(struct usb_device *usbdev, uint8_t endpoint, uint8_t lun,
	uint8_t *cdb, uint8_t direction, int data_length, uint32_t *ret_tag)
{
	static uint32_t tag = 1;
	uint8_t cdb_len;
	int i, r,j,k, size;
	unsigned int pipe= usb_sndbulkpipe(usbdev,endpoint);
	typedef struct command_block_wrapper CBW;
	CBW *cbw;
	cbw=(CBW*)kmalloc(sizeof(CBW),GFP_KERNEL);
	printk("send_mass_storage ENDPOINT: %x",endpoint);
	if (cdb == NULL) 
	{
		return -1 ;
	}

	if (endpoint & USB_IN_ENDPOINT) {
		printk("send_mass_storage_command: cannot send command on IN endpoint\n");
		return -1;
	}

	cdb_len = cdb_length[cdb[0]];
	if ((cdb_len == 0) || (cdb_len > sizeof(cbw->CBWCB))) {
		printk("send_mass_storage_command: don't know how to handle this command (%02X, length %d)\n",
			cdb[0], cdb_len);
		return -1;
	}
	
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
	for(k=0;k<16;k++){
	cbw->CBWCB[i]=*(cdb+i);	
	}


	i = 0;
	do {
	r = usb_bulk_msg(usbdev, pipe , (unsigned char*)cbw ,31, &size, 1000);
		// The transfer length must always be exactly 31 bytes.
		if (r == -EPIPE) {
			usb_clear_halt(usbdev, endpoint);
			}
		
		i++;
		
	}
	while ((r == -9)&&(i<RETRY_MAX));
	if (r != SUCCESS){
	printk(KERN_ERR "   send_mass_storage_command again : ERROR CODE %d" ,r);
	return -1;
	}
	printk(" sent %d CDB bytes\n", cdb_len);
	return 0;
      

}
//function to Get mass storage status function//
static int get_mass_storage_status(struct usb_device *usbdev, uint8_t endpoint, uint32_t expected_tag)
{
	int i, r, size;
	typedef struct command_status_wrapper CSW;
	CSW *csw;
	csw=(CSW*)kmalloc(sizeof(CSW),GFP_KERNEL);
	unsigned int pipe= usb_rcvbulkpipe(usbdev,endpoint);
	//printk("get_mass_storage at ENDPOINT:%d ",endpoint);
	// The device is allowed to STALL this transfer. If it does, you have to
	// clear the stall and try again.
	i = 0;
	do {
		r=usb_bulk_msg(usbdev, pipe , (void*)csw ,13, &size, 1000);
	if (r == -EPIPE) {
			usb_clear_halt(usbdev, endpoint); //clearing the stall
		}
		i++;
		
	} 
	while ((r == -9 ) && (i<RETRY_MAX));
	if (csw->dCSWTag != expected_tag)
		return -1;
	if (csw->bCSWStatus) {
		
		if (csw->bCSWStatus == 1)
			return -2;	// request Get Sense
		else
			return -1;
	}
	if (r != SUCCESS) {
		printk(KERN_ERR"Error getting mass storage status\n %d:",r);
		return -1;
	}
	if (size != 13) {
		printk("   get_mass_storage_status: received %d bytes (expected 13)\n", size);
		return -1;
	}
	if (csw->dCSWTag != expected_tag) {
		printk("   get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",
			expected_tag, csw->dCSWTag);
		return -1;
	}

	

	return 0;
}

//function for testing SCSI command supportted by device
static void get_sense(struct usb_device *usbdev, uint8_t endpoint_in, uint8_t endpoint_out)
{
	uint8_t cdb[16];	// SCSI Command Descriptor Block
	uint8_t sense[18];
	uint32_t expected_tag;
	int size,j,k;
	int rc;
	unsigned int pipe= usb_rcvbulkpipe(usbdev,endpoint_in);
	printk("get_sense ENDPOINTS: IN- %x OUT- %x",endpoint_in,endpoint_out);
	// Request Sense
	printk("Request Sense:\n");
	for(j=0;j<18;j++){
	sense[j]=0;
	}
	for(k=0;k<16;k++){
	cdb[k]=0;
	}
	cdb[0]= 0x03;	// Request Sense
	cdb[4]=0x12;
	

	send_mass_storage_command(usbdev, endpoint_out, 0, cdb,USB_IN_ENDPOINT, REQUEST_SENSE_LENGTH, &expected_tag);
	rc=usb_bulk_msg(usbdev, pipe , (unsigned char*)&sense,READ_CAPACITY_LENGTH, &size, 1000);
	if (rc < 0)
	{
		printk("bulk_transfer failed\n");
		return;
	}
	printk("   received %d bytes\n", size);

	if ((sense[0] != 0x70) && (sense[0] != 0x71)) {
		printk("   ERROR No sense data\n");
	} else {
		printk("   ERROR Sense: %X %X %X\n", sense[2]&0x0F, sense[12], sense[13]);
	}
	get_mass_storage_status(usbdev, endpoint_in, expected_tag);
}


// Mass Storage device to test bulk transfers (non destructive test)//
static unsigned int test_mass_storage(struct usb_device *usbdev, uint8_t endpoint_in, uint8_t endpoint_out)
{
	int r, size ,j,k;
	uint8_t size8,*lun;
	uint32_t  max_lba, block_size;
	uint32_t expected_tag;
	uint64_t device_size;
	//uint64_t *capacity;
	uint8_t *cdb;	// SCSI Command Descriptor Block
	unsigned int pipe_r= usb_rcvbulkpipe(usbdev,endpoint_in);
	unsigned int pipe_s= usb_sndbulkpipe(usbdev,endpoint_out);
	unsigned int pipe_c= usb_rcvctrlpipe(usbdev,0);
	unsigned char *buffer ;
	
	buffer= kmalloc(64*sizeof(size8),GFP_KERNEL);
	cdb= kmalloc(16*sizeof(size8),GFP_KERNEL);
	printk("Reading Max LUN:\n");
	r= usb_control_msg(usbdev,pipe_c,0xFE,0xa1,0, 0,(void*)lun,1,1000);
		if (r == 0) {
		lun = 0;
		} else if (r < 0) {
		printk(" control transfer Failed");
		}
	printk("   Max LUN = %d\n", lun);
	printk("test_mass_storage ENDPOINTS: IN- %x OUT- %x",endpoint_in,endpoint_out);
       // Read device capacity
	printk("Reading Capacity:\n");
	for(k=0;k<16;k++){
	cdb[k]=0;
	}
	cdb[0]= 0x25;	// Read capacity
	for(j=0;j<64;j++){
	*(buffer+j)=0;
	}

	send_mass_storage_command(usbdev,endpoint_out,lun,cdb, USB_IN_ENDPOINT, READ_CAPACITY_LENGTH, &expected_tag);	
	usb_bulk_msg(usbdev, pipe_r , (unsigned char*)buffer ,READ_CAPACITY_LENGTH, &size, 1000);
	get_mass_storage_status(usbdev, endpoint_in,expected_tag);	
	printk("   received %d bytes\n", size);
	max_lba = be_to_int32(buffer);
	block_size = be_to_int32(buffer+4);
	device_size = ((uint64_t)(max_lba+1))*block_size/(1024*1024*1024);
	//capacity = &device_size;
	printk(" Max LBA: %X, Block Size: %X (%llx GB)\n",max_lba,block_size,device_size);
	if (get_mass_storage_status(usbdev, endpoint_in,expected_tag) == -2) {
		get_sense(usbdev, endpoint_in, endpoint_out);
	}
	
return 0;
}
////////////////PROBE function///////////////////
static int usbdev_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	int i;
	
	unsigned char epAddr, epAttr;
	struct usb_endpoint_descriptor *ep_desc;
	struct usb_device *device ;

	uint8_t endpoint_in = 0, endpoint_out = 0;	// default IN and OUT endpoints
    if(id->idProduct == BAYADER_PD_PID && id->idVendor == BAYADER_PD_VID)
	{
		printk(KERN_INFO "ALBAYADER Pendrive Plugged in\n\n");
	}
	else if(id->idProduct == SANDISK_MEDIA_PID && id-> idVendor== SANDISK_MEDIA_VID)
	{
		printk(KERN_INFO "SANDISK PENDRIVE Plugged in\n\n");
	}
	
	else if(id->idVendor == MOTO_VID)
	{
		printk(KERN_INFO "MOTOROLA Media Plugged in\n\n");
	}             

////////////////////////////reading device descriptor////////////////////////////////////
	device = usb_get_dev(interface_to_usbdev(interface));
	printk("          VID: %04X", device->descriptor.idVendor);
	printk("          PID:%04X" , device->descriptor.idProduct);
	printk("      device class: %d\n", device->descriptor.bDeviceClass);
	printk(KERN_INFO "USB INTERFACE CLASS : %x", interface->cur_altsetting->desc.bInterfaceClass);
	printk(KERN_INFO "USB INTERFACE SUB CLASS : %x", interface->cur_altsetting->desc.bInterfaceSubClass);
	printk(KERN_INFO "USB INTERFACE Protocol : %x", interface->cur_altsetting->desc.bInterfaceProtocol);
	printk(KERN_INFO "No. of Altsettings = %d\n",interface->num_altsetting);
	printk(KERN_INFO "No. of Endpoints = %d\n", interface->cur_altsetting->desc.bNumEndpoints);
			
			for(i=0;i<interface->cur_altsetting->desc.bNumEndpoints;i++)
			{
					ep_desc = &interface->cur_altsetting->endpoint[i].desc;
					epAddr = ep_desc->bEndpointAddress;
				// Use the first interrupt or bulk IN/OUT endpoints as default for testing
				if (ep_desc->bmAttributes & 0x03) 
				{
					if (ep_desc->bEndpointAddress & USB_IN_ENDPOINT) 
					{
						if (!endpoint_in){
							endpoint_in = ep_desc->bEndpointAddress;
							printk(KERN_INFO "EP %d is Bulk IN\n", i);
						}
					} 
					else 
					{
						if (!endpoint_out){
							endpoint_out = ep_desc->bEndpointAddress;
							printk(KERN_INFO "EP %d is Bulk OUT\n", i);
						}
					}
				}
				printk("           max packet size: %04X\n", ep_desc->wMaxPacketSize);
				printk("          polling interval: %02X\n", ep_desc->bInterval);
				
			}
	
// Check if the device is USB attaced SCSI type Mass storage class
			
	if(!((interface->cur_altsetting->desc.bInterfaceClass==0x08)
			  &(interface->cur_altsetting->desc.bInterfaceSubClass == 0x01)
			  | (interface->cur_altsetting->desc.bInterfaceSubClass == 0x06)
			  & (interface->cur_altsetting->desc.bInterfaceProtocol == 0x50))) 
			{
				printk(KERN_INFO"This is not a mass storage device");
			}
			else
	test_mass_storage(device, endpoint_in, endpoint_out);

	printk("\n");
return 0;
}

/*Operations structure*/
static struct usb_driver usbdev_driver = {
	name: "usbdev",  //name of the device
	probe: usbdev_probe, // Whenever Device is plugged in
	disconnect: usbdev_disconnect, // When we remove a device
	id_table: usbdev_table, //  List of devices served by this driver
};

static int __init device_init(void)
{	
	printk(KERN_INFO "UAS READ Capacity Driver Inserted");
	return usb_register(&usbdev_driver);
}

static void __exit device_exit(void)
{
	usb_deregister(&usbdev_driver);
}

module_init(device_init);
module_exit(device_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Neha Bharti : H20190123");
