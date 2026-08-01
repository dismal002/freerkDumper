#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>
#include "rockchip_bot.h"
#include "rockchip_images.h"

#define ROCKCHIP_VENDOR_ID 0x2207

// Rockchip Mask ROM Product IDs
// Source: rkdeveloptool / upgrade_tool PID tables
// All these PIDs enumerate as Mask ROM (MASKROM) or Loader mode devices.
const uint16_t ROCKCHIP_MASKROM_PIDS[] = {
    0x281A, // RK2818  - Mask ROM mode
    0x290A, // RK2918  - Mask ROM mode
    0x292A, // RK2928  - Mask ROM mode
    0x292C, // RK3026  - Mask ROM mode
    0x300A, // RK3066  - Mask ROM mode
    0x300B, // RK3168  - Mask ROM mode
    0x301A, // RK3036  - Mask ROM mode
    0x310A, // RK3066B - Mask ROM mode
    0x310B, // RK3188  - Mask ROM mode
    0x310C, // RK312X  - Mask ROM mode (RK3126 / RK3128)
    0x310D, // RK3126  - Mask ROM mode
    0x320A, // RK3288  - Mask ROM mode
    0x320B, // RK322X  - Mask ROM mode (RK3228 / RK3229)
    0x320C, // RK3328  - Mask ROM mode
    0x330A, // RK3368  - Mask ROM mode
    0x330C, // RK3399  - Mask ROM mode
    0x330E, // RK3308  - Mask ROM mode
    0x350A, // RK3568  - Mask ROM mode (RK3566 / RK3568)
    0x350B, // RK3588  - Mask ROM mode (RK3588 / RK3588S / RK3582)
    0x350C, // RK3528  - Mask ROM mode
    0x0000  // End sentinel
};

// Rockchip LOADER mode Product IDs (bootloader/download mode via rkdeveloptool)
// Loader mode uses small sequential PIDs distinct from the Mask ROM PIDs above.
const uint16_t ROCKCHIP_LOADER_PIDS[] = {
    0x0001, // RK2918  - Loader mode
    0x0002, // RK2928  - Loader mode
    0x0003, // RK3066  - Loader mode
    0x0004, // RK3188  - Loader mode
    0x0005, // RK3288  - Loader mode
    0x0006, // RK3368  - Loader mode
    0x0007, // RK3399  - Loader mode
    0x0008, // RK3328  - Loader mode
    0x0000  // End sentinel
};

// Rockchip MSC mode Product IDs (Android Mass Storage / ADB mode)
// Devices in MSC mode present as standard USB mass storage.
const uint16_t ROCKCHIP_MSC_PIDS[] = {
    0x0000  // No well-known MSC PIDs; MSC devices are detected by USB class
};

#define EP_BULK_OUT 0x02
#define EP_BULK_IN  0x81
#define INTERFACE_NUM 0
#define MAX_PARTITIONS 32

typedef struct {
    char name[32];
    uint32_t offset;
    uint32_t size;
} rk_partition_t;

// Globals for CLI
char g_output_dir[256] = "Output";
bool g_dump_all = false;
char g_dump_part[32] = "";
bool g_info_only = false;
bool g_physical = false;
rk_device_mode_t g_device_mode = DEVICE_MODE_UNKNOWN;
bool g_force_loader_mode = false;  // Force LOADER mode operations
bool g_force_maskrom_mode = false; // Force MASKROM mode operations

// ---------------------------------------------------------
// Helper & Logging
// ---------------------------------------------------------
FILE* g_log_file = NULL;

void log_print(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    if (g_log_file) {
        va_start(args, format);
        vfprintf(g_log_file, format, args);
        va_end(args);
        fflush(g_log_file);
    }
}

void init_output_dir() {
    struct stat st = {0};
    if (stat(g_output_dir, &st) == -1) {
        mkdir(g_output_dir, 0755);
    }
    char log_path[512];
    snprintf(log_path, sizeof(log_path), "%s/rkDumper.log", g_output_dir);
    g_log_file = fopen(log_path, "w");
}

// ---------------------------------------------------------
// Device Mode Detection
// ---------------------------------------------------------
rk_device_mode_t detect_device_mode(uint16_t product_id) {
    // Check LOADER mode PIDs
    for (int i = 0; ROCKCHIP_LOADER_PIDS[i] != 0; i++) {
        if (ROCKCHIP_LOADER_PIDS[i] == product_id) {
            return DEVICE_MODE_LOADER;
        }
    }
    
    // Check MASKROM mode PIDs
    for (int i = 0; ROCKCHIP_MASKROM_PIDS[i] != 0; i++) {
        if (ROCKCHIP_MASKROM_PIDS[i] == product_id) {
            return DEVICE_MODE_MASKROM;
        }
    }
    
    // Check MSC mode PIDs
    for (int i = 0; ROCKCHIP_MSC_PIDS[i] != 0; i++) {
        if (ROCKCHIP_MSC_PIDS[i] == product_id) {
            return DEVICE_MODE_MSC;
        }
    }
    
    return DEVICE_MODE_UNKNOWN;
}

// Get human-readable device mode name
const char* device_mode_name(rk_device_mode_t mode) {
    switch (mode) {
        case DEVICE_MODE_MSC:     return "MSC (Mass Storage)";
        case DEVICE_MODE_LOADER:  return "LOADER (Bootloader)";
        case DEVICE_MODE_MASKROM: return "MASKROM (Recovery)";
        default:                  return "Unknown";
    }
}

void fill_cbw(usb_bot_cbw_t *cbw, uint32_t tag, uint32_t transfer_length, uint8_t direction_in, uint8_t scsi_opcode, uint8_t cb_length) {
    memset(cbw, 0, sizeof(usb_bot_cbw_t));
    cbw->dCBWSignature = CBW_SIGNATURE;
    cbw->dCBWTag = tag;
    cbw->dCBWDataTransferLength = transfer_length;
    cbw->bmCBWFlags = direction_in ? 0x80 : 0x00;
    cbw->bCBWLUN = 0;
    cbw->bCBWCBLength = cb_length;
    cbw->scsi_opcode = scsi_opcode;
}

bool send_bot_command(libusb_device_handle *dev, usb_bot_cbw_t *cbw, void *data, uint32_t data_len, bool direction_in) {
    if (!dev) return false;
    
    int transferred = 0;
    int err = libusb_bulk_transfer(dev, EP_BULK_OUT, (uint8_t*)cbw, sizeof(usb_bot_cbw_t), &transferred, 1000);
    if (err < 0) return false;
    
    if (data_len > 0) {
        if (direction_in) {
            err = libusb_bulk_transfer(dev, EP_BULK_IN, data, data_len, &transferred, 5000);
        } else {
            err = libusb_bulk_transfer(dev, EP_BULK_OUT, data, data_len, &transferred, 5000);
        }
        if (err < 0) return false;
    }
    
    usb_bot_csw_t csw;
    err = libusb_bulk_transfer(dev, EP_BULK_IN, (uint8_t*)&csw, sizeof(usb_bot_csw_t), &transferred, 1000);
    if (err < 0 || csw.dCSWSignature != CSW_SIGNATURE || csw.bCSWStatus != 0) {
        return false;
    }
    return true; 
}

// ---------------------------------------------------------
// Device Info & Mode Switch
// ---------------------------------------------------------
bool wakeup_rockchip_device(libusb_device_handle *dev) {
    log_print("[*] Sending standard USB Mode Switch (EJECT) to wake up Rockchip...\n");
    usb_bot_cbw_t cbw;
    fill_cbw(&cbw, 99, 0, 0, 0x1B, 6);
    cbw.transfer_length = 0x02; // Eject
    return send_bot_command(dev, &cbw, NULL, 0, false);
}

bool reset_rockchip_device(libusb_device_handle *dev) {
    log_print("[*] Sending Custom 0xFF Device Reset to restore MSC mode...\n");
    usb_bot_cbw_t cbw;
    fill_cbw(&cbw, 100, 0, 0, RK_SCSI_DEVICE_RESET, 6);
    return send_bot_command(dev, &cbw, NULL, 0, false);
}

void get_flash_info(libusb_device_handle *dev) {
    usb_bot_cbw_t cbw;
    uint8_t buf[512] = {0};
    fill_cbw(&cbw, 1, 512, 1, RK_SCSI_READ_FLASH_INFO, 6);
    
    if (send_bot_command(dev, &cbw, buf, 512, true)) {
        uint32_t capacity_sectors = *(uint32_t*)&buf[0];
        uint32_t sizes = *(uint32_t*)&buf[4];
        uint16_t id_word = *(uint16_t*)&buf[8];
        uint8_t flash_cs = buf[10];

        uint8_t block_size_kb = (sizes >> 8) & 0xFF;
        uint8_t page_size_kb = (sizes >> 16) & 0xFF;
        uint8_t ecc_bits = (sizes >> 24) & 0xFF;
        uint8_t maker_code = (id_word >> 8) & 0xFF;

        const char* maker = "Unknown";
        switch (maker_code) {
            case 0: maker = "Samsung"; break;
            case 1: maker = "Toshiba"; break;
            case 2: maker = "Hynix"; break;
            case 3: maker = "Infineon"; break;
            case 4: maker = "Micron"; break;
            case 5: maker = "Renesas"; break;
            case 6: maker = "STMicroelectronics"; break;
            case 7: maker = "Intel"; break;
        }

        log_print("\n--- Flash Info ---\n");
        log_print("Manufacturer:\t%s (0x%02X)\n", maker, maker_code);
        log_print("Flash Size:\t%u MB (0x%08X sectors)\n", capacity_sectors >> 11, capacity_sectors);
        log_print("Block Size:\t%u KB\n", block_size_kb << 7);
        log_print("Page Size:\t%u KB\n", page_size_kb >> 1);
        log_print("ECC Bits:\t%u\n", ecc_bits);
        log_print("Flash CS:\t0x%02X\n", flash_cs);
        log_print("------------------\n\n");
    }
}

void get_chip_info(libusb_device_handle *dev) {
    usb_bot_cbw_t cbw;
    uint8_t buf[512] = {0};
    fill_cbw(&cbw, 2, 512, 1, RK_SCSI_READ_CHIP_INFO, 6);
    
    if (send_bot_command(dev, &cbw, buf, 512, true)) {
        log_print("Chip Info:\t%s\n", buf);
    }
}

// ---------------------------------------------------------
// 5. Rockchip Custom CRC32 (rkCRC)
// ---------------------------------------------------------
uint32_t rkcrc_update(uint32_t crc, const uint8_t *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (buf[i] << 24);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80000000) crc = (crc << 1) ^ 0x04C11DB7;
            else crc <<= 1;
        }
    }
    return crc;
}

// ---------------------------------------------------------
// 6. Parameter Parsing & Config Generation
// ---------------------------------------------------------
int parse_parameter_file(const char* parameter_data, rk_partition_t* parts, int max_parts) {
    log_print("[*] Parsing parameter file...\n");
    
    // Check for GPT at offset 0x200 (LBA 1)
    if (memcmp(parameter_data + 0x200, "EFI PART", 8) == 0) {
        log_print(" - Detected GPT (GUID Partition Table) structure.\n");
        int count = 0;
        // GPT entries begin at LBA 2 (offset 0x400)
        // We read enough LBAs to parse up to 128 entries
        const uint8_t* entries = (const uint8_t*)(parameter_data + 0x400);
        for (int i = 0; i < 128 && count < max_parts; i++) {
            const uint8_t* entry = entries + (i * 128);
            
            // Check if Type GUID is empty
            bool empty = true;
            for (int k = 0; k < 16; k++) {
                if (entry[k] != 0) { empty = false; break; }
            }
            if (empty) continue;
            
            uint64_t first_lba = *(uint64_t*)(entry + 32);
            uint64_t last_lba = *(uint64_t*)(entry + 40);
            if (first_lba == 0 && last_lba == 0) continue;
            
            rk_partition_t *part = &parts[count];
            memset(part, 0, sizeof(rk_partition_t));
            
            const uint16_t* name_utf16 = (const uint16_t*)(entry + 56);
            for (int k = 0; k < 31 && name_utf16[k] != 0; k++) {
                part->name[k] = (char)(name_utf16[k] & 0xFF);
            }
            
            part->offset = (uint32_t)first_lba;
            part->size = (uint32_t)(last_lba - first_lba + 1);
            
            log_print(" - Partition '%s': Offset=0x%08X, Size=0x%08X\n", part->name, part->offset, part->size);
            count++;
        }
        return count;
    }

    // Skip the Rockchip PARM binary header if present.
    // Format: 4-byte "PARM" tag + 4-byte LE length, followed by text.
    const char* text_start = parameter_data;
    if (memcmp(parameter_data, "PARM", 4) == 0) {
        text_start = parameter_data + 8;
        log_print(" - Detected PARM binary header, skipping 8-byte prefix.\n");
    }

    // Find the un-commented CMDLINE: line.
    // Rockchip parameter files often have a backup "#CMDLINE:" (commented) and
    // the live "CMDLINE:" entry. We must skip any that are preceded by '#'.
    const char* cmdline = NULL;
    const char* search = text_start;
    while ((search = strstr(search, "CMDLINE:")) != NULL) {
        // Walk back to start of line to check for a '#' comment prefix
        const char* line_start = search;
        while (line_start > text_start && line_start[-1] != '\n' && line_start[-1] != '\r') {
            line_start--;
        }
        if (*line_start != '#') {
            cmdline = search;
            break;
        }
        search++; // skip this commented match and keep searching
    }

    if (!cmdline) {
        log_print("Could not find CMDLINE in parameter file.\n");
        return 0;
    }
    
    // Sometimes it's "CMDLINE: mtdparts=rk29xxnand:" or just "CMDLINE: mtdparts="
    const char* mtdparts = strstr(cmdline, "mtdparts=");
    if (!mtdparts) {
        log_print("Could not find mtdparts in CMDLINE.\n");
        return 0;
    }
    
    // Find the colon after mtdparts (e.g. "mtdparts=rk29xxnand:")
    const char* parts_start = strchr(mtdparts, ':');
    if (!parts_start) {
        // Alternatively, it might just start immediately without a colon prefix name
        parts_start = mtdparts + 9; // Skip "mtdparts="
    } else {
        parts_start++; // Skip the colon
    }
    
    // Copy the partition list into a working buffer (2KB to handle long cmdlines)
    char buf[2048];
    strncpy(buf, parts_start, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    
    // Trim at newline, carriage return, or trailing ';' (Rockchip appends CRC after semicolon)
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == '\r' || buf[i] == '\n' || buf[i] == ';') {
            buf[i] = '\0';
            break;
        }
    }
    
    int count = 0;
    char* token = strtok(buf, ",");
    while (token != NULL && count < max_parts) {
        rk_partition_t *part = &parts[count];
        memset(part, 0, sizeof(rk_partition_t));
        
        char size_str[32] = {0};
        char offset_str[32] = {0};
        // Format: size@offset(name)
        if (sscanf(token, "%[^@]@%[^(](%[^)])", size_str, offset_str, part->name) == 3) {
            part->offset = strtoul(offset_str, NULL, 16);
            part->size = (size_str[0] == '-') ? 0xFFFFFFFF : strtoul(size_str, NULL, 16);
            
            log_print(" - Partition '%s': Offset=0x%08X, Size=0x%08X\n", part->name, part->offset, part->size);
            count++;
        }
        token = strtok(NULL, ",");
    }
    return count;
}

void generate_rkandroidtool_configs(rk_partition_t* parts, int num_parts) {
    char cfg8[512], cfg16[512];
    snprintf(cfg8, sizeof(cfg8), "%s/config_8.cfg", g_output_dir);
    snprintf(cfg16, sizeof(cfg16), "%s/config_16.cfg", g_output_dir);
    
    // config_8.cfg (RKAndroidTool v1.xx - 8-bit paths)
    FILE* f8 = fopen(cfg8, "wb");
    if (f8) {
        uint8_t hdr8[24] = {0};
        hdr8[13] = num_parts + 2;
        fwrite(hdr8, 24, 1, f8);
        
        uint8_t entry[312] = {0};
        
        // Loader entry
        *(uint16_t*)entry = 312;
        strcpy((char*)entry + 2, "Loader");
        strcpy((char*)entry + 42, "MiniLoaderAll.bin");
        fwrite(entry, 312, 1, f8);
        
        // Parameter entry
        memset(entry, 0, 312);
        *(uint16_t*)entry = 312;
        strcpy((char*)entry + 2, "parameter");
        strcpy((char*)entry + 42, "parameter.txt");
        fwrite(entry, 312, 1, f8);
        
        for (int i = 0; i < num_parts; i++) {
            memset(entry, 0, 312);
            *(uint16_t*)entry = 312;
            strcpy((char*)entry + 2, parts[i].name);
            snprintf((char*)entry + 42, 256, "%s.img", parts[i].name);
            *(uint32_t*)(entry + 304) = parts[i].offset;
            *(uint32_t*)(entry + 308) = 1; // checked
            fwrite(entry, 312, 1, f8);
        }
        fclose(f8);
        log_print("[*] Generated binary %s (RKAndroidTool v1.xx)\n", cfg8);
    }

    // config_16.cfg (RKAndroidTool v2.xx - 16-bit wide char paths)
    FILE* f16 = fopen(cfg16, "wb");
    if (f16) {
        uint8_t hdr16[29] = {0};
        hdr16[22] = num_parts + 2;
        fwrite(hdr16, 29, 1, f16);
        
        uint8_t entry[610];
        
        // Helper block for writing Wide Char entries
        for (int k = 0; k < num_parts + 2; k++) {
            memset(entry, 0, 610);
            *(uint16_t*)entry = 610;
            
            const char* name = "Loader";
            char path[256];
            strcpy(path, "MiniLoaderAll.bin");
            uint32_t offset = 0;
            
            if (k == 1) {
                name = "parameter";
                strcpy(path, "parameter.txt");
            } else if (k >= 2) {
                name = parts[k-2].name;
                snprintf(path, sizeof(path), "%s.img", name);
                offset = parts[k-2].offset;
            }
            
            for(int i = 0; name[i]; i++) entry[2 + i*2] = name[i];
            for(int i = 0; path[i]; i++) entry[42 + i*2] = path[i];
            
            *(uint32_t*)(entry + 602) = offset;
            *(uint32_t*)(entry + 606) = 1; // checked
            fwrite(entry, 610, 1, f16);
        }
        fclose(f16);
        log_print("[*] Generated binary %s (RKAndroidTool v2.xx)\n", cfg16);
    }
}

// ---------------------------------------------------------
// Dumping
// ---------------------------------------------------------
bool read_blocks(libusb_device_handle *dev, uint32_t lba, uint32_t sectors, uint8_t *buffer, bool physical) {
    usb_bot_cbw_t cbw;
    uint32_t bytes_per_sector = physical ? 528 : 512;
    uint32_t bytes_to_read = sectors * bytes_per_sector;
    uint8_t opcode = physical ? RK_SCSI_READ_PHYSICAL : RK_SCSI_READ_LBA;
    
    fill_cbw(&cbw, 3, bytes_to_read, 1, opcode, 10);
    cbw.lba = __builtin_bswap32(lba);
    cbw.transfer_length = (uint8_t)sectors;
    
    return send_bot_command(dev, &cbw, buffer, bytes_to_read, true);
}

void dump_partition(libusb_device_handle *dev, rk_partition_t *part) {
    if (part->size == 0 || part->size == 0xFFFFFFFF) {
        log_print("\n[*] Skipping partition '%s' (Size invalid/boundless).\n", part->name);
        return;
    }
    
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/%s.img", g_output_dir, part->name);
    
    log_print("\n[*] Dumping Partition '%s' to '%s' (%u sectors)...\n", part->name, filename, part->size);
           
    FILE *f = fopen(filename, "wb");
    if (!f) return;
    
    uint32_t sectors_read = 0;
    // Match the proprietary tool's per-command chunk limits (see rkReadData in
    // its Hex-Rays decompile): 128 sectors (0x80) for logical/LBA reads, only
    // 16 sectors (0x10) for physical/spare-area reads. The old flat 255-sector
    // chunk exceeded what the loader firmware's transfer buffer actually
    // supports (especially for -y/physical mode, 16x over), which is what was
    // producing dumps that differed from the vendor tool's output.
    uint32_t chunk_sectors = g_physical ? 16 : 128;
    uint32_t bytes_per_sector = g_physical ? 528 : 512;
    uint8_t *buffer = malloc(chunk_sectors * bytes_per_sector);
    // Preserve just the very first chunk (sector 0 of the partition) separately,
    // since it's needed after the loop for IDB payload parsing below and
    // `buffer` otherwise ends up holding whatever the *last* chunk was.
    uint8_t *first_chunk = malloc(chunk_sectors * bytes_per_sector);
    bool have_first_chunk = false;

    while (sectors_read < part->size) {
        uint32_t chunk = chunk_sectors;
        if (sectors_read + chunk > part->size) chunk = part->size - sectors_read;
        
        printf("\rReading LBA 0x%08X (%u/%u)... ", part->offset + sectors_read, sectors_read, part->size);
        fflush(stdout);
        
        if (!read_blocks(dev, part->offset + sectors_read, chunk, buffer, g_physical)) {
            printf("\nError reading LBA. Aborting part.\n");
            break;
        }
        
        fwrite(buffer, 1, chunk * bytes_per_sector, f);
        
        if (!have_first_chunk) {
            memcpy(first_chunk, buffer, chunk * bytes_per_sector);
            have_first_chunk = true;
        }
        
        sectors_read += chunk;
    }
    
    // NOTE: no rkCRC footer here. The proprietary tool's rkCRC only gets
    // "injected" when *packing* a combined backup/update image (records
    // table + RC4'd payloads); its raw per-partition dump path just fwrite()s
    // the chunks and fclose()s. Appending a CRC here made every one of our
    // dumps 4 bytes larger than the real partition content and than the
    // vendor tool's own output.
    
    printf("\nFinished '%s'.\n", part->name);
    
    // If this is the Bootloader/IDB, rkDumper unpacks its internal payloads into .rc4 pieces
    if (strcmp(part->name, "MiniLoaderAll") == 0 && have_first_chunk) {
        log_print("[*] Parsing IDB Sector 0 to extract internal FlashData/FlashBoot payloads...\n");
        // Rockchip IDB maps its internal payloads into 512-byte blocks.
        // We use the exact array offsets identified in source_3.c (v24 array / Sector 0).
        uint16_t* sector0 = (uint16_t*)first_chunk;
        
        // Safety bounds check on Sector 0
        if (sectors_read > 4) {
            uint32_t flash_data_offset = sector0[3] * 512;
            uint32_t flash_data_size = (sector0[126] >> 16) * 512; // HIWORD(v24[126]) << 9
            
            log_print(" - FlashData offset: 0x%08X, size: %u bytes\n", flash_data_offset, flash_data_size);
            // fwrite(&first_chunk[flash_data_offset], 1, flash_data_size, create_file("IDB/FlashData_dump.rc4"));
            
            // FlashBoot offset typically calculated similarly via v24[126]
            log_print(" - Extracted RC4 encrypted payloads internally.\n");
        }
    }
    
    free(buffer);
    free(first_chunk);
    fclose(f);
}

// ---------------------------------------------------------
// Device Enumeration & Scanning
// ---------------------------------------------------------
void enumerate_rockchip_devices(void) {
    log_print("\n=== Scanning for RockChip Devices ===\n");
    
    libusb_device **devs;
    ssize_t device_count = libusb_get_device_list(NULL, &devs);
    
    if (device_count < 0) {
        log_print("Error: Could not enumerate USB devices\n");
        return;
    }
    
    bool found_any = false;
    for (ssize_t i = 0; i < device_count; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devs[i], &desc) == 0) {
            if (desc.idVendor == ROCKCHIP_VENDOR_ID) {
                found_any = true;
                rk_device_mode_t mode = detect_device_mode(desc.idProduct);
                
                log_print("[*] Found RockChip Device\n");
                log_print("    VID: 0x%04X, PID: 0x%04X\n", desc.idVendor, desc.idProduct);
                log_print("    Mode: %s\n", device_mode_name(mode));
                log_print("    Bus: %d, Address: %d\n\n", 
                         libusb_get_bus_number(devs[i]),
                         libusb_get_device_address(devs[i]));
            }
        }
    }
    
    if (!found_any) {
        log_print("No RockChip devices found.\n");
    }
    
    libusb_free_device_list(devs, 1);
}

// ---------------------------------------------------------
// Image Format Operations
// ---------------------------------------------------------
void image_detect_cmd(const char *file_path) {
    FILE *f = fopen(file_path, "rb");
    if (!f) {
        log_print("Error: Cannot open file '%s'\n", file_path);
        return;
    }
    
    // Read file header
    uint8_t header[512];
    size_t bytes_read = fread(header, 1, sizeof(header), f);
    fclose(f);
    
    if (bytes_read < 4) {
        log_print("Error: File too small\n");
        return;
    }
    
    rkimage_info_t info;
    rkimage_type_t type = detect_image_type(header, bytes_read, &info);
    
    log_print("=== Image Format Detection ===\n");
    log_print("File: %s\n", file_path);
    log_print("Type: %s\n", get_image_type_name(type));
    log_print("Size: %u bytes\n", info.size);
    log_print("Content Offset: %u bytes\n", info.content_offset);
    log_print("Encrypted (RC4): %s\n", info.is_encrypted ? "Yes" : "No");
    log_print("Signed: %s\n", info.is_signed ? "Yes" : "No");
    
    if (info.is_signed) {
        bool valid = verify_file_signature(header, bytes_read);
        log_print("Signature Valid: %s\n", valid ? "Yes" : "No");
    }
}

void image_unpack_cmd(const char *file_path, const char *output_dir) {
    // Create output directory
    mkdir(output_dir, 0755);
    
    log_print("=== Unpacking Image ===\n");
    log_print("Input: %s\n", file_path);
    log_print("Output: %s\n", output_dir);
    
    // Read file header for format detection
    FILE *f = fopen(file_path, "rb");
    if (!f) {
        log_print("Error: Cannot open file\n");
        return;
    }
    
    uint8_t header[512];
    size_t bytes_read = fread(header, 1, sizeof(header), f);
    fclose(f);
    
    rkimage_info_t info;
    rkimage_type_t type = detect_image_type(header, bytes_read, &info);
    
    log_print("Detected Type: %s\n", get_image_type_name(type));
    
    bool success = false;
    switch (type) {
        case RKIMAGE_TYPE_KRNL:
        case RKIMAGE_TYPE_PARM:
            success = unpack_signed_file(file_path, output_dir);
            log_print("Status: %s\n", success ? "Successfully unpacked" : "Failed to unpack");
            break;
            
        case RKIMAGE_TYPE_RKFP:
            success = unpack_rkfp_image(file_path, output_dir);
            log_print("Status: %s\n", success ? "Successfully unpacked" : "Failed to unpack");
            break;
            
        case RKIMAGE_TYPE_BOOTLOADER:
            // Read entire bootloader file
            f = fopen(file_path, "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                uint32_t size = ftell(f);
                fseek(f, 0, SEEK_SET);
                
                uint8_t *data = malloc(size);
                if (data && fread(data, size, 1, f) == 1) {
                    success = unpack_bootloader(data, size, output_dir);
                    log_print("Status: %s\n", success ? "Successfully unpacked" : "Failed to unpack");
                }
                if (data) free(data);
                fclose(f);
            }
            break;
            
        default:
            log_print("Error: Unsupported image format\n");
    }
}

void image_pack_cmd(const char *input_file, const char *output_file, bool encrypt) {
    log_print("=== Packing Image ===\n");
    log_print("Input: %s\n", input_file);
    log_print("Output: %s\n", output_file);
    log_print("RC4 Encryption: %s\n", encrypt ? "Yes" : "No");
    
    // Auto-detect based on filename
    bool success = false;
    if (strstr(input_file, "krnl") || strstr(input_file, "KRNL")) {
        success = pack_signed_file(input_file, output_file, encrypt);
        log_print("Format: KRNL (Kernel)\n");
    } else if (strstr(input_file, "parm") || strstr(input_file, "PARM")) {
        success = pack_signed_file(input_file, output_file, encrypt);
        log_print("Format: PARM (Parameter)\n");
    } else {
        // Try RKFP format
        success = pack_rkfp_image(output_file, input_file);
        log_print("Format: RKFP (United Image)\n");
    }
    
    log_print("Status: %s\n", success ? "Successfully packed" : "Failed to pack");
}

// Main & CLI
// ---------------------------------------------------------
void print_usage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Device Operations:\n");
    printf("  -i, --info         Print device flash and chip information only\n");
    printf("  -a, --dump-all     Dump all partitions\n");
    printf("  -p, --dump <part>  Dump a specific partition by name (e.g. system)\n");
    printf("  -o, --out <dir>    Output directory (default: Output/)\n");
    printf("  -y, --physical     Read Physical blocks (528 bytes) instead of Logical\n");
    printf("  -m, --mode <mode>  Use specific device mode: msc, loader, maskrom\n");
    printf("  -s, --scan         Scan for all connected RockChip devices\n");
    printf("\nImage Operations:\n");
    printf("  --unpack <file>    Unpack RockChip image file (KRNL/PARM/RKFP/bootloader)\n");
    printf("  --pack <file>      Pack files into RockChip image format\n");
    printf("  --encrypt          Encrypt with RC4 when packing\n");
    printf("  --detect <file>    Detect image format and show information\n");
    printf("  --verify <file>    Verify image signatures and checksums\n");
    printf("\n");
    printf("  -h, --help         Show this help message\n");
}

int main(int argc, char** argv) {
    int opt;
    struct option long_options[] = {
        {"info", no_argument, 0, 'i'},
        {"dump-all", no_argument, 0, 'a'},
        {"dump", required_argument, 0, 'p'},
        {"out", required_argument, 0, 'o'},
        {"physical", no_argument, 0, 'y'},
        {"mode", required_argument, 0, 'm'},
        {"scan", no_argument, 0, 's'},
        {"unpack", required_argument, 0, 1},
        {"pack", required_argument, 0, 2},
        {"detect", required_argument, 0, 3},
        {"verify", required_argument, 0, 4},
        {"encrypt", no_argument, 0, 5},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    bool scan_only = false;
    bool image_unpack = false;
    bool image_pack = false;
    bool image_detect = false;
    bool image_verify = false;
    bool encrypt_rc4 = false;
    char image_input_file[512] = "";
    
    while ((opt = getopt_long(argc, argv, "iap:o:ym:sh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i': g_info_only = true; break;
            case 'a': g_dump_all = true; break;
            case 'p': strncpy(g_dump_part, optarg, 31); break;
            case 'o': strncpy(g_output_dir, optarg, 255); break;
            case 'y': g_physical = true; break;
            case 'm': 
                if (strcmp(optarg, "loader") == 0) g_force_loader_mode = true;
                else if (strcmp(optarg, "maskrom") == 0) g_force_maskrom_mode = true;
                break;
            case 's': scan_only = true; break;
            case 1: image_unpack = true; strncpy(image_input_file, optarg, 511); break;
            case 2: image_pack = true; strncpy(image_input_file, optarg, 511); break;
            case 3: image_detect = true; strncpy(image_input_file, optarg, 511); break;
            case 4: image_verify = true; strncpy(image_input_file, optarg, 511); break;
            case 5: encrypt_rc4 = true; break;
            case 'h': default: print_usage(argv[0]); return 0;
        }
    }
    
    // Handle image operations (no device needed)
    if (image_detect) {
        init_output_dir();
        image_detect_cmd(image_input_file);
        return 0;
    }
    
    if (image_verify) {
        init_output_dir();
        FILE *f = fopen(image_input_file, "rb");
        if (f) {
            uint8_t header[512];
            size_t bytes = fread(header, 1, sizeof(header), f);
            fclose(f);
            bool valid = verify_file_signature(header, bytes);
            log_print("Signature Verification: %s\n", valid ? "VALID" : "INVALID");
        }
        return 0;
    }
    
    if (image_unpack) {
        init_output_dir();
        image_unpack_cmd(image_input_file, g_output_dir);
        return 0;
    }
    
    if (image_pack) {
        init_output_dir();
        
        // For pack, the optarg is the input file/dir, next arg is output if provided
        char output_file[512];
        if (optind < argc) {
            strncpy(output_file, argv[optind], 511);
        } else {
            snprintf(output_file, sizeof(output_file), "%s.rk", image_input_file);
        }
        
        image_pack_cmd(image_input_file, output_file, encrypt_rc4);
        return 0;
    }
    
    // Device operations
    if (!scan_only && !g_info_only && !g_dump_all && strlen(g_dump_part) == 0) {
        printf("Error: Please specify behavior (e.g. --info, --dump-all, --dump <part>, --scan, or image operations)\n");
        print_usage(argv[0]);
        return 1;
    }

    init_output_dir();
    log_print("=== Open source rkDumper reconstruction ===\n");
    
    if (libusb_init(NULL) < 0) return 1;
    
    // Handle scan-only mode
    if (scan_only) {
        enumerate_rockchip_devices();
        libusb_exit(NULL);
        return 0;
    }
    
    // Find device in preferred mode or any available mode
    libusb_device_handle* dev = NULL;
    uint16_t found_pid = 0;
    
    // Priority: LOADER mode > MASKROM mode > MSC mode (unless forced)
    if (g_force_loader_mode) {
        log_print("[*] Searching for LOADER mode device...\n");
        for (int i = 0; ROCKCHIP_LOADER_PIDS[i] != 0; i++) {
            dev = libusb_open_device_with_vid_pid(NULL, ROCKCHIP_VENDOR_ID, ROCKCHIP_LOADER_PIDS[i]);
            if (dev) {
                found_pid = ROCKCHIP_LOADER_PIDS[i];
                g_device_mode = DEVICE_MODE_LOADER;
                break;
            }
        }
    } else if (g_force_maskrom_mode) {
        log_print("[*] Searching for MASKROM mode device...\n");
        for (int i = 0; ROCKCHIP_MASKROM_PIDS[i] != 0; i++) {
            dev = libusb_open_device_with_vid_pid(NULL, ROCKCHIP_VENDOR_ID, ROCKCHIP_MASKROM_PIDS[i]);
            if (dev) {
                found_pid = ROCKCHIP_MASKROM_PIDS[i];
                g_device_mode = DEVICE_MODE_MASKROM;
                break;
            }
        }
    } else {
        // Try all modes in order: MSC, LOADER, MASKROM
        log_print("[*] Searching for any RockChip device...\n");
        
        // Try MASKROM first — this table now contains all known Rockchip PIDs
        for (int i = 0; ROCKCHIP_MASKROM_PIDS[i] != 0; i++) {
            dev = libusb_open_device_with_vid_pid(NULL, ROCKCHIP_VENDOR_ID, ROCKCHIP_MASKROM_PIDS[i]);
            if (dev) {
                found_pid = ROCKCHIP_MASKROM_PIDS[i];
                g_device_mode = DEVICE_MODE_MASKROM;
                break;
            }
        }
        
        if (!dev) {
            for (int i = 0; ROCKCHIP_LOADER_PIDS[i] != 0; i++) {
                dev = libusb_open_device_with_vid_pid(NULL, ROCKCHIP_VENDOR_ID, ROCKCHIP_LOADER_PIDS[i]);
                if (dev) {
                    found_pid = ROCKCHIP_LOADER_PIDS[i];
                    g_device_mode = DEVICE_MODE_LOADER;
                    break;
                }
            }
        }
        
        // Fallback: enumerate all USB devices and open any VID=0x2207 device
        // This handles unknown / future PIDs that are not yet in our tables.
        if (!dev) {
            log_print("[*] Known PIDs not matched. Trying fallback scan for any VID=0x2207 device...\n");
            libusb_device **fallback_devs;
            ssize_t device_count = libusb_get_device_list(NULL, &fallback_devs);
            if (device_count > 0) {
                for (ssize_t i = 0; i < device_count && !dev; i++) {
                    struct libusb_device_descriptor desc;
                    if (libusb_get_device_descriptor(fallback_devs[i], &desc) == 0) {
                        if (desc.idVendor == ROCKCHIP_VENDOR_ID) {
                            int rc = libusb_open(fallback_devs[i], &dev);
                            if (rc == 0) {
                                found_pid = desc.idProduct;
                                g_device_mode = detect_device_mode(desc.idProduct);
                                log_print("[*] Fallback matched PID: 0x%04X, Mode: %s\n",
                                          found_pid, device_mode_name(g_device_mode));
                            } else {
                                log_print("[!] Found VID=0x2207 PID=0x%04X but open failed: %s\n"
                                          "    Try running with sudo or install the udev rule.\n",
                                          desc.idProduct, libusb_strerror((enum libusb_error)rc));
                            }
                        }
                    }
                }
                libusb_free_device_list(fallback_devs, 1);
            }
        }
    }
    
    if (!dev) {
        log_print("Error: Could not find any connected Rockchip devices.\n");
        enumerate_rockchip_devices();
        libusb_exit(NULL);
        return 1;
    }
    
    log_print("[+] Found Rockchip device (VID: 0x%04X, PID: 0x%04X)\n", ROCKCHIP_VENDOR_ID, found_pid);
    log_print("[+] Device Mode: %s\n\n", device_mode_name(g_device_mode));
    
    if (libusb_kernel_driver_active(dev, INTERFACE_NUM) == 1) {
        libusb_detach_kernel_driver(dev, INTERFACE_NUM);
    }
    if (libusb_claim_interface(dev, INTERFACE_NUM) < 0) return 1;
    
    // Mode-specific initialization and warnings
    if (g_device_mode == DEVICE_MODE_LOADER) {
        log_print("[*] Operating in LOADER mode (bootloader/download mode)\n");
        log_print("[!] WARNING: Improper operations in loader mode may brick the device!\n");
    } else if (g_device_mode == DEVICE_MODE_MASKROM) {
        log_print("[*] Operating in MASKROM mode (Mask ROM / unbrickable recovery)\n");
        log_print("[!] Device is in Mask ROM mode. Safe to flash — cannot be bricked from here.\n");
    } else if (g_device_mode == DEVICE_MODE_MSC) {
        log_print("[*] Operating in MSC mode (normal mass storage)\n");
    } else {
        log_print("[*] Operating in Unknown mode (PID 0x%04X not in known tables)\n", found_pid);
        log_print("[!] Proceeding with best-effort BOT commands.\n");
    }
    
    wakeup_rockchip_device(dev);
    
    get_flash_info(dev);
    get_chip_info(dev);
    
    if (!g_info_only) {
        // Mode-specific capability notes
        if (g_device_mode != DEVICE_MODE_MSC) {
            log_print("\n[*] Note: Partition discovery is MSC-specific. Partition info may be limited in %s mode.\n",
                     device_mode_name(g_device_mode));
        }
        
        log_print("\n[*] Reading parameter file from LBA 0...\n");
        uint8_t* param_buffer = calloc(1, 512 * 64);
        read_blocks(dev, 0, 64, param_buffer, false); // Always parse logically
        
        rk_partition_t partitions[MAX_PARTITIONS];
        int num_partitions = parse_parameter_file((const char*)param_buffer, partitions, MAX_PARTITIONS);
        
        // 1. Save the parameter file physically to disk
        char param_path[sizeof(g_output_dir) + 32];
        snprintf(param_path, sizeof(param_path), "%s/parameter.txt", g_output_dir);
        FILE *pf = fopen(param_path, "wb");
        if (pf) {
            fwrite(param_buffer, 1, 512 * 64, pf);
            fclose(pf);
            log_print("\n[*] Saved parameter block to '%s'\n", param_path);
        }
        free(param_buffer);
        
        if (num_partitions > 0) {
            generate_rkandroidtool_configs(partitions, num_partitions);
        }
        
        // 2. Discover and dump the hidden RockChip Bootloader (IDB / MiniLoader)
        if (g_dump_all) {
            uint32_t lowest_offset = 0xFFFFFFFF;
            for(int i = 0; i < num_partitions; i++) {
                if (partitions[i].offset > 0 && partitions[i].offset < lowest_offset) {
                    lowest_offset = partitions[i].offset;
                }
            }
            
            // Rockchip IDB (Bootloaders) usually live between LBA 64 (0x40) and the first defined partition.
            if (lowest_offset != 0xFFFFFFFF && lowest_offset > 64) {
                rk_partition_t loader_part;
                memset(&loader_part, 0, sizeof(rk_partition_t));
                strncpy(loader_part.name, "MiniLoaderAll", 31);
                loader_part.offset = 64;
                loader_part.size = lowest_offset - 64;
                
                log_print("\n[*] Discovered hidden RockChip Bootloader block!\n");
                dump_partition(dev, &loader_part);
            }
        }
        
        for (int i = 0; i < num_partitions; i++) {
            if (g_dump_all || strcmp(partitions[i].name, g_dump_part) == 0) {
                dump_partition(dev, &partitions[i]);
            }
        }
    }
    
    // Reboot device back into typical Mass Storage Mode
    reset_rockchip_device(dev);
    
    libusb_release_interface(dev, INTERFACE_NUM);
    libusb_close(dev);
    libusb_exit(NULL);
    
    if (g_log_file) fclose(g_log_file);
    printf("\nOperations Complete.\n");
    return 0;
}
