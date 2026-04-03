#ifndef ROCKCHIP_BOT_H
#define ROCKCHIP_BOT_H

#include <stdint.h>

// Device Mode Definitions
typedef enum {
    DEVICE_MODE_UNKNOWN = -1,
    DEVICE_MODE_MSC = 0,      // Mass Storage Class (normal mode)
    DEVICE_MODE_LOADER = 1,   // Bootloader mode (firmware programming)
    DEVICE_MODE_MASKROM = 2   // MASKROM mode (recovery firmware)
} rk_device_mode_t;

// Standard USB BOT Command Block Wrapper (CBW) Signature
#define CBW_SIGNATURE 0x43425355 // "USBC"
#define CSW_SIGNATURE 0x53425355 // "USBS"

// Rockchip Custom SCSI Opcodes (used in MSC/LOADER modes)
#define RK_SCSI_TEST_UNIT_READY    0x00 // Standard SCSI, used to enter test mode
#define RK_SCSI_READ_FLASH_ID      0x01 // Read Flash ID for specific Chip Select
#define RK_SCSI_REQUEST_SENSE      0x03 // Standard SCSI Request Sense
#define RK_SCSI_READ_PHYSICAL      0x04 // Read physical sectors with spare area (528 bytes)
#define RK_SCSI_READ_LBA           0x14 // Read logical sectors (512 bytes)
#define RK_SCSI_READ_FLASH_INFO    0x1A // Read Flash Info (512 bytes, gives NAND capacity/maker)
#define RK_SCSI_READ_CHIP_INFO     0x1B // Read Chip Info (512 bytes, gives SoC version string)
#define RK_SCSI_DEVICE_RESET       0xFF // Custom rockchip device reset

// Rockchip LOADER/MASKROM Mode Commands
#define RK_CMD_QUERY_DEVICE        0x00 // Query device info in LOADER/MASKROM mode
#define RK_CMD_READ_LBA_LOADER     0x01 // Read LBA in LOADER mode
#define RK_CMD_WRITE_LBA_LOADER    0x02 // Write LBA in LOADER mode
#define RK_CMD_FLASH_ERASE         0x03 // Erase flash in LOADER mode
#define RK_CMD_TEST_BAD_BLOCK       0x04 // Test for bad blocks
#define RK_CMD_SET_DEVICE_RESET    0x05 // Set device reset flag (MASKROM only)

#pragma pack(push, 1)

// Standard USB Bulk-Only-Transport CBW
typedef struct {
    uint32_t dCBWSignature;          // 0x43425355 ("USBC")
    uint32_t dCBWTag;                // Sequence number
    uint32_t dCBWDataTransferLength; // Bytes to transfer
    uint8_t  bmCBWFlags;             // 0x80 = Data IN (Device to Host), 0x00 = Data OUT
    uint8_t  bCBWLUN;                // Logical Unit Number (usually 0)
    uint8_t  bCBWCBLength;           // Length of the SCSI command block (CBWCB)
    
    // SCSI Command Block (Max 16 bytes)
    uint8_t  scsi_opcode;            // See RK_SCSI_* defines
    uint8_t  reserved1;
    uint32_t lba;                    // Big-endian Logical Block Address
    uint16_t reserved2;
    uint8_t  transfer_length;        // Number of blocks to read (e.g. sectors)
    uint8_t  reserved3[7];           // Pad to 16 bytes
} usb_bot_cbw_t;

// Standard USB Bulk-Only-Transport CSW
typedef struct {
    uint32_t dCSWSignature;          // 0x53425355 ("USBS")
    uint32_t dCSWTag;                // Matches dCBWTag
    uint32_t dCSWDataResidue;        // Un-transferred bytes
    uint8_t  bCSWStatus;             // 0 = Success, 1 = Failed, 2 = Phase Error
} usb_bot_csw_t;

#pragma pack(pop)

// Device info structure for LOADER/MASKROM modes
typedef struct {
    uint32_t flash_size;       // Total flash size in bytes
    uint32_t block_size;       // Erase block size
    uint32_t page_size;        // Page size
    uint16_t vendor_id;        // Flash manufacturer ID
    uint16_t device_id;        // Flash device ID
    uint8_t  ecc_bits;         // ECC bits per page
    uint8_t  access_time;      // Access time
    uint16_t reserved;
} rk_device_info_t;

#endif // ROCKCHIP_BOT_H

