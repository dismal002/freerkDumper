#include "rockchip_images.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

// ===== RC4 CIPHER IMPLEMENTATION =====
void rc4_keyschedule(rc4_state_t *state, const uint8_t *key, size_t key_len) {
    // Initialize S box
    for (int i = 0; i < 256; i++) {
        state->S[i] = i;
    }
    
    // Key scheduling algorithm
    int j = 0;
    for (int i = 0; i < 256; i++) {
        j = (j + state->S[i] + key[i % key_len]) & 0xFF;
        
        // Swap S[i] and S[j]
        uint8_t temp = state->S[i];
        state->S[i] = state->S[j];
        state->S[j] = temp;
    }
    
    state->i = 0;
    state->j = 0;
}

void rc4_init(rc4_state_t *state, const uint8_t *key, size_t key_len) {
    if (key_len > 32) key_len = 32;
    memcpy(state->key, key, key_len);
    rc4_keyschedule(state, key, key_len);
}

void rc4_crypt(rc4_state_t *state, uint8_t *data, size_t len) {
    for (size_t n = 0; n < len; n++) {
        state->i = (state->i + 1) & 0xFF;
        state->j = (state->j + state->S[state->i]) & 0xFF;
        
        // Swap S[i] and S[j]
        uint8_t temp = state->S[state->i];
        state->S[state->i] = state->S[state->j];
        state->S[state->j] = temp;
        
        // Get K
        uint8_t K = state->S[(state->S[state->i] + state->S[state->j]) & 0xFF];
        data[n] ^= K;
    }
}

// ===== SIMPLIFIED SHA-1 IMPLEMENTATION =====
// Note: This is a basic implementation. For production, use OpenSSL

typedef struct {
    uint32_t h[5];
    uint32_t length;
    uint8_t  buffer[64];
} sha1_ctx_t;

static sha1_ctx_t g_sha1_ctx;

static uint32_t sha1_rol(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

void sha1_init(void) {
    g_sha1_ctx.h[0] = 0x67452301;
    g_sha1_ctx.h[1] = 0xEFCDAB89;
    g_sha1_ctx.h[2] = 0x98BADCFE;
    g_sha1_ctx.h[3] = 0x10325476;
    g_sha1_ctx.h[4] = 0xC3D2E1F0;
    g_sha1_ctx.length = 0;
    memset(g_sha1_ctx.buffer, 0, 64);
}

void sha1_update(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        g_sha1_ctx.buffer[g_sha1_ctx.length] = data[i];
        g_sha1_ctx.length++;
        
        if (g_sha1_ctx.length == 64) {
            // Process 512-bit block
            // (Simplified - real SHA-1 would process here)
            g_sha1_ctx.length = 0;
        }
    }
}

void sha1_final(uint8_t *digest) {
    // (Simplified - real SHA-1 would finalize padding here)
    // For now, return a pseudo-hash
    for (int i = 0; i < 20; i++) {
        digest[i] = g_sha1_ctx.h[i % 5] >> (8 * (i % 4));
    }
}

// CRC32 calculation for RockChip files
uint32_t sha1_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= (data[i] << 24);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80000000) {
                crc = (crc << 1) ^ 0x04C11DB7;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// ===== IMAGE DETECTION =====
rkimage_type_t detect_image_type(const uint8_t *data, size_t size, rkimage_info_t *info) {
    if (!data || size < 12) {
        if (info) info->type = RKIMAGE_TYPE_UNKNOWN;
        return RKIMAGE_TYPE_UNKNOWN;
    }
    
    rkimage_type_t type = RKIMAGE_TYPE_UNKNOWN;
    bool encrypted = false;
    bool signed_file = false;
    
    // Check for signed KRNL
    if (size >= 48 && memcmp(data, RKSIGNED_MAGIC_KRNL, 12) == 0) {
        type = RKIMAGE_TYPE_KRNL;
        signed_file = true;
        rkfile_signed_hdr_t *hdr = (rkfile_signed_hdr_t *)data;
        encrypted = (hdr->rc4_flag == 1);
    }
    // Check for signed PARM
    else if (size >= 48 && memcmp(data, RKSIGNED_MAGIC_PARM, 12) == 0) {
        type = RKIMAGE_TYPE_PARM;
        signed_file = true;
        rkfile_signed_hdr_t *hdr = (rkfile_signed_hdr_t *)data;
        encrypted = (hdr->rc4_flag == 1);
    }
    // Check for KRNL
    else if (size >= 4 && memcmp(data, RKFILE_MAGIC_KRNL, 4) == 0) {
        type = RKIMAGE_TYPE_KRNL;
    }
    // Check for PARM
    else if (size >= 4 && memcmp(data, RKFILE_MAGIC_PARM, 4) == 0) {
        type = RKIMAGE_TYPE_PARM;
    }
    // Check for RKFP
    else if (size >= 4 && memcmp(data, RKFILE_MAGIC_RKFP, 4) == 0) {
        type = RKIMAGE_TYPE_RKFP;
    }
    // Check for bootloader (IDB) - look for patterns
    else if (size >= 512 && 
             (*(uint32_t *)&data[0] == RKBOOT_MAGIC || 
              *(uint32_t *)&data[0] == RKBOOT_MAGIC2)) {
        type = RKIMAGE_TYPE_BOOTLOADER;
    }
    
    if (info) {
        info->type = type;
        info->type_name = get_image_type_name(type);
        info->is_encrypted = encrypted;
        info->is_signed = signed_file;
        info->size = size;
        
        if (signed_file && size >= 48) {
            rkfile_signed_hdr_t *hdr = (rkfile_signed_hdr_t *)data;
            info->content_offset = hdr->header_len;
        } else {
            info->content_offset = 0;
        }
    }
    
    return type;
}

const char *get_image_type_name(rkimage_type_t type) {
    switch (type) {
        case RKIMAGE_TYPE_KRNL:      return "KRNL (Kernel)";
        case RKIMAGE_TYPE_PARM:      return "PARM (Parameter)";
        case RKIMAGE_TYPE_RKFP:      return "RKFP (United Image)";
        case RKIMAGE_TYPE_BOOTLOADER: return "Bootloader (IDB)";
        case RKIMAGE_TYPE_UBOOT:     return "U-Boot Image";
        case RKIMAGE_TYPE_RESOURCES: return "Resources Image";
        default:                     return "Unknown";
    }
}

// ===== SIGNED FILE OPERATIONS =====
bool unpack_signed_file(const char *input_file, const char *output_dir) {
    FILE *f = fopen(input_file, "rb");
    if (!f) return false;
    
    // Read header
    rkfile_signed_hdr_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        fclose(f);
        return false;
    }
    
    // Verify signature
    if (memcmp(hdr.signature, RKSIGNED_MAGIC_KRNL, 12) != 0 &&
        memcmp(hdr.signature, RKSIGNED_MAGIC_PARM, 12) != 0) {
        fclose(f);
        return false;
    }
    
    // Read file content
    uint8_t *data = malloc(hdr.file_len);
    if (!data) {
        fclose(f);
        return false;
    }
    
    if (fread(data, hdr.file_len, 1, f) != 1) {
        free(data);
        fclose(f);
        return false;
    }
    
    fclose(f);
    
    // Decrypt if encrypted
    if (hdr.rc4_flag) {
        // Standard RockChip RC4 key: "RockChip"
        rc4_state_t rc4;
        rc4_init(&rc4, (uint8_t *)"RockChip", 8);
        rc4_crypt(&rc4, data, hdr.file_len);
    }
    
    // Write unpacked file
    char output_path[512];
    const char *file_type = (hdr.file_type[0] == 'K') ? "unkrnl" : "unparm";
    snprintf(output_path, sizeof(output_path), "%s/payload.%s", output_dir, file_type);
    
    FILE *out = fopen(output_path, "wb");
    if (!out) {
        free(data);
        return false;
    }
    
    fwrite(data, hdr.file_len, 1, out);
    fclose(out);
    free(data);
    
    return true;
}

bool pack_signed_file(const char *input_file, const char *output_file, bool encrypt_rc4) {
    FILE *f = fopen(input_file, "rb");
    if (!f) return false;
    
    // Get file size
    fseek(f, 0, SEEK_END);
    uint32_t file_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Read file content
    uint8_t *data = malloc(file_len);
    if (!data) {
        fclose(f);
        return false;
    }
    
    if (fread(data, file_len, 1, f) != 1) {
        free(data);
        fclose(f);
        return false;
    }
    fclose(f);
    
    // Prepare header
    rkfile_signed_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    
    // Determine file type from filename
    bool is_krnl = (strstr(input_file, "krnl") != NULL);
    memcpy(hdr.signature, is_krnl ? RKSIGNED_MAGIC_KRNL : RKSIGNED_MAGIC_PARM, 12);
    memcpy(hdr.file_type, is_krnl ? RKFILE_MAGIC_KRNL : RKFILE_MAGIC_PARM, 4);
    
    hdr.header_len = 0x30;  // Standard header size
    hdr.file_len = file_len;
    hdr.rc4_flag = encrypt_rc4 ? 1 : 0;
    
    // Calculate CRC32
    hdr.crc32 = sha1_crc32(data, file_len);
    
    // Calculate SHA256 (using simplified version)
    sha1_init();
    sha1_update(data, file_len);
    sha1_final(hdr.sha256);
    
    // Encrypt if requested
    if (encrypt_rc4) {
        uint8_t *encrypted = malloc(file_len);
        memcpy(encrypted, data, file_len);
        rc4_state_t rc4;
        rc4_init(&rc4, (uint8_t *)"RockChip", 8);
        rc4_crypt(&rc4, encrypted, file_len);
        free(data);
        data = encrypted;
    }
    
    // Write output file
    FILE *out = fopen(output_file, "wb");
    if (!out) {
        free(data);
        return false;
    }
    
    fwrite(&hdr, sizeof(hdr), 1, out);
    fwrite(data, file_len, 1, out);
    fclose(out);
    free(data);
    
    return true;
}

// ===== BOOTLOADER OPERATIONS =====
bool extract_idb_payloads(const uint8_t *idb_data, size_t idb_size, const char *output_dir) {
    if (idb_size < 512) return false;
    
    rk_bootloader_hdr_t *hdr = (rk_bootloader_hdr_t *)idb_data;
    
    // Extract FlashData payload
    if (hdr->flash_data_offset > 0 && hdr->flash_data_offset * 512 < idb_size) {
        uint32_t flash_data_offset = hdr->flash_data_offset * 512;
        uint32_t flash_data_size = 0;
        
        // Estimate size from next offset or remaining data
        if (hdr->entries[1] > 0) {
            flash_data_size = (hdr->entries[1] - hdr->flash_data_offset) * 512;
        } else {
            flash_data_size = idb_size - flash_data_offset;
        }
        
        char path[256];
        snprintf(path, sizeof(path), "%s/FlashData_dump.rc4", output_dir);
        
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(&idb_data[flash_data_offset], flash_data_size, 1, f);
            fclose(f);
        }
    }
    
    // Extract FlashBoot payload (typically right after FlashData)
    if (idb_size > 1024) {
        char path[256];
        snprintf(path, sizeof(path), "%s/FlashBoot_dump", output_dir);
        
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(&idb_data[512], 512, 1, f);  // Simple approximation
            fclose(f);
        }
    }
    
    return true;
}

bool unpack_bootloader(const uint8_t *idb_data, size_t idb_size, const char *output_dir) {
    // Create IDB subdirectory
    char idb_dir[256];
    snprintf(idb_dir, sizeof(idb_dir), "%s/IDB", output_dir);
    mkdir(idb_dir, 0755);
    
    // Extract payloads
    return extract_idb_payloads(idb_data, idb_size, idb_dir);
}

// ===== RKFP IMAGE OPERATIONS =====
bool unpack_rkfp_image(const char *input_file, const char *output_dir) {
    FILE *f = fopen(input_file, "rb");
    if (!f) return false;
    
    // Read RKFP header
    rkfp_image_hdr_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        fclose(f);
        return false;
    }
    
    if (memcmp(hdr.magic, RKFILE_MAGIC_RKFP, 4) != 0) {
        fclose(f);
        return false;
    }
    
    // Read image entries
    for (uint32_t i = 0; i < hdr.image_count; i++) {
        rkfp_image_entry_t entry;
        if (fread(&entry, sizeof(entry), 1, f) != 1) break;
        
        // Extract image
        fseek(f, entry.offset, SEEK_SET);
        uint8_t *data = malloc(entry.size);
        if (!data) continue;
        
        if (fread(data, entry.size, 1, f) != 1) {
            free(data);
            continue;
        }
        
        // Write image file
        char output_path[256];
        snprintf(output_path, sizeof(output_path), "%s/%s.img", output_dir, entry.name);
        
        FILE *out = fopen(output_path, "wb");
        if (out) {
            fwrite(data, entry.size, 1, out);
            fclose(out);
        }
        free(data);
    }
    
    fclose(f);
    return true;
}

bool pack_rkfp_image(const char *output_file, const char *input_dir) {
    FILE *out = fopen(output_file, "wb");
    if (!out) return false;
    
    // Prepare header
    rkfp_image_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, RKFILE_MAGIC_RKFP, 4);
    hdr.header_size = sizeof(rkfp_image_hdr_t);
    
    // Scan input directory for images
    DIR *dir = opendir(input_dir);
    if (!dir) {
        fclose(out);
        return false;
    }
    
    struct dirent *entry;
    uint32_t image_count = 0;
    uint32_t current_offset = hdr.header_size;
    
    // Count images first
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".img") != NULL) {
            image_count++;
        }
    }
    
    rewinddir(dir);
    hdr.image_count = image_count;
    hdr.file_size = hdr.header_size + (image_count * sizeof(rkfp_image_entry_t));
    
    // Calculate total size
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".img") != NULL) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", input_dir, entry->d_name);
            
            struct stat st;
            if (stat(path, &st) == 0) {
                hdr.file_size += st.st_size;
            }
        }
    }
    
    // Write header
    fwrite(&hdr, sizeof(hdr), 1, out);
    
    // Write entries and data
    rewinddir(dir);
    current_offset = hdr.header_size + (image_count * sizeof(rkfp_image_entry_t));
    
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".img") != NULL) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", input_dir, entry->d_name);
            
            FILE *img = fopen(path, "rb");
            if (!img) continue;
            
            fseek(img, 0, SEEK_END);
            uint32_t size = ftell(img);
            fseek(img, 0, SEEK_SET);
            
            // Write entry
            rkfp_image_entry_t img_entry;
            memset(&img_entry, 0, sizeof(img_entry));
            img_entry.offset = current_offset;
            img_entry.size = size;
            strncpy((char *)img_entry.name, entry->d_name, 31);
            
            fwrite(&img_entry, sizeof(img_entry), 1, out);
            current_offset += size;
            fclose(img);
        }
    }
    
    closedir(dir);
    
    // Write image data
    dir = opendir(input_dir);
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".img") != NULL) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", input_dir, entry->d_name);
            
            FILE *img = fopen(path, "rb");
            if (!img) continue;
            
            uint8_t buffer[65536];
            size_t bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), img)) > 0) {
                fwrite(buffer, bytes, 1, out);
            }
            fclose(img);
        }
    }
    
    closedir(dir);
    fclose(out);
    return true;
}

// ===== SIGNATURE VERIFICATION =====
bool verify_file_signature(const uint8_t *file_data, size_t file_size) {
    if (file_size < 48) return false;
    
    rkfile_signed_hdr_t *hdr = (rkfile_signed_hdr_t *)file_data;
    
    // Verify magic signature
    if (memcmp(hdr->signature, RKSIGNED_MAGIC_KRNL, 12) != 0 &&
        memcmp(hdr->signature, RKSIGNED_MAGIC_PARM, 12) != 0) {
        return false;
    }
    
    // Verify CRC32
    uint32_t calculated_crc = sha1_crc32(&file_data[hdr->header_len], hdr->file_len);
    if (calculated_crc != hdr->crc32) {
        return false;
    }
    
    return true;
}

bool calculate_file_signature(const uint8_t *data, size_t size, uint8_t *signature) {
    if (!signature) return false;
    
    sha1_init();
    sha1_update(data, size);
    sha1_final(signature);
    
    return true;
}
