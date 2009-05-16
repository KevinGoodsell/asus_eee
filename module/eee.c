/*
 *  eee.c - Asus eeePC extras
 *
 *  Copyright (C) 2007 Andrew Tipton
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Ths program is distributed in the hope that it will be useful,
 *  but WITOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTAILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Template Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *  
 *  WARNING:  This is an extremely *experimental* module!  Writing random
 *  data to /proc/eee/fsb is likely to destabilise or even crash your eee.
 *  Writing random data (or even data that appears to be correct) to the PLL
 *  via /proc/eee/pll is almost guaranteed to crash your eee.
 *
 *  That being said, proper use of this module will probably be at least as
 *  stable as running the 8804 BIOS.  Once this module has been loaded into
 *  the kernel, switching to 900Mhz is as simple as:
 *      echo 85 24 0 > /proc/eee/fsb        # 765Mhz
 *      echo 100 24 0 > /proc/eee/fsb       # 900Mhz
 *
 *  Switching back again is likewise simple:
 *      echo 85 24 0 > /proc/eee/fsb        # 765Mhz
 *      echo 70 24 0 > /proc/eee/fsb        # 630Mhz
 *
 *  The first number is the PLL N multiplier, and the second number is the PLL
 *  M divisor.  The reference frequency for the PLL is 24Mhz, so to calculate
 *  the speed in Mhz:
 *      Mhz = 24 * N / M
 *
 *  --------------------------------------------------------------------------
 *
 *  So how did I figure all of this out?  By looking at the ACPI tables which
 *  are included in the BIOS!  In the DSDT, there's the standard ATKD device
 *  (which has lots of ACPI methods to do things with hotkeys etc).  One of its
 *  methods, CFVS, calls a very interesting method \_SB.PCI0.SBRG.FSBA, and
 *  this method uses a variable called FS70...
 *
 *  In short, the FSBA method:
 *    - Calls \_SB.PCI0.SBRG.RCLK() to read the PLL chip's registers
 *    - Calls \_SB.PCI0.SBRG.WCKB() to alter bytes 11 and 12 of those registers
 *    - Calls \_SB.PCI0.SBRG.WCLK() to write those back to the PLL chip
 *
 *  For going 70 -> 100, it pauses briefly at an intermediate step of 85.  For
 *  going 100 -> 70, it also pauses briefly at 85.
 *
 *  Finally, it sends a command 0xE1 (using \_SB.PCI0.SBRG.EC0.ECXW) to the
 *  embedded controller, informing it of the change it just made.  Why?  I
 *  have no idea!  I have found that if I send this command to the EC after
 *  switching to 100Mhz, my external display goes all fuzzy...
 *
 *  The N/M that I'm using above (70/24, 85/24, and 100/24) are straight out of
 *  the BIOS, so I have good reason to believe that they're as stable as the
 *  8804 BIOS ever was.
 *
 *  According to the ICS9LPRS426 datasheet, this shouldn't work...  but it
 *  does!
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <acpi/acpi_drivers.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>


/* Module info */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrew Tipton");
MODULE_DESCRIPTION("Support for eeePC-specific functionality.");
#define EEE_VERSION "0.1"


static union acpi_object * eee_RCLK(void) {
    struct acpi_buffer result;
    union acpi_object *obj;
    acpi_status status;

    status = acpi_evaluate_object(NULL, "\\_SB.PCI0.SBRG.RCLK", NULL, NULL);

    result.length = ACPI_ALLOCATE_BUFFER;
    status = acpi_evaluate_object(NULL, "\\_SB.PCI0.SBRG.CLKB", NULL, &result);
    if (status != AE_OK) {
        printk(KERN_ERR "eee: status 0x%x while evaluating CLKB.\n", status);
        return 0;
    }
    obj = result.pointer;
    if (obj->type != ACPI_TYPE_BUFFER) {
        printk(KERN_ERR "eee: expected a Buffer, got a 0x%x instead.\n", obj->type);
        return 0;
    }
    return obj;
}

static void eee_WCKB(int idx, unsigned char data) {
    struct acpi_object_list params;
    union acpi_object in_params[2];
    acpi_status status;

    params.count = 2;
    params.pointer = in_params;
    in_params[0].type = ACPI_TYPE_INTEGER;
    in_params[0].integer.value = idx;
    in_params[1].type = ACPI_TYPE_INTEGER;
    in_params[1].integer.value = data;

    status = acpi_evaluate_object(NULL, "\\_SB.PCI0.SBRG.WCKB", &params, NULL);
    if (status != AE_OK) {
        printk(KERN_ERR "eee: status 0x%x evaluating WCKB().\n", status);
    }
}

static void eee_ECXW(unsigned char cmd, unsigned char arg) {
    struct acpi_object_list params;
    union acpi_object in_params[2];
    acpi_status status;

    params.count = 2;
    params.pointer = in_params;
    in_params[0].type = ACPI_TYPE_INTEGER;
    in_params[0].integer.value = cmd;
    in_params[1].type = ACPI_TYPE_INTEGER;
    in_params[1].integer.value = arg;

    status = acpi_evaluate_object(NULL, "\\_SB.PCI0.SBRG.EC0.ECXW", &params, NULL);
    if (status != AE_OK) {
        printk(KERN_ERR "eee: status 0x%x evaluating ECXW().\n", status);
    }
}

static void eee_WCLK(void) {
    acpi_status status;
    status = acpi_evaluate_object(NULL, "\\_SB.PCI0.SBRG.WCLK", NULL, NULL);
    if (status != AE_OK) {
        printk(KERN_ERR "eee: status 0x%x evaluating WCLK().\n", status);
    }
}

static void eee_get_freq(int *n, int *m) {
    union acpi_object *clkb;
    clkb = eee_RCLK();
    *m = 0;
    *n = 0;
    if (clkb && clkb->buffer.length > 12) {
        *m = clkb->buffer.pointer[11] & 0x3F;
        *n = clkb->buffer.pointer[12];
    }
}

static void eee_set_freq(int n, int m, int ec) {
    if (ec > 0) {
        eee_ECXW(0xE1, 0x01);
    }
    eee_RCLK();
    eee_WCKB(11, m & 0x3F);
    eee_WCKB(12, n & 0xFF);
    eee_WCLK();
    if (ec == 0) {
        eee_ECXW(0xE1, 0x00);
    }
}


static struct proc_dir_entry *eee_proc_dir;
static struct proc_dir_entry *eee_proc_fsb;
static struct proc_dir_entry *eee_proc_pll;

int eee_proc_fsb_read(char *buffer, char **buffer_location, off_t offset,
                      int buffer_length, int *eof, void *data)
{
    int n = 0;
    int m = 0;
    eee_get_freq(&n, &m);

    if (offset > 0) {
        *eof = 1;
        return 0;
    }

    *eof = 1;
    return sprintf(buffer, "%d %d\n", n, m);
}

int eee_proc_fsb_write(struct file *file, const char *buffer,
                       unsigned long count, void *data)
{
    char userdata[128];
    int n = 0;
    int m = 0;
    int ec = 0;

    if (copy_from_user(userdata, buffer, (count > 128) ? 128 : count)) {
        printk(KERN_DEBUG "eee: copy_from_user() failed\n");
        return -EIO;
    }

    if (sscanf(userdata, "%i %i %i\n", &n, &m, &ec) != 3) {
        printk(KERN_DEBUG "eee: unable to sscanf() the PLL N/M values\n");
        return count;
    }

    eee_set_freq(n, m, ec);
    return count;
}


int eee_proc_pll_read(char *buffer, char **buffer_location, off_t offset,
                      int buffer_length, int *eof, void *data)
{
    union acpi_object *clkb;

    if (offset > 0) {
        *eof = 1;
        return 0;
    }

    clkb = eee_RCLK();
    if (!clkb) {
        return -EIO;
    }

    *eof = 1;
    memcpy(buffer, clkb->buffer.pointer, clkb->buffer.length);
    return clkb->buffer.length;
}

int eee_proc_pll_write(struct file *file, const char *buffer,
                       unsigned long count, void *data)
{
    char userdata[32];
    int bytes = 32;
    int i;
    
    if (count < bytes) {
        bytes = count;
    }

    if (copy_from_user(userdata, buffer, bytes)) {
        printk(KERN_DEBUG "eee: copy_from_user() failed\n");
        return -EIO;
    }

    eee_RCLK();
    for (i = 0; i < bytes; i++) {
        eee_WCKB(i, userdata[i]);
    }
    eee_WCLK();
    return bytes;
}

int init_module(void) {

    /* Create the /proc/eee directory. */
    eee_proc_dir = proc_mkdir("eee", &proc_root);
    if (!eee_proc_dir) {
        printk(KERN_ERR "eee: Unable to create /proc/eee\n");
        return -ENODEV;
    }
    eee_proc_dir->owner = THIS_MODULE;

   
    /* Add the /proc/eee/fsb file. */
    eee_proc_fsb = create_proc_entry("fsb", 0644, eee_proc_dir);
    if (!eee_proc_fsb) {
        printk(KERN_ERR "eee: Unable to create /proc/eee/fsb\n");
        remove_proc_entry("fsb", eee_proc_dir);
        remove_proc_entry("eee", &proc_root);
        return -ENODEV;
    }
    eee_proc_fsb->read_proc = eee_proc_fsb_read;
    eee_proc_fsb->write_proc = eee_proc_fsb_write;
    eee_proc_fsb->owner = THIS_MODULE;
    eee_proc_fsb->mode = S_IFREG | S_IRUGO | S_IWUSR;
    eee_proc_fsb->uid = 0;
    eee_proc_fsb->gid = 0;
    
    /* Add the /proc/eee/pll file. */
    eee_proc_pll = create_proc_entry("pll", 0644, eee_proc_dir);
    if (!eee_proc_fsb) {
        printk(KERN_ERR "eee: Unable to create /proc/eee/pll\n");
        remove_proc_entry("pll", eee_proc_dir);
        remove_proc_entry("fsb", eee_proc_dir);
        remove_proc_entry("eee", &proc_root);
        return -ENODEV;
    }
    eee_proc_pll->read_proc = eee_proc_pll_read;
    eee_proc_pll->write_proc = eee_proc_pll_write;
    eee_proc_pll->owner = THIS_MODULE;
    eee_proc_pll->mode = S_IFREG | S_IRUGO | S_IWUSR;
    eee_proc_pll->uid = 0;
    eee_proc_pll->gid = 0;

    /* Done. */
    printk(KERN_NOTICE "Asus eeePC extras, version %s\n", EEE_VERSION);
    return 0;
}

void cleanup_module(void) {
    remove_proc_entry("pll", eee_proc_dir);
    remove_proc_entry("fsb", eee_proc_dir);
    remove_proc_entry("eee", &proc_root);
}


