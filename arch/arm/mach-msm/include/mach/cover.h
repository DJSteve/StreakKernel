#ifndef _COVER_H_
#define _COVER_H_
#include <asm/ioctl.h>


#define COVER_IOC_MAGIC 'c'
#define COVER_IOC_DISABLE_DET		_IO(COVER_IOC_MAGIC, 0)
#define COVER_IOC_ENABLE_DET		_IO(COVER_IOC_MAGIC, 1)
#define COVER_IOC_TEST_RUN_CPO_SYNC_AND_RESETCHIP		_IO(COVER_IOC_MAGIC, 2)

#endif

