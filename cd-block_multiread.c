#include <cd-block.h>
#include <cpu/instructions.h>
#include <smpc/smc.h>
#include <assert.h>

#define CD_STATUS_TIMEOUT 0xAA

int
_svin_cd_block_sector_read_request(uint32_t fad)
{
        const int32_t num_sectors = 20;

        assert(fad >= 150);

        int ret;

        if ((ret = cd_block_cmd_sector_length_set(SECTOR_LENGTH_2048)) != 0) {
                return ret;
        }

        if ((ret = cd_block_cmd_selector_reset(0, 0)) != 0) {
                return ret;
        }

        if ((ret = cd_block_cmd_cd_dev_connection_set(0)) != 0) {
                return ret;
        }

        /* Start reading */
        if ((ret = cd_block_cmd_disk_play(0, fad, num_sectors)) != 0) {
                return ret;
        }

        return 0;
}

int
_svin_cd_block_multiple_sector_read_request(uint32_t fad, int sectors)
{
        const int32_t num_sectors = sectors;

        assert(fad >= 150);

        int ret;

        if ((ret = cd_block_cmd_sector_length_set(SECTOR_LENGTH_2048)) != 0) {
                return ret;
        }

        if ((ret = cd_block_cmd_selector_reset(0, 0)) != 0) {
                return ret;
        }

        if ((ret = cd_block_cmd_cd_dev_connection_set(0)) != 0) {
                return ret;
        }

        /* Start reading */
        if ((ret = cd_block_cmd_disk_play(0, fad, num_sectors)) != 0) {
                return ret;
        }

        return 0;
}

int
_svin_cd_block_sector_read_process(uint8_t *output_buffer)
{
        assert(output_buffer != NULL);

        int ret;

        /* If at least one sector has transferred, we copy it */
        while ((cd_block_cmd_sector_number_get(0)) == 0) {
        }

        if ((ret = cd_block_transfer_data(0, 0, output_buffer,2048)) != 0) {
                return ret;
        }

        return 0;
}


int
_svin_cd_block_sector_read_flush(uint8_t *output_buffer)
{
        assert(output_buffer != NULL);

        /* If at least one sector has transferred, we copy it */
        while ((cd_block_cmd_sector_number_get(0)) != 0) {
                cd_block_transfer_data(0, 0, output_buffer,2048);
        }

        return 0;
}

bool
_svin_cd_block_sector_read_check(void)
{
        /* If at least one sector has transferred, say yes */
        if ((cd_block_cmd_sector_number_get(0)) != 0) {
                return false;
        }

        return true;
}