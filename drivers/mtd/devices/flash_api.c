
#include <linux/string.h>
#include <asm/dma.h>
#include <mach/dma.h>
#include <asm/io.h>
#include <linux/vmalloc.h>
#include "msm_nand.h"
#ifdef CONFIG_HW_AUSTIN
    #include "../../../../bootable/bootloader/lk/include/hw/austin_flash_layout.h"
#else   
    #include "../../../../bootable/bootloader/lk/include/hw/toucan_flash_layout.h"
#endif
#define CFG1_WIDE_FLASH (1U << 1)
#define MSM_NAND_BASE 0xA0A00000


#define VALID_START_BLOCK_NO    PART_START_BLK_NO_OEM_LOG
#define VALID_BLOCKS            24     





#define DATA_SIZE               393216  




#define GLBL_CLK_ENA            (MSM_CLK_CTL_BASE + 0x000)
#define GLBL_CLK_STATE          (MSM_CLK_CTL_BASE + 0x004)
#define GLBL_CLK_ENA2           (MSM_CLK_CTL_BASE + 0x220)
#define MASK_ADM_CLK            (0x1 << 5)      
#define MASK_AXI_ADM_CI_CLK_ENA (0x1 << 14)     

#define PHY_ADDR_PTRLIST    0
#define PHY_ADDR_CMDLIST    1024 
#define PHY_ADDR_DATA       2048 
#define PHY_ADDR_SPARE      4096 
#define PHY_ADDR_ECC_CFG_SAVE    4160 
#define PAGE_SIZE_WRITE     2048 
#define PART_SIZE_1M        0x100000

#define BLOCK_SIZE          131072 


#define VALID_START_PAGE_NO    (VALID_START_BLOCK_NO << 6)
#define VIRTUAL_2_DMA(v) (paddr((char *)(v)- (char *)flash_ptrlist))

#define VERBOSE 0
#define paddr(n) ((unsigned) (n))
typedef struct dmov_ch dmov_ch;

static unsigned *flash_ptrlist;
static dmov_s *flash_cmdlist;
static void *flash_spare;
static void *flash_data;
static void *flash_tmpbuf_HeadBlk;
static void *flash_tmpspare_HeadBlk;
static void *flash_tmpbuf_TailBlk;
static void *flash_tmpspare_TailBlk;
static void *flash_testData;
static unsigned CFG0, CFG1;
char* fiq_dmabuffer=NULL;

int flash_init(void);
static int flash_read_page(unsigned page, void *data, void *extra);
static int flash_write_page(unsigned _page, unsigned extra_per_page, const void *data, unsigned bytes, unsigned* badBlocks);
int flash_write_offset(unsigned offset, const void *data, unsigned bytes, unsigned *badBlocks);
static int flash_erase_block(dmov_s *cmdlist, unsigned *ptrlist, unsigned page);
static int _flash_write_page(dmov_s *cmdlist, unsigned *ptrlist, unsigned page,
                             const void *_addr, const void *_spareaddr);
#if 0
static void hexdump(void *ptr, unsigned len);
#endif
static int isFalshBusy(dmov_s *cmdlist, unsigned *ptrlist);
asmlinkage int fiq_printk(const char *fmt, ...);
#ifdef CONFIG_DEBUG_LL
extern void printascii(char *);
#else
#define printascii(a) do{}while(0)
#endif

asmlinkage int fiq_printk(const char *fmt, ...) 
{
	va_list vargs;
	int ret = 0;
	unsigned char buf[1024]; 
	unsigned long temp;
	
	__asm__ __volatile__(                                   \
	"mrs    %0, cpsr                @ stf\n"                \
	: "=r" (temp)                                           \
	:                                                       \
	: "memory", "cc");
	if((temp & 0x40) == 0 || (temp & 0x80) == 0) return ret;
	
	va_start(vargs, fmt);
	ret = vscnprintf(buf,sizeof(buf), fmt, vargs);
	if(ret) printascii(buf);
	va_end(vargs);
	return ret;	
}

struct dmov_ch 
{
    volatile unsigned cmd;
    volatile unsigned result;
    volatile unsigned status;
    volatile unsigned config;
};

static void dmov_prep_ch(dmov_ch *ch, unsigned id)
{
    ch->cmd = (unsigned)    (DMOV_CMD_PTR(id) + MSM_DMOV_BASE);
    ch->result =(unsigned)  (DMOV_RSLT(id)+ MSM_DMOV_BASE);
    ch->status =(unsigned)  (DMOV_STATUS(id)+ MSM_DMOV_BASE);
    ch->config =(unsigned)  (DMOV_RSLT_CONF(id)+ MSM_DMOV_BASE);
}

#define SRC_CRCI_NAND_CMD  CMD_SRC_CRCI(DMOV_NAND_CRCI_CMD)
#define DST_CRCI_NAND_CMD  CMD_DST_CRCI(DMOV_NAND_CRCI_CMD)
#define SRC_CRCI_NAND_DATA CMD_SRC_CRCI(DMOV_NAND_CRCI_DATA)
#define DST_CRCI_NAND_DATA CMD_DST_CRCI(DMOV_NAND_CRCI_DATA)

static int dmov_exec_cmdptr(unsigned id, unsigned *ptr)
{
    dmov_ch ch;
    unsigned n;


    dmov_prep_ch(&ch, id);
    writel(DMOV_CMD_PTR_LIST | DMOV_CMD_ADDR(PHY_ADDR_PTRLIST), ch.cmd);
    while(!(readl(ch.status) & DMOV_STATUS_RSLT_VALID)) ;
    n = readl(ch.status);
    while(DMOV_STATUS_RSLT_COUNT(n)) {
        n = readl(ch.result);
        if(n != 0x80000002) {
            fiq_printk("ERROR: result: %x\n", n);
            fiq_printk("ERROR:  flush: %x %x %x %x\n",
                    readl(DMOV_FLUSH0(DMOV_NAND_CHAN)),
                    readl(DMOV_FLUSH1(DMOV_NAND_CHAN)),
                    readl(DMOV_FLUSH2(DMOV_NAND_CHAN)),
                    readl(DMOV_FLUSH3(DMOV_NAND_CHAN)));
        }
        n = readl(ch.status);
    }
    return 0;
}

struct data_flash_io {
    unsigned cmd;
    unsigned addr0;
    unsigned addr1;
    unsigned chipsel;
    unsigned cfg0;
    unsigned cfg1;
    unsigned exec;
    unsigned ecc_cfg;
    unsigned ecc_cfg_save;
    struct {
        unsigned flash_status;
        unsigned buffer_status;
    } result[4];
};



static int flash_read_config(dmov_s *cmdlist, unsigned *ptrlist)
{
    cmdlist[0].cmd = CMD_OCB;
    cmdlist[0].src = MSM_NAND_DEV0_CFG0;
    cmdlist[0].dst = PHY_ADDR_SPARE;
    cmdlist[0].len = 4;

    cmdlist[1].cmd = CMD_OCU | CMD_LC;
    cmdlist[1].src = MSM_NAND_DEV0_CFG1;
    cmdlist[1].dst = PHY_ADDR_SPARE + 4;
    cmdlist[1].len = 4;

    *ptrlist = (VIRTUAL_2_DMA(cmdlist) >> 3) | CMD_PTR_LP;


    dmov_exec_cmdptr(DMOV_NAND_CHAN, ptrlist);

    CFG0 = *(unsigned *)flash_spare;
    CFG1 = *((unsigned *)flash_spare+1);

    if((CFG0 == 0) || (CFG1 == 0)) {
        return -1;
    }
    
    fiq_printk("after readconfig via DMA, nandcfg: %x %x (initial)\n", CFG0, CFG1);
    
	CFG0 = (3 <<  6)  
		|  (516 <<  9)  
		|   (10 << 19)  
		|    (5 << 27)  
		|    (1 << 30)  
		|    (1 << 31)  
		|    (0 << 23); 
	CFG1 = (0 <<  0)  
		|    (7 <<  2)  
		|    (0 <<  5)  
		|  (465 <<  6)  
		|    (0 << 16)  
		|    (2 << 17)  
		|    (1 << 1);  

    fiq_printk("after reconfig, nandcfg: %x %x (used)\n", CFG0, CFG1);

    return 0;
}

static int _flash_read_page(dmov_s *cmdlist, unsigned *ptrlist, unsigned page, void *_addr, void *_spareaddr)
{
    dmov_s *cmd = cmdlist;
    unsigned *ptr = ptrlist;
    struct data_flash_io *data = (void*) (ptrlist + 4);
    unsigned addr = (unsigned) _addr;
    unsigned spareaddr = (unsigned) _spareaddr;
    unsigned n;
    
    data->cmd = MSM_NAND_CMD_PAGE_READ_ECC;
    data->addr0 = page << 16;
    data->addr1 = (page >> 16) & 0xff;
    data->chipsel = 0 | 4; 

        
    data->exec = 1;

    data->cfg0 = CFG0;
    data->cfg1 = CFG1;
    
    data->ecc_cfg = 0x203;

        
    cmd->cmd = CMD_OCB;
    cmd->src = MSM_NAND_EBI2_ECC_BUF_CFG;
    cmd->dst = VIRTUAL_2_DMA(&data->ecc_cfg_save);
    
    
    cmd->len = 4;
    cmd++;

    for(n = 0; n < 4; n++) {
            
        cmd->cmd = DST_CRCI_NAND_CMD;
        cmd->src = VIRTUAL_2_DMA(&data->cmd);
        
        cmd->dst = MSM_NAND_FLASH_CMD;
        cmd->len = ((n == 0) ? 16 : 4);
        cmd++;

        if (n == 0) {
                
            cmd->cmd = 0;
            cmd->src = VIRTUAL_2_DMA(&data->cfg0);
            
            cmd->dst = MSM_NAND_DEV0_CFG0;
            cmd->len = 8;
            cmd++;

                
            cmd->cmd = 0;
            cmd->src = VIRTUAL_2_DMA(&data->ecc_cfg);
            
            cmd->dst = MSM_NAND_EBI2_ECC_BUF_CFG;
            cmd->len = 4;
            cmd++;
        }
            
        cmd->cmd = 0;
        cmd->src = VIRTUAL_2_DMA(&data->exec);
        
        cmd->dst = MSM_NAND_EXEC_CMD;
        cmd->len = 4;
        cmd++;

            
        cmd->cmd = SRC_CRCI_NAND_DATA;
        cmd->src = MSM_NAND_FLASH_STATUS;
        cmd->dst = VIRTUAL_2_DMA(&data->result[n]);
        
        cmd->len = 8;
        cmd++;

            
        cmd->cmd = 0;
        cmd->src = MSM_NAND_FLASH_BUFFER;
        cmd->dst = VIRTUAL_2_DMA(addr + n * 516);
        
        cmd->len = ((n < 3) ? 516 : 500);
        cmd++;
    }

        
    cmd->cmd = 0;
    cmd->src = MSM_NAND_FLASH_BUFFER + 500;
    cmd->dst = VIRTUAL_2_DMA(spareaddr);
    cmd->len = 16;
    cmd++;
    
        
    cmd->cmd = CMD_OCU | CMD_LC;
    cmd->src = VIRTUAL_2_DMA(&data->ecc_cfg_save);
    
    cmd->dst = MSM_NAND_EBI2_ECC_BUF_CFG;
    cmd->len = 4;

    ptr[0] = (VIRTUAL_2_DMA(cmdlist) >> 3) | CMD_PTR_LP;

    dmov_exec_cmdptr(DMOV_NAND_CHAN, ptr);

#if VERBOSE
    fiq_printk("read page %d: status: %x %x %x %x\n",
            page, data[5], data[6], data[7], data[8]);
	for(n = 0; n < 4; n++) {
	    ptr = (unsigned*)(addr + 512 * n);
	    fiq_printk("data%d:   %x %x %x %x\n", n, ptr[0], ptr[1], ptr[2], ptr[3]);
	    ptr = (unsigned*)(spareaddr + 16 * n);
	    fiq_printk("spare data%d   %x %x %x %x\n", n, ptr[0], ptr[1], ptr[2], ptr[3]);
	}
#endif
    
        
    for(n = 0; n < 4; n++) {
        if (data->result[n].flash_status & 0x110) {
            fiq_printk("_flash_read_page error, result[%d].flash_status=0x%x\n", n, data->result[n].flash_status);            
            return -1;
        }
    }
    
    return 0;
}

int flash_init(void)
{
	
    
    
    if (*((unsigned int*)GLBL_CLK_STATE) & MASK_ADM_CLK)
    {
        *((unsigned  int*)GLBL_CLK_ENA2)=*((unsigned int*)GLBL_CLK_ENA2) | MASK_AXI_ADM_CI_CLK_ENA;
        *((unsigned  int*)GLBL_CLK_ENA)=*((unsigned int*)GLBL_CLK_ENA) | MASK_ADM_CLK;        
    }
    while(*((unsigned int*)GLBL_CLK_STATE) & MASK_ADM_CLK);
    
    

    
    if (fiq_dmabuffer == NULL)
    {
        

        if (fiq_dmabuffer == NULL)
        {
            fiq_printk("failed to ioremap for fiq_dmabuffer! \n");
            return -1;
        }
        else
        {
            fiq_printk("ok to ioremap for fiq_dmabuffer!, fiq_dmabuffer = 0x%x\n", (unsigned)fiq_dmabuffer);
        }
    }

        
    flash_ptrlist = (unsigned *)fiq_dmabuffer;
    flash_cmdlist = (dmov_s *)((char *)flash_ptrlist + 1024);
    flash_data = (void *)((char *)flash_cmdlist + 1024);
    flash_spare = (void *)((char *)flash_data + 2048); 
    flash_tmpbuf_HeadBlk = (void *)((char *)flash_spare + 64); 
    flash_tmpspare_HeadBlk = (void *)((char *)flash_tmpbuf_HeadBlk + 131072); 
    flash_tmpbuf_TailBlk = (void *)((char *)flash_tmpspare_HeadBlk + 64); 
    flash_tmpspare_TailBlk = (void *)((char *)flash_tmpbuf_TailBlk + 131072); 
    flash_testData = (void *)((char *)flash_tmpspare_TailBlk + 64);  
    
    fiq_printk("\nflash_ptrlist=0x%x\n", (unsigned)flash_ptrlist);
    fiq_printk("flash_cmdlist=0x%x\n", (unsigned)flash_cmdlist);
    fiq_printk("flash_data=0x%x\n", (unsigned)flash_data);
    fiq_printk("flash_spare=0x%x\n", (unsigned)flash_spare);

    if(flash_read_config(flash_cmdlist, flash_ptrlist)) {
        fiq_printk("ERROR: could not read CFG0/CFG1 state\n");
        for(;;);
    }
        
    return 0;
}

static int flash_write_page(unsigned _page, unsigned extra_per_page, const void *data, unsigned bytes, unsigned* badBlocks)
{
    unsigned page = _page;
    unsigned lastpage = VALID_START_PAGE_NO + (VALID_BLOCKS<<6);
    unsigned *spare = (unsigned*) flash_spare;
    const unsigned char *image = data;
    unsigned wsize = 2048 + extra_per_page;
    unsigned n;
    int r;

    *badBlocks=0;
    if ((page < VALID_START_PAGE_NO) || (page > lastpage))
        return -1;

    for(n = 0; n < 16; n++) spare[n] = 0xffffffff;

    while(bytes > 0) {
        if(bytes < wsize) {
            fiq_printk("flash_write_image: image undersized (%d < %d)\n", bytes, wsize);
            return -1;
        }
        if(page >= lastpage) {
            fiq_printk("flash_write_image: out of space, page=%d, lastpage=%d\n", page, lastpage);
            return -1;
        }

        if((page & 63) == 0) {
            
            if(flash_erase_block(flash_cmdlist, flash_ptrlist, page)) {
                fiq_printk("flash_write_image: bad block @ %d\n", page >> 6);
                page += 64;
                *badBlocks = *badBlocks + 1;
                continue;
            }
        }
        
        memcpy(flash_data, image, wsize);        
        if(extra_per_page) {
            r = _flash_write_page(flash_cmdlist, flash_ptrlist, page, flash_data, flash_data + 2048);
        } else {
            r = _flash_write_page(flash_cmdlist, flash_ptrlist, page, flash_data, spare);
        }
        if(r) {
            fiq_printk("flash_write_image: write failure @ page %d (src %d)\n", page, image - (const unsigned char *)data);
            image -= (page & 63) * wsize;
            bytes += (page & 63) * wsize;
            page &= ~63;
            if(flash_erase_block(flash_cmdlist, flash_ptrlist, page)) {
                fiq_printk("flash_write_image: erase failure @ page %d\n", page);
            }
            fiq_printk("flash_write_image: restart write @ page %d (src %d)\n", page, image - (const unsigned char *)data);
            page += 64;
            continue;
        }
        else
        {
            
        }
        page++;
        image += wsize;
        bytes -= wsize;
    }

#if 0   
        
    page = (page + 63) & (~63);
    while(page < lastpage){
        if(flash_erase_block(flash_cmdlist, flash_ptrlist, page)) {
            fiq_printk("flash_write_image: bad block @ %d\n", page >> 6);
        }
        page += 64;
    }
#endif    

    
    return 0;
}



int flash_write_offset(unsigned offset, const void *data, unsigned bytes, unsigned *badBlocks)
{
    unsigned head_block, offset_in_HeadBlk,
             tail_block, offset_in_TailBlk, 
             badBlks_inHead=0, badBlks_inMid=0, badBlks_inTail=0; 
    int i, ret, bytes_to_be_appended, endPos, middle_blocks;
    
    
    if ((offset+bytes) > VALID_BLOCKS * 131072)
    {
        fiq_printk("out of range\n");   
        return -1;
    }
    
    
    head_block= (offset & ~(BLOCK_SIZE-1)) / BLOCK_SIZE + VALID_START_BLOCK_NO;
    offset_in_HeadBlk = offset & (BLOCK_SIZE-1);
    
    
    endPos = offset + bytes - 1;
    tail_block = (endPos & ~(BLOCK_SIZE-1)) / BLOCK_SIZE + VALID_START_BLOCK_NO;
    offset_in_TailBlk = endPos & (BLOCK_SIZE-1);
    
    
    while(1)
    {
        if (!isFalshBusy(flash_cmdlist, flash_ptrlist))
        {
            break;
        }
    }
    


    
    
    for (i=0; i<64; i++)
    {
        flash_read_page((head_block<<6)+i, (char*)flash_tmpbuf_HeadBlk + i * PAGE_SIZE_WRITE, flash_tmpspare_HeadBlk); 
    }
    

    bytes_to_be_appended = min(bytes, BLOCK_SIZE - offset_in_HeadBlk);
    memcpy(flash_tmpbuf_HeadBlk + offset_in_HeadBlk, data, bytes_to_be_appended);
    ret = flash_write_page((head_block<<6), 0, flash_tmpbuf_HeadBlk, BLOCK_SIZE, &badBlks_inHead);
    if (ret)
    {
        fiq_printk(" write the head block FAILED..., startPage_HeadBlock=%d\n", (head_block<<6));    
        return -1;
    }
    else
    {
        head_block += badBlks_inHead;
        tail_block += badBlks_inHead;       
    }
    data = (char *)data + bytes_to_be_appended;
    
    
    middle_blocks = (tail_block - head_block - 1);
    if(middle_blocks > 0)
    {
        fiq_printk("middle blocks, tail_block=%d, head_block=%d, offset=%d \n", tail_block, head_block, offset);
        ret = flash_write_page((head_block<<6) + 64, 0, data, middle_blocks * BLOCK_SIZE, &badBlks_inMid);
        if (ret)
        {
            fiq_printk(" write middle blocks FAILED..., startPage of middle blocks=%d\n", (head_block<<6) + 64);    
            return -1;
        }    
        else
        {
            tail_block += badBlks_inMid;                    
        }
            
        data = (char *)data + middle_blocks * BLOCK_SIZE;
    }
    
    
    
    if (tail_block > head_block)
    {
        
        for (i=0; i<64; i++)
        {
            flash_read_page((tail_block<<6)+i, (char*)flash_tmpbuf_TailBlk + i * PAGE_SIZE_WRITE, flash_tmpspare_TailBlk); 
        }
        
        memcpy(flash_tmpbuf_TailBlk, data, offset_in_TailBlk+1);
        ret = flash_write_page((tail_block<<6), 0, flash_tmpbuf_TailBlk, BLOCK_SIZE, &badBlks_inTail);
        if (ret)
        {
            fiq_printk("write the Tail block FAILED..., startPage_TailBlock = %d\n", (tail_block<<6));        
            return -1;
        }
    }
    
    *badBlocks = badBlks_inHead + badBlks_inMid + badBlks_inTail;
    return ret;
}


static int flash_read_page(unsigned page, void *data, void *extra)
{
    int ret;
  
    ret =  _flash_read_page(flash_cmdlist, flash_ptrlist, 
                            page, flash_data, flash_spare);

    memcpy(data, flash_data, PAGE_SIZE_WRITE);
    memcpy(extra, flash_spare, 64);
    
    
    return ret;
}

#if 0
void hexdump(void *ptr, unsigned len)
{
    unsigned char *b = ptr;
    int count = 0;

    fiq_printk("%x: ", (unsigned) b);
    while(len-- > 0) {
        fiq_printk("%02x ", *b++);
        if(++count == 16) {
            fiq_printk("\n%x: ", (unsigned) b);
            count = 0;
        }
    }
    if(count != 0) fiq_printk("\n");
}
#endif

static int _flash_write_page(dmov_s *cmdlist, unsigned *ptrlist, unsigned page,
                             const void *_addr, const void *_spareaddr)
{
    dmov_s *cmd = cmdlist;
    unsigned *ptr = ptrlist;
    struct data_flash_io *data = (void*) (ptrlist + 4);
    unsigned addr = (unsigned) _addr;
    unsigned spareaddr = (unsigned) _spareaddr;
    unsigned n;    

    data->cmd = MSM_NAND_CMD_PRG_PAGE;
    data->addr0 = page << 16;
    data->addr1 = (page >> 16) & 0xff;
    data->chipsel = 0 | 4; 

    data->cfg0 = CFG0;
    data->cfg1 = CFG1;

        
    data->exec = 1;

    data->ecc_cfg = 0x203;

        
    cmd->cmd = CMD_OCB;
    cmd->src = MSM_NAND_EBI2_ECC_BUF_CFG;
    cmd->dst = VIRTUAL_2_DMA(&data->ecc_cfg_save);
    cmd->len = 4;
    cmd++;

    for(n = 0; n < 4; n++) {
            
        cmd->cmd = DST_CRCI_NAND_CMD;
        cmd->src = VIRTUAL_2_DMA(&data->cmd);
        cmd->dst = MSM_NAND_FLASH_CMD;
        cmd->len = ((n == 0) ? 16 : 4);
        cmd++;

        if (n == 0) {
                
            cmd->cmd = 0;
            cmd->src = VIRTUAL_2_DMA(&data->cfg0);
            cmd->dst = MSM_NAND_DEV0_CFG0;
            cmd->len = 8;
            cmd++;

                
            cmd->cmd = 0;
            cmd->src = VIRTUAL_2_DMA(&data->ecc_cfg);
            cmd->dst = MSM_NAND_EBI2_ECC_BUF_CFG;
            cmd->len = 4;
            cmd++;
        }

            
        cmd->cmd = 0;
        cmd->src = VIRTUAL_2_DMA(addr) + n * 516;
        cmd->dst = MSM_NAND_FLASH_BUFFER;
        cmd->len = ((n < 3) ? 516 : 510);
        cmd++;
        
        if (n == 3) {
                
            cmd->cmd = 0;
            cmd->src = VIRTUAL_2_DMA(spareaddr);
            cmd->dst = MSM_NAND_FLASH_BUFFER + 500;
            cmd->len = 16;
            cmd++;
        }
        
            
        cmd->cmd = 0;
        cmd->src = VIRTUAL_2_DMA(&data->exec);
        cmd->dst = MSM_NAND_EXEC_CMD;
        cmd->len = 4;
        cmd++;

            
        cmd->cmd = SRC_CRCI_NAND_DATA;
        cmd->src = MSM_NAND_FLASH_STATUS;
        cmd->dst = VIRTUAL_2_DMA(&data->result[n]);
        cmd->len = 8;
        cmd++;
    }

        
    cmd->cmd = CMD_OCU | CMD_LC;
    cmd->src = VIRTUAL_2_DMA(&data->ecc_cfg_save);
    cmd->dst = MSM_NAND_EBI2_ECC_BUF_CFG;
    cmd->len = 4;

    ptr[0] = (VIRTUAL_2_DMA(cmdlist) >> 3) | CMD_PTR_LP;   

    dmov_exec_cmdptr(DMOV_NAND_CHAN, ptr);
    
#if VERBOSE
    fiq_printk("write page %d: status: %x %x %x %x\n",
            page, data[5], data[6], data[7], data[8]);
#endif
    
        
    for(n = 0; n < 4; n++) {
        if(data->result[n].flash_status & 0x110) return -1;
        if(!(data->result[n].flash_status & 0x80)) return -1;
    }

    return 0;
}


static int flash_erase_block(dmov_s *cmdlist, unsigned *ptrlist, unsigned page)
{
    dmov_s *cmd = cmdlist;
    unsigned *ptr = ptrlist;
    unsigned *data = ptrlist + 4;

        
    if(page & 63) return -1;

    data[0] = MSM_NAND_CMD_BLOCK_ERASE;   
    data[1] = page;  
    data[2] = 0;     
    data[3] = 0 | 4; 
    data[4] = 1;     
    data[5] = 0xeeeeeeee; 
    data[6] = CFG0 & (~(7 << 6));  
    data[7] = CFG1;
    
    cmd[0].cmd = DST_CRCI_NAND_CMD | CMD_OCB;
    cmd[0].src = VIRTUAL_2_DMA(&data[0]); 
    cmd[0].dst = MSM_NAND_FLASH_CMD;
    cmd[0].len = 16;    

    cmd[1].cmd = 0;
    cmd[1].src = VIRTUAL_2_DMA(&data[6]);
    cmd[1].dst = MSM_NAND_DEV0_CFG0;   
    cmd[1].len = 8;     
    
    cmd[2].cmd = 0;
    cmd[2].src = VIRTUAL_2_DMA(&data[4]);
    cmd[2].dst = MSM_NAND_EXEC_CMD;
    cmd[2].len = 4;

    cmd[3].cmd = SRC_CRCI_NAND_DATA | CMD_OCU | CMD_LC;
    cmd[3].src = MSM_NAND_FLASH_STATUS;
    cmd[3].dst = VIRTUAL_2_DMA(&data[5]);
    cmd[3].len = 4;

    ptr[0] = (VIRTUAL_2_DMA(cmd) >> 3) | CMD_PTR_LP;

    dmov_exec_cmdptr(DMOV_NAND_CHAN, ptr);

#if VERBOSE
    fiq_printk("status: %x\n", data[5]);
#endif
    
        
    if(data[5] & 0x110) 
    {
        fiq_printk("erase error 1\n");
        return -1;
    }
    if(!(data[5] & 0x80)) 
    {
        fiq_printk("erase error 2\n");
        return -1;
    }
    return 0;
}



static int isFalshBusy(dmov_s *cmdlist, unsigned *ptrlist)
{
    unsigned nand_flash_status;
    int isBusy = 0;

    
    cmdlist[1].cmd = CMD_OCB |CMD_OCU | CMD_LC;   
    cmdlist[1].src = MSM_NAND_FLASH_STATUS;
    cmdlist[1].dst = PHY_ADDR_DATA;
    cmdlist[1].len = 4;
    

    *ptrlist = (VIRTUAL_2_DMA(cmdlist) >> 3) | CMD_PTR_LP;
    dmov_exec_cmdptr(DMOV_NAND_CHAN, ptrlist);
    
    
    nand_flash_status = *(unsigned *)(flash_data);
    
    if ((nand_flash_status & (0x1 << 5)) == 0)
    {
        
        isBusy=1;
    }
    else
    {
        
    }        

    return isBusy;
}


