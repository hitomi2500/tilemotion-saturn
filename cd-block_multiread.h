/*
 * Copyright (c) 2012-2019 Israel Jacquez
 * See LICENSE for details.
 *
 * Israel Jacquez <mrkotfw@gmail.com>
 * Romulo Fernandes Machado <abra185@gmail.com>
 */

#ifndef _CD_BLOCK_MULTIREAD_H_
#define _CD_BLOCK_MULTIREAD_H_

#include <stdint.h>

#include <cd-block/cmd.h>

__BEGIN_DECLS

extern int cd_block_sector_read_request(uint32_t fad);
extern int cd_block_multiple_sector_read_request(uint32_t fad, int sectors);
extern int cd_block_sector_read_process(uint8_t *output_buffer);
extern int cd_block_sector_read_flush(uint8_t *output_buffer);
extern bool cd_block_sector_read_check(void);


__END_DECLS

#endif /* !_CD_BLOCK_MULTIREAD_H_ */
