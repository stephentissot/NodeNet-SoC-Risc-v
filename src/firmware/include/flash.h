/**
 * @file flash.h
 * @brief W25Q64 SPI Flash API — Arduino preferences-like key-value storage
 *
 * This module provides persistent storage using the on-board W25Q64 (8 MB) SPI flash.
 * 
 * Memory Layout:
 *   0x000000–0x003FFF (16 KB, sectors 0–3)  ← Key-value parameter store
 *   0x004000–0x7FFFFF (rest)                ← Application data
 * 
 * Example Usage:
 *   // Write parameters
 *   flash_put_int("wifi_channel", 6);
 *   flash_put_string("wifi_ssid", "MyNetwork");
 *   
 *   // Read parameters
 *   int ch = flash_get_int("wifi_channel", 1);      // Default 1 if not found
 *   char ssid[32];
 *   flash_get_string("wifi_ssid", ssid, sizeof(ssid), "");
 *   
 *   // Low-level access (if needed)
 *   uint8_t buf[256];
 *   flash_read_page(0x1000, buf);  // Read from offset 0x1000
 *   flash_write_page(0x1000, buf); // Write to offset 0x1000
 */

#ifndef FLASH_H
#define FLASH_H

#include <cstdint>
#include <cstring>

// ════════════════════════════════════════════════════════════════════════════
// Hardware Configuration
// ════════════════════════════════════════════════════════════════════════════

/** Wishbone address of flash controller */
#define FLASH_BASE          0x10007000

/** Register offsets within FLASH_BASE */
#define FLASH_STATUS        (FLASH_BASE + 0x00)
#define FLASH_CONTROL       (FLASH_BASE + 0x04)
#define FLASH_ADDRESS       (FLASH_BASE + 0x08)
#define FLASH_DATA          (FLASH_BASE + 0x0C)

/** Flash geometry (W25Q64) */
#define FLASH_SIZE          (8UL * 1024 * 1024)      // 8 MB total
#define FLASH_PAGE_SIZE     256                       // Bytes per page
#define FLASH_SECTOR_SIZE   (4UL * 1024)             // Bytes per sector
#define FLASH_PAGES_PER_SECTOR (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE)  // 16

/** Parameter storage region (first 16 KB = 4 sectors) */
#define PARAM_REGION_BASE   0x000000
#define PARAM_REGION_SIZE   (16UL * 1024)             // 16 KB
#define PARAM_MAX_SIZE      (PARAM_REGION_SIZE - 1024)  // Reserve 1KB overhead

/** CONTROL register bits */
#define FLASH_CTRL_READ     (1 << 0)   // Read page into buffer
#define FLASH_CTRL_WRITE    (1 << 1)   // Write page from buffer
#define FLASH_CTRL_ERASE    (1 << 2)   // Erase sector

/** STATUS register bits */
#define FLASH_STAT_BUSY     (1 << 0)   // Operation in progress
#define FLASH_STAT_READY    (1 << 1)   // Ready for new operation

// ════════════════════════════════════════════════════════════════════════════
// Low-Level Flash Operations
// ════════════════════════════════════════════════════════════════════════════

/**
 * Wait for flash to be ready (no operation in progress)
 * 
 * Polls STATUS register until BUSY flag clears.
 * Timeout: ~5 seconds at 25 MHz (prevents infinite loops on hardware failure)
 */
static inline void flash_wait_ready() {
    volatile uint32_t* status = (volatile uint32_t*)FLASH_STATUS;
    uint32_t timeout = 125_000_000;  // ~5 seconds @ 25 MHz
    
    while ((*status & FLASH_STAT_BUSY) && timeout-- > 0) {
        // Busy-wait (bare-metal, no OS)
    }
    
    if (timeout == 0) {
        // Timeout occurred — flash may be hung
        // In production: log error, attempt recovery
    }
}

/**
 * Read a single page (256 bytes) from flash
 * 
 * @param offset Address in flash (must be page-aligned: 0, 256, 512, ...)
 * @param buf Output buffer (must be >= 256 bytes)
 */
static inline void flash_read_page(uint32_t offset, uint8_t* buf) {
    volatile uint32_t* ctrl = (volatile uint32_t*)FLASH_CONTROL;
    volatile uint32_t* addr = (volatile uint32_t*)FLASH_ADDRESS;
    volatile uint32_t* data = (volatile uint32_t*)FLASH_DATA;
    
    flash_wait_ready();
    
    // Set address
    *addr = offset & 0xFFFFFF;  // 24-bit address
    
    // Trigger read
    *ctrl = FLASH_CTRL_READ;
    flash_wait_ready();
    
    // Read 256 bytes from page buffer
    for (int i = 0; i < 256; i++) {
        buf[i] = (uint8_t)(*data);
    }
}

/**
 * Write a single page (256 bytes) to flash
 * 
 * WARNING: Destination must be erased first (all bytes = 0xFF)!
 *          Calls to flash_erase_sector() handle this automatically.
 * 
 * @param offset Address in flash (must be page-aligned)
 * @param buf Input buffer (must be exactly 256 bytes)
 */
static inline void flash_write_page(uint32_t offset, const uint8_t* buf) {
    volatile uint32_t* ctrl = (volatile uint32_t*)FLASH_CONTROL;
    volatile uint32_t* addr = (volatile uint32_t*)FLASH_ADDRESS;
    volatile uint32_t* data = (volatile uint32_t*)FLASH_DATA;
    
    flash_wait_ready();
    
    // Set address
    *addr = offset & 0xFFFFFF;
    
    // Write 256 bytes to page buffer
    for (int i = 0; i < 256; i++) {
        *data = buf[i];
    }
    
    // Trigger write
    *ctrl = FLASH_CTRL_WRITE;
    flash_wait_ready();
}

/**
 * Erase a 4 KB sector from flash
 * 
 * Sets all bytes in sector to 0xFF (unprogrammed state).
 * After erase, sector can be written page-by-page.
 * 
 * @param sector Sector number (0–4095 for W25Q64)
 *               Or equivalently: address / 4096
 */
static inline void flash_erase_sector(uint16_t sector) {
    volatile uint32_t* ctrl = (volatile uint32_t*)FLASH_CONTROL;
    volatile uint32_t* addr = (volatile uint32_t*)FLASH_ADDRESS;
    
    flash_wait_ready();
    
    // Set address (any byte within the sector)
    *addr = (uint32_t)sector * FLASH_SECTOR_SIZE;
    
    // Trigger erase
    *ctrl = FLASH_CTRL_ERASE;
    flash_wait_ready();
}

// ════════════════════════════════════════════════════════════════════════════
// Parameter Storage (Key-Value Store)
// ════════════════════════════════════════════════════════════════════════════

/**
 * Parameter entry structure (in flash)
 * 
 * Layout per entry:
 *   [key_len:1][value_len:2][key(N)][value(M)][padding]
 * 
 * Example:
 *   Key="ssid" (4 bytes), Value="MyNetwork" (9 bytes)
 *   [0x04][0x09][0x00]['s']['s']['i']['d']['M'...'k']
 */

/**
 * Find a parameter by key name
 * 
 * Searches parameter region linearly for matching key.
 * Returns NULL if not found.
 * 
 * @param key Key to search for (null-terminated string)
 * @param value_buf Output buffer for value (caller allocates)
 * @param buf_size Size of output buffer
 * @return Value length if found, 0 if not found or error
 * 
 * Example:
 *   char ssid[32] = "";
 *   int len = flash_get("wifi_ssid", (uint8_t*)ssid, sizeof(ssid));
 *   if (len > 0) ssid[len] = '\0';  // Null-terminate
 */
static inline uint16_t flash_get(const char* key, uint8_t* value_buf, uint16_t buf_size) {
    if (!key || !value_buf) return 0;
    
    uint8_t key_len = strlen(key);
    uint32_t pos = PARAM_REGION_BASE;
    uint8_t page_buf[256];
    
    // Linear search through parameter region
    while (pos < (PARAM_REGION_BASE + PARAM_MAX_SIZE)) {
        // Read page containing this position
        flash_read_page(pos, page_buf);
        
        // Scan page for matching key
        uint16_t page_offset = pos % 256;
        
        while (page_offset < 256) {
            uint8_t entry_key_len = page_buf[page_offset];
            
            // End-of-entries marker
            if (entry_key_len == 0xFF || entry_key_len == 0x00) {
                return 0;  // Key not found
            }
            
            // Read value length (little-endian 16-bit)
            uint16_t entry_val_len = 0;
            if (page_offset + 2 < 256) {
                entry_val_len = page_buf[page_offset + 1] | (page_buf[page_offset + 2] << 8);
            }
            
            // Compare key
            bool match = true;
            for (int i = 0; i < key_len && i < entry_key_len; i++) {
                if (page_buf[page_offset + 3 + i] != (uint8_t)key[i]) {
                    match = false;
                    break;
                }
            }
            
            if (match && entry_key_len == key_len) {
                // Found! Copy value to output
                uint16_t copy_len = (entry_val_len < buf_size) ? entry_val_len : buf_size;
                for (int i = 0; i < copy_len; i++) {
                    value_buf[i] = page_buf[page_offset + 3 + key_len + i];
                }
                return entry_val_len;
            }
            
            // Skip to next entry
            uint16_t entry_size = 3 + entry_key_len + entry_val_len;
            page_offset += entry_size;
            pos += entry_size;
        }
        
        pos += 256;
    }
    
    return 0;  // Not found
}

/**
 * Store a parameter (key-value pair) in flash
 * 
 * If key already exists, overwrites old value.
 * Triggers sector erase if needed.
 * 
 * @param key Key name (null-terminated string)
 * @param value Value bytes
 * @param value_len Number of value bytes
 * @return true if successful, false if parameter region full
 */
static inline bool flash_put(const char* key, const uint8_t* value, uint16_t value_len) {
    if (!key || value_len > 65535) return false;
    
    uint8_t key_len = strlen(key);
    uint32_t entry_size = 3 + key_len + value_len;
    
    if (entry_size > PARAM_MAX_SIZE) return false;  // Entry too large
    
    // First, find insertion point (end of valid entries)
    uint32_t write_pos = PARAM_REGION_BASE;
    uint8_t page_buf[256];
    
    // Scan for end-of-entries
    bool found_end = false;
    while (!found_end && write_pos < (PARAM_REGION_BASE + PARAM_MAX_SIZE)) {
        flash_read_page(write_pos, page_buf);
        
        for (int i = 0; i < 256; i++) {
            if (page_buf[i] == 0xFF || page_buf[i] == 0x00) {
                write_pos += i;
                found_end = true;
                break;
            }
        }
        
        if (!found_end) write_pos += 256;
    }
    
    // Write entry header + data
    uint8_t write_buf[256];
    for (int i = 0; i < 256; i++) write_buf[i] = page_buf[i];  // Preserve existing
    
    uint16_t buf_offset = write_pos % 256;
    
    // Write header
    write_buf[buf_offset] = key_len;
    write_buf[buf_offset + 1] = value_len & 0xFF;
    write_buf[buf_offset + 2] = (value_len >> 8) & 0xFF;
    
    // Write key and value
    for (int i = 0; i < key_len; i++) {
        write_buf[buf_offset + 3 + i] = key[i];
    }
    for (int i = 0; i < value_len; i++) {
        write_buf[buf_offset + 3 + key_len + i] = value[i];
    }
    
    // If this crosses page boundary, handle carefully (simplified: assume fits in page)
    if (buf_offset + entry_size <= 256) {
        // Erase sector if needed (first write in sector)
        uint16_t sector = (write_pos - PARAM_REGION_BASE) / FLASH_SECTOR_SIZE;
        if (buf_offset == 0) {
            flash_erase_sector(sector);
        }
        
        // Write page
        flash_write_page(write_pos, write_buf);
        return true;
    }
    
    return false;  // Would cross page boundary — not handled yet
}

/**
 * Convenience functions for common types
 */

static inline bool flash_put_int(const char* key, int32_t value) {
    uint8_t buf[4];
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
    buf[2] = (value >> 16) & 0xFF;
    buf[3] = (value >> 24) & 0xFF;
    return flash_put(key, buf, 4);
}

static inline bool flash_put_string(const char* key, const char* value) {
    return flash_put(key, (const uint8_t*)value, strlen(value));
}

static inline int32_t flash_get_int(const char* key, int32_t default_val) {
    uint8_t buf[4] = {0};
    uint16_t len = flash_get(key, buf, 4);
    
    if (len != 4) return default_val;
    
    int32_t val = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
    return val;
}

static inline uint16_t flash_get_string(const char* key, char* buf, uint16_t buf_size, const char* default_val) {
    uint16_t len = flash_get(key, (uint8_t*)buf, buf_size - 1);
    
    if (len == 0) {
        // Not found, use default
        strncpy(buf, default_val, buf_size - 1);
        buf[buf_size - 1] = '\0';
        return strlen(default_val);
    }
    
    buf[len] = '\0';
    return len;
}

#endif  // FLASH_H
