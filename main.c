#include <stdio.h>
#include <syslog.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>

#include <bcm_host.h>

static inline void rotate90_16bpp_interlace(uint16_t *src, register uint16_t *dst, int src_width, int src_height, uint8_t interlace)
{
    int i, j;
    int j_inc, j_init;
    
    if(interlace == 1)
    {
        //do odds
        j_inc = 2;
        j_init = 1;
    }
    else if(interlace == 2)
    {
        //do evens
        j_inc = 2;
        j_init = 0;
    }
    else
    {
        //no interlace
        j_init = 0;
        j_inc = 1;
    }
    
    for(i=0; i<src_width; i++)
    {
        for(j=j_init; j<src_height; j+=j_inc)
        {
            *(dst+((src_height*i)+j)) = *(src+((src_height-1-j)*src_width) + i);
        }
    }
}

static inline void rotate90_16bpp(uint16_t *src, register uint16_t *dst, int src_width, int src_height)
{
    int i, j;
    
    for(i=0; i<src_width; i++)
    {
        for(j=0; j<src_height; j++)
        {
            *(dst+((src_height*i)+j)) = *(src+((src_height-1-j)*src_width) + i);
        }
    }
}

static inline void copy_16bpp_offset(uint16_t *src, register uint16_t *dst, int src_width, int src_height, int dst_offset_x, int dst_offset_y, int dst_width, int dst_height)
{
    int src_x, src_y;
    uint16_t *dst_ptr;
    
    
    
    for(src_y=0; src_y<src_height; src_y++)
    {
        dst_ptr = dst + (dst_width * dst_offset_y) + (dst_width * src_y) + dst_offset_x;
        
        /*for(src_x=0; src_x<src_width; src_x++)
         {
         *(dst_ptr + src_x) = *(src+(src_y*src_width) + src_x);
         }*/
        memcpy(dst_ptr,src+(src_y*src_width),src_width*2);
    }
}


static inline void copy_16bpp_offset_and_rotate(uint16_t *src, register uint16_t *dst, int src_width, int src_height, int dst_offset_x, int dst_offset_y, int dst_width, int dst_height)
{
    int src_x, src_y;
    uint16_t *dst_ptr;
    
    
    
    for(src_y=0; src_y<src_height; src_y++)
    {
        dst_ptr = dst + (dst_width * dst_offset_y) + (dst_width * src_y) + dst_offset_x;
        
        for(src_x=0; src_x<src_width; src_x++)
        {
            //            *(dst_ptr + src_x) = *(src+(src_y*src_width) + src_x);
            *(dst_ptr+((dst_height*src_y)+src_x)) = *(src+((src_height-1-src_x)*src_width) + src_y);
        }
        //memcpy(dst_ptr,src+(src_y*src_width),src_width*2);
    }
}


int process() {
    DISPMANX_DISPLAY_HANDLE_T display;
    DISPMANX_MODEINFO_T display_info;
    DISPMANX_RESOURCE_HANDLE_T screen_resource;
    VC_IMAGE_TRANSFORM_T transform;
    uint32_t image_prt;
    VC_RECT_T rect1;
    int ret;
    int fbfd = 0;
    char *fbp = 0;
    uint8_t portrait_mode_pri = 0;
    uint8_t portrait_mode_sec = 0;
    uint8_t rotate_screen = 0;
    
    uint8_t overscan_screen = 1;
    int scaled_w = 16*19;//304 (but seemingly needs to be in multiples of 16)
    int scaled_h = 203;  //GBA viewport should be 3:2
    int offset_x = 16;
    int offset_y = 8;
    
    
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    
    
    bcm_host_init();
    
    display = vc_dispmanx_display_open(0);
    if (!display) {
        syslog(LOG_ERR, "Unable to open primary display");
        return -1;
    }
    ret = vc_dispmanx_display_get_info(display, &display_info);
    if (ret) {
        syslog(LOG_ERR, "Unable to get primary display information");
        return -1;
    }
    syslog(LOG_INFO, "Freeplay-fbcp Primary display is %d x %d", display_info.width, display_info.height);
    printf("Freeplay-fbcp Primary display is %d x %d\n", display_info.width, display_info.height);
    
    
    fbfd = open("/dev/fb1", O_RDWR);
    if (fbfd == -1) {
        syslog(LOG_ERR, "Unable to open secondary display");
        return -1;
    }
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
        syslog(LOG_ERR, "Unable to get secondary display information");
        return -1;
    }
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
        syslog(LOG_ERR, "Unable to get secondary display information");
        return -1;
    }
    
    syslog(LOG_INFO, "Freeplay-fbcp Second display is %d x %d %dbpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
    printf("Freeplay-fbcp Second display is %d x %d %dbpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
    
    //if vinfo.xres < vinfo.yres, then we are in portrait mode
    if(vinfo.xres < vinfo.yres)
        portrait_mode_sec = 1;
    
    if(display_info.width < display_info.height)
        portrait_mode_pri = 1;
    
    if(portrait_mode_sec != portrait_mode_pri)
    {
        rotate_screen = 1;
        syslog(LOG_INFO, "Set rotate_screen mode on.\n");
        printf("Set rotate_screen mode on.\n");
    }
    
    /*if(rotate_screen)
     {
     screen_resource = vc_dispmanx_resource_create(VC_IMAGE_RGB565, vinfo.yres, vinfo.xres, &image_prt);
     if (!screen_resource) {
     syslog(LOG_ERR, "Unable to create screen buffer");
     close(fbfd);
     vc_dispmanx_display_close(display);
     return -1;
     }
     }
     else*/ if(overscan_screen)
     {
         screen_resource = vc_dispmanx_resource_create(VC_IMAGE_RGB565, scaled_w, scaled_h, &image_prt);
         if (!screen_resource) {
             syslog(LOG_ERR, "Unable to create screen buffer");
             close(fbfd);
             vc_dispmanx_display_close(display);
             return -1;
         }
     }
     else
     {
         screen_resource = vc_dispmanx_resource_create(VC_IMAGE_RGB565, vinfo.xres, vinfo.yres, &image_prt);
         if (!screen_resource) {
             syslog(LOG_ERR, "Unable to create screen buffer");
             close(fbfd);
             vc_dispmanx_display_close(display);
             return -1;
         }
     }
    
    fbp = (char*) mmap(0, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fbp <= 0) {
        syslog(LOG_ERR, "Unable to create mamory mapping");
        close(fbfd);
        ret = vc_dispmanx_resource_delete(screen_resource);
        vc_dispmanx_display_close(display);
        return -1;
    }
    
    
    /*if(rotate_screen)
     {
     //uint8_t interlace = 1;
     vc_dispmanx_rect_set(&rect1, 0, 0, vinfo.yres, vinfo.xres);
     
     void *image_orig = calloc( 1, vinfo.yres * vinfo.xres * vinfo.bits_per_pixel / 8 );
     //image_rotated = calloc( 1, vinfo.yres * vinfo.xres * vinfo.bits_per_pixel / 8 );
     if(!image_orig)
     return -1;
     
     //rotate before sending to fb1
     while (1) {
     //snapshot the display
     ret = vc_dispmanx_snapshot(display, screen_resource, 0);
     
     //COULD MAYBE USE third param of vc_dispmanx_snapshot to set snapshot rotation
     //
     // Bottom 2 bits sets the orientation (BUT DOESN'T SEEM TO WORK)
     //DISPMANX_NO_ROTATE = 0,
     //DISPMANX_ROTATE_90 = 1,
     //DISPMANX_ROTATE_180 = 2,
     //DISPMANX_ROTATE_270 = 3,
     
     
     //read data from snapshot into image_orig
     vc_dispmanx_resource_read_data(screen_resource, &rect1, image_orig, vinfo.yres * vinfo.bits_per_pixel / 8);
     
     //rotate the data (straight to the fbp [but we could rotate to another buffer and then memcpy])
     rotate90_16bpp((uint16_t *)image_orig, (uint16_t *)fbp, vinfo.yres, vinfo.xres);
     
     
     usleep(20 * 1000);//sleep N microseconds
     }
     }
     else*/ if(overscan_screen)
     {
         
         vc_dispmanx_rect_set(&rect1, 0, 0, scaled_w, scaled_h);
         
         void *image_scaled = calloc( 1, scaled_w * scaled_h * vinfo.bits_per_pixel / 8 );
         void *image_fb_temp = calloc( 1, vinfo.yres * vinfo.xres * vinfo.bits_per_pixel / 8 );
         
         
         if(!image_scaled)
             return -1;
         
         while (1) {
             char *fbp_offset;
             
             //snapshot the display
             ret = vc_dispmanx_snapshot(display, screen_resource, 0);
             
             //read data (and scale) from snapshot into image_scaled
             vc_dispmanx_resource_read_data(screen_resource, &rect1, image_scaled, scaled_w * vinfo.bits_per_pixel / 8);
             
             if(rotate_screen)
             {
                 //this mode is where we are rotating the image here in software
                 
                 //in rotation mode, xres is the width of the LCD in portrait mode (240)
                 //in rotation mode, yres is the height of the LCD in portrait mode (320)
                 
                 //copy from scaled image to image_fb_temp
                 copy_16bpp_offset((uint16_t *)image_scaled, (uint16_t *)image_fb_temp, scaled_w, scaled_h, offset_x, offset_y, vinfo.yres, vinfo.xres);
                 
                 //rotate image_fb_temp to the LCD (fbp)
                 rotate90_16bpp((uint16_t *)image_fb_temp, (uint16_t *)fbp, vinfo.yres, vinfo.xres);
                 
                 //copy_16bpp_offset_and_rotate((uint16_t *)image_scaled, (uint16_t *)fbp, scaled_w, scaled_h, offset_x, offset_y, vinfo.yres, vinfo.xres);
             }
             else
             {
                 //no rotation means that the LCD hardware/driver is handling the rotation
                 
                 //xres is the width of the LCD in landscape mode (320)
                 //yres is the height of the LCD in landscape mode (240)
                 
                 //no rotation, so just push the data to the proper area of the LCD (fbp)
                 copy_16bpp_offset((uint16_t *)image_scaled, (uint16_t *)fbp,           scaled_w, scaled_h, offset_x, offset_y, vinfo.xres, vinfo.yres);
             }
             
             usleep(20 * 1000);//sleep N microseconds
         }
     }
     else
     {
         vc_dispmanx_rect_set(&rect1, 0, 0, vinfo.xres, vinfo.yres);
         while (1) {
             ret = vc_dispmanx_snapshot(display, screen_resource, 0);
             vc_dispmanx_resource_read_data(screen_resource, &rect1, fbp, vinfo.xres * vinfo.bits_per_pixel / 8);
             usleep(25 * 1000);//sleep 25 microseconds (when not rotating)
         }
     }
    
    munmap(fbp, finfo.smem_len);
    close(fbfd);
    ret = vc_dispmanx_resource_delete(screen_resource);
    vc_dispmanx_display_close(display);
}

int main(int argc, char **argv) {
    setlogmask(LOG_UPTO(LOG_DEBUG));
    openlog("fbcp", LOG_NDELAY | LOG_PID, LOG_USER);
    
    return process();
}



