#include "chardev.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>              /* open */
#include <unistd.h>             /* exit */
#include <sys/ioctl.h>          /* ioctl */
#include<stdint.h>
int ioctl_set_channel(int file_desc, char *ch)
{
    int ret_val;
    
    ret_val = ioctl(file_desc, IOCTL_GET_Channel, ch);

    if (ret_val < 0) {
        printf("ioctl_sel_channel failed:%d\n", ret_val);
        exit(-1);
    }
       
    return 0;
}

int ioctl_set_alignment(int file_desc,char *al)
{
    int ret_val;
    
    ret_val = ioctl(file_desc,IOCTL_GET_allignment, al);

    if (ret_val < 0) {
        printf("ioctl_sel_channel failed:%d\n", ret_val);
        exit(-1);
    }

        
    return 0;
}



int main()
{
    int file_desc, ret_val;
    int adc_ch_no;
    char align;
    uint16_t ADC_out;



     file_desc = open(DEVICE_FILE_NAME, 0);
    if (file_desc < 0) {
        printf("Can't open device file: %s\n", DEVICE_FILE_NAME);
        exit(-1);
    }

        
    printf("Select a ADC channel from 1 to 8 : ");
    scanf("%d",&adc_ch_no); //SELECT CHANNEL

    printf("Channel selected : %d\n",adc_ch_no);
	if(adc_ch_no<0||adc_ch_no>8)
     {
	printf("invalid channel selection\n");
        return 0;
     }

     ioctl_set_channel(file_desc,&adc_ch_no);


    printf("Select the Data alignment 'L' for Left or 'R' for Right : ");
    scanf("%s",&align); //SELECT ALIGN
       if(align=='L'||align=='R')
	{
    
    ioctl_set_alignment(file_desc,&align);

	}
	else 
        {
	printf("Invalid Data alignment selected\n");
	return 0;
	}
    	read(file_desc,&ADC_out,2);   //reading values from device(adc8) file
	if(align=='L')
		{
	          ADC_out = ADC_out>>6;
		
		}
	
	printf("The ADC data from Channel no. %d is = %d\n",adc_ch_no,ADC_out);

    		
    close(file_desc);
    return 0;
}
