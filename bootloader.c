/*
 * h9pic-bootloader
 *
 * Created by SQ8KFH on 2020-07-29.
 *
 * Copyright (C) 2020 Kamil Palkowski. All rights reserved.
 */


#include "config.h"
#include <xc.h>
#include "can.h"

#define FLASH_BLOCK_SIZE 64
#define BOOTLOADER_VERSION 1

void StartWrite(void) {
    EECON2 = 0x55;
    EECON2 = 0xAA;
    EECON1bits.WR = 1;       // Start the write
    NOP();
    NOP();
}

void erase_block(uint24_t tblptr) {
    TBLPTR = tblptr;
    EECON1 = 0x94;       // Setup writes
    StartWrite();
}

void write_block(uint16_t block, uint16_t dst_id) {
    uint16_t bytes_remain = FLASH_BLOCK_SIZE;
    uint24_t tblptr = block * FLASH_BLOCK_SIZE;

    erase_block(tblptr);
    
    TBLPTR = tblptr;
	EECON1 = 0x84;       // Setup writes
    while (1) {
        h9msg_t cm;
        CAN_get_msg_blocking(&cm);

        h9msg_t cm_res;
        CAN_init_new_msg(&cm_res);
        cm_res.priority = H9MSG_PRIORITY_HIGH;
        cm_res.seqnum = cm.seqnum;
        cm_res.destination_id = dst_id;

        if (cm.source_id == dst_id && cm.type == H9MSG_TYPE_PAGE_FILL && cm.dlc == 8) {
            for (uint8_t i = 0; i < cm.dlc; ++i) {
                TABLAT = cm.data[i];
                --bytes_remain;
                asm("TBLWT *+");
            }

            if (bytes_remain == 0) {
                cm_res.type = H9MSG_TYPE_PAGE_WRITED;
                cm_res.dlc = 2;
                cm_res.data[0] = (block >> 8) & 0xff;
                cm_res.data[1] = (block) & 0xff;

                asm("TBLRD *-");
                StartWrite();
                //asm("TBLRD *+");
                EECON1bits.WREN = 0;
                CAN_put_msg_blocking(&cm_res);
                break;
            }
            else {
                cm_res.type = H9MSG_TYPE_PAGE_FILL_NEXT;
                cm_res.dlc = 2;
                cm_res.data[0] = (bytes_remain >> 8) & 0xff;
                cm_res.data[1] = (bytes_remain) & 0xff;
                CAN_put_msg_blocking(&cm_res);
            }
        }
        else if (cm.source_id == dst_id && (cm.type & H9MSG_TYPE_GROUP_MASK) == H9MSG_TYPE_GROUP_0) {
            cm_res.type = H9MSG_TYPE_PAGE_FILL_BREAK;

            CAN_put_msg_blocking(&cm_res);
            break;
        }
    }
}

void main(void) {
    INTCONbits.PEIE = 0;
    INTCONbits.GIE = 0;
    CAN_init();
    
    h9msg_t turn_on_msg;
    CAN_init_new_msg(&turn_on_msg);

    turn_on_msg.priority = H9MSG_PRIORITY_HIGH;
    turn_on_msg.type = H9MSG_TYPE_BOOTLOADER_TURNED_ON;
    turn_on_msg.destination_id = H9MSG_BROADCAST_ID;
    turn_on_msg.dlc = 4;
    turn_on_msg.data[0] = BOOTLOADER_VERSION;
    turn_on_msg.data[1] = NODE_CPU_TYPE;
    turn_on_msg.data[2] = (NODE_TYPE >> 8) & 0xff;
    turn_on_msg.data[3] = (NODE_TYPE) & 0xff;
    CAN_put_msg_blocking(&turn_on_msg);
    
    h9msg_t cm;
    while (1) {
        if (CAN_get_msg_blocking(&cm)) {
            if (cm.type == H9MSG_TYPE_PAGE_START && cm.dlc == 2) {
                uint16_t block = cm.data[0] << 8 | cm.data[1];

                h9msg_t cm_res;
                CAN_init_new_msg(&cm_res);

                cm_res.destination_id = cm.source_id;
                cm_res.seqnum = cm.seqnum;
                cm_res.type = H9MSG_TYPE_PAGE_FILL_NEXT;
                cm_res.dlc = 2;
                cm_res.data[0] = (FLASH_BLOCK_SIZE >> 8) & 0xff;
                cm_res.data[1] = (FLASH_BLOCK_SIZE) & 0xff;

                CAN_put_msg_blocking(&cm_res);

                write_block(block, cm.source_id);
            }
            else if (cm.type == H9MSG_TYPE_QUIT_BOOTLOADER && cm.dlc == 0) {
                STKPTR = 0x00;
                RESET();
                //asm  ("goto 0x0000");
            }
        }
        else {
            CAN_put_msg_blocking(&turn_on_msg);
        }
    }
    return;
}
