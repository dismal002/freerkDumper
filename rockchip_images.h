#ifndef ROCKCHIP_IMAGES_H
#define ROCKCHIP_IMAGES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// RockChip Image Format Magic Numbers and Signatures
#define RKFILE_MAGIC_KRNL       "KRNL"
#define RKFILE_MAGIC_PARM       "PARM"
#define RKFILE_MAGIC_RKFP       "RKFP"
#define RKFILE_MAGIC_LOADER     "LOADER  "

#define RKSIGNED_MAGIC_KRNL     "[KRNLSIGNED]"
#define RKSIGNED_MAGIC_PARM     "[PARMSIGNED]"

#define RKBOOT_MAGIC            0x544F4F42  // "BOOT"
#define RKBOOT_MAGIC2           0x544F5242  // "BRPT"

// RockChip image format types
typedef enum {
    RKIMAGE_TYPE_UNKNOWN = -1,
    RKIMAGE_TYPE_KRNL = 0,           // Kernel signed file
    RKIMAGE_TYPE_PARM = 1,           // Parameter signed file
    RKIMAGE_TYPE_RKFP = 2,           // RKFP image (unified format)
    RKIMAGE_TYPE_BOOTLOADER = 3,     // Bootloader/IDB
    RKIMAGE_TYPE_UBOOT = 4,          // U-Boot image
    RKIMAGE_TYPE_RESOURCES = 5       // Resources image
} rkimage_type_t;

#pragma pack(push, 1)

// Common header for signed KRNL/PARM files
typedef struct {
    uint8_t  signature[12];      // "[KRNLSIGNED]" or "[PARMSIGNED]"
    uint16_t header_len;         // Header length in bytes (usually 0x30 = 48)
    uint16_t reserved1;
    uint8_t  file_type[4];       // "KRNL" or "PARM"
    uint8_t  reserved2[4];
    uint32_t file_len;           // Real file content length
    uint32_t crc32;              // CRC32 checksum
    uint8_t  rc4_flag;           // 1 = RC4 encrypted, 0 = plain
    uint8_t  reserved3[11];
    uint8_t  reserved4[4];
    uint8_t  sha256[32];         // SHA256 signature
} rkfile_signed_hdr_t;

// Bootloader/IDB sector 0 header (MiniLoaderAll)
typedef struct {
    uint16_t size_blocks;        // Size in 512-byte blocks
    uint16_t reserved1;
    uint16_t reserved2;
    uint16_t flash_data_offset;  // Offset to FlashData (in blocks)
    uint32_t reserved3[4];
    uint16_t entries[127];       // Internal payload offsets as uint16 (in blocks)
} rk_bootloader_hdr_t;

// IDB FlashData/FlashBoot payloads structure
typedef struct {
    uint32_t payload_size;       // Size of payload
    uint32_t payload_offset;     // Offset in IDB
    uint8_t  rc4_encrypted;      // Whether encrypted with RC4
    uint8_t  reserved[3];
} rk_idb_payload_t;

// RKFP (unified) image header
typedef struct {
    uint8_t  magic[4];           // "RKFP"
    uint32_t header_size;        // Total header size
    uint32_t file_size;          // Total file size
    uint8_t  version[4];         // Format version
    uint32_t image_count;        // Number of images packed
    uint8_t  reserved[256];
} rkfp_image_hdr_t;

// Individual image entry in RKFP
typedef struct {
    uint32_t offset;             // Offset from start of file
    uint32_t size;               // Size in bytes
    uint8_t  image_type[4];      // Type identifier
    uint8_t  name[32];           // Image name
    uint8_t  reserved[8];
} rkfp_image_entry_t;

#pragma pack(pop)

// RC4 cipher state
typedef struct {
    uint8_t key[32];
    uint8_t S[256];
    int i;
    int j;
} rc4_state_t;

// Image detection result
typedef struct {
    rkimage_type_t type;
    const char *type_name;
    bool is_encrypted;
    bool is_signed;
    uint32_t size;
    uint32_t content_offset;
} rkimage_info_t;

// ===== Function Declarations =====

// RC4 Encryption/Decryption
void rc4_init(rc4_state_t *state, const uint8_t *key, size_t key_len);
void rc4_crypt(rc4_state_t *state, uint8_t *data, size_t len);
void rc4_keyschedule(rc4_state_t *state, const uint8_t *key, size_t key_len);

// SHA-1 Hash
void sha1_init(void);
void sha1_update(const uint8_t *data, size_t len);
void sha1_final(uint8_t *digest);  // 20 bytes
uint32_t sha1_crc32(const uint8_t *data, size_t len);

// Image Format Detection
rkimage_type_t detect_image_type(const uint8_t *data, size_t size, rkimage_info_t *info);
const char *get_image_type_name(rkimage_type_t type);

// KRNL/PARM File Operations
bool unpack_signed_file(const char *input_file, const char *output_dir);
bool pack_signed_file(const char *input_file, const char *output_file, bool encrypt_rc4);

// Bootloader Operations
bool unpack_bootloader(const uint8_t *idb_data, size_t idb_size, const char *output_dir);
bool extract_idb_payloads(const uint8_t *idb_data, size_t idb_size, const char *output_dir);

// RKFP Image Operations
bool unpack_rkfp_image(const char *input_file, const char *output_dir);
bool pack_rkfp_image(const char *output_file, const char *input_dir);

// Signature Verification
bool verify_file_signature(const uint8_t *file_data, size_t file_size);
bool calculate_file_signature(const uint8_t *data, size_t size, uint8_t *signature);

#endif // ROCKCHIP_IMAGES_H
