/*
 * IMU data reader from shared memory
 * 
 * Reads IMU data from /dev/shm/breezy_desktop_imu
 * Format matches XRLinuxDriver's shared memory layout
 */

#include "breezy_xfce4_renderer.h"
#include "logging.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define IMU_SHM_PATH "/dev/shm/breezy_desktop_imu"
#define DATA_LAYOUT_VERSION 5

// Data layout offsets (matches devicedatastream.js)
#define OFFSET_VERSION 0
#define OFFSET_ENABLED 1
#define OFFSET_LOOK_AHEAD_CFG 2
#define OFFSET_DISPLAY_RES 18
#define OFFSET_DISPLAY_FOV 26
#define OFFSET_LENS_DISTANCE_RATIO 30
#define OFFSET_SBS_ENABLED 34
#define OFFSET_CUSTOM_BANNER_ENABLED 35
#define OFFSET_SMOOTH_FOLLOW_ENABLED 36
#define OFFSET_SMOOTH_FOLLOW_ORIGIN_DATA 37
#define OFFSET_POSE_POSITION 101
#define OFFSET_EPOCH_MS 113
#define OFFSET_POSE_ORIENTATION 121
#define OFFSET_IMU_PARITY_BYTE 185

static uint8_t calculate_parity(const uint8_t *data) {
    uint8_t parity = 0;
    // XOR all bytes in epoch and pose_orientation
    for (int i = OFFSET_EPOCH_MS; i < OFFSET_IMU_PARITY_BYTE; i++) {
        parity ^= data[i];
    }
    return parity;
}

int init_imu_reader(IMUReader *reader) {
    memset(reader, 0, sizeof(*reader));
    reader->shm_fd = -1;
    reader->latest.valid = false;
    
    if (pthread_mutex_init(&reader->lock, NULL) != 0) {
        log_error("[IMU] Failed to initialize mutex\n");
        return -1;
    }
    
    // Open shared memory file
    reader->shm_fd = open(IMU_SHM_PATH, O_RDONLY);
    if (reader->shm_fd < 0) {
        log_error("[IMU] Failed to open %s: %s\n", IMU_SHM_PATH, strerror(errno));
        return -1;
    }
    
    // Get file size
    struct stat st;
    if (fstat(reader->shm_fd, &st) < 0) {
        log_error("[IMU] Failed to stat %s: %s\n", IMU_SHM_PATH, strerror(errno));
        close(reader->shm_fd);
        reader->shm_fd = -1;
        return -1;
    }
    reader->shm_size = st.st_size;
    
    // Map shared memory
    reader->shm_ptr = mmap(NULL, reader->shm_size, PROT_READ, MAP_SHARED, reader->shm_fd, 0);
    if (reader->shm_ptr == MAP_FAILED) {
        log_error("[IMU] Failed to mmap %s: %s\n", IMU_SHM_PATH, strerror(errno));
        close(reader->shm_fd);
        reader->shm_fd = -1;
        return -1;
    }
    
    // Check version
    uint8_t version = ((uint8_t *)reader->shm_ptr)[OFFSET_VERSION];
    if (version != DATA_LAYOUT_VERSION) {
        log_warn("[IMU] Version mismatch: expected %d, got %d\n", DATA_LAYOUT_VERSION, version);
        // Continue anyway - might work
    }
    
    log_info("[IMU] Reader initialized, mapped %zu bytes\n", reader->shm_size);
    return 0;
}

void cleanup_imu_reader(IMUReader *reader) {
    if (reader->shm_ptr && reader->shm_fd >= 0) {
        munmap(reader->shm_ptr, reader->shm_size);
        close(reader->shm_fd);
        reader->shm_ptr = NULL;
        reader->shm_fd = -1;
    }
    pthread_mutex_destroy(&reader->lock);
}

IMUData read_latest_imu(IMUReader *reader) {
    IMUData result = {0};
    result.valid = false;
    
    if (!reader->shm_ptr || reader->shm_fd < 0) {
        return result;
    }
    
    pthread_mutex_lock(&reader->lock);
    
    const uint8_t *data = (const uint8_t *)reader->shm_ptr;
    
    // Check if enabled
    bool enabled = data[OFFSET_ENABLED] != 0;
    if (!enabled) {
        pthread_mutex_unlock(&reader->lock);
        return result;
    }
    
    // Verify parity
    uint8_t expected_parity = calculate_parity(data);
    uint8_t actual_parity = data[OFFSET_IMU_PARITY_BYTE];
    if (expected_parity != actual_parity) {
        // Parity mismatch - data might be corrupted or in transition
        pthread_mutex_unlock(&reader->lock);
        return result;
    }
    
    // Read pose position (3 floats)
    memcpy(result.position, &data[OFFSET_POSE_POSITION], sizeof(float) * 3);
    
    // Read epoch (2 uints, milliseconds)
    uint32_t epoch_low, epoch_high;
    memcpy(&epoch_low, &data[OFFSET_EPOCH_MS], sizeof(uint32_t));
    memcpy(&epoch_high, &data[OFFSET_EPOCH_MS + 4], sizeof(uint32_t));
    result.timestamp_ms = ((uint64_t)epoch_high << 32) | epoch_low;
    
    // Read pose orientation (16 floats = 4x4 matrix)
    // Row 0-2: quaternions at t0, t1, t2 (each 4 floats: x, y, z, w)
    // Row 3: timestamps (4 floats: timestamp_t0, timestamp_t1, timestamp_t2, unused)
    memcpy(result.pose_orientation, &data[OFFSET_POSE_ORIENTATION], sizeof(float) * 16);
    
    result.valid = true;
    
    // Update latest
    reader->latest = result;
    
    pthread_mutex_unlock(&reader->lock);
    return result;
}

// Read device configuration from shared memory
DeviceConfig read_device_config(IMUReader *reader) {
    DeviceConfig config = {0};
    config.valid = false;
    
    if (!reader->shm_ptr || reader->shm_fd < 0) {
        return config;
    }
    
    pthread_mutex_lock(&reader->lock);
    
    const uint8_t *data = (const uint8_t *)reader->shm_ptr;
    
    // Check if enabled
    bool enabled = data[OFFSET_ENABLED] != 0;
    if (!enabled) {
        pthread_mutex_unlock(&reader->lock);
        return config;
    }
    
    // Verify parity
    uint8_t expected_parity = calculate_parity(data);
    uint8_t actual_parity = data[OFFSET_IMU_PARITY_BYTE];
    if (expected_parity != actual_parity) {
        pthread_mutex_unlock(&reader->lock);
        return config;
    }
    
    // Read look ahead config (4 floats)
    memcpy(config.look_ahead_cfg, &data[OFFSET_LOOK_AHEAD_CFG], sizeof(float) * 4);
    
    // Read display resolution (2 uints)
    memcpy(config.display_resolution, &data[OFFSET_DISPLAY_RES], sizeof(uint32_t) * 2);
    
    // Read display FOV (1 float)
    memcpy(&config.display_fov, &data[OFFSET_DISPLAY_FOV], sizeof(float));
    
    // Read lens distance ratio (1 float)
    memcpy(&config.lens_distance_ratio, &data[OFFSET_LENS_DISTANCE_RATIO], sizeof(float));
    
    // Read SBS enabled (1 bool)
    config.sbs_enabled = data[OFFSET_SBS_ENABLED] != 0;
    
    // Read custom banner enabled (1 bool)
    config.custom_banner_enabled = data[OFFSET_CUSTOM_BANNER_ENABLED] != 0;
    
    // Read smooth follow enabled (1 bool)
    config.smooth_follow_enabled = data[OFFSET_SMOOTH_FOLLOW_ENABLED] != 0;
    
    // Read smooth follow origin (16 floats = 4x4 matrix)
    memcpy(config.smooth_follow_origin, &data[OFFSET_SMOOTH_FOLLOW_ORIGIN_DATA], sizeof(float) * 16);
    
    config.valid = true;
    
    pthread_mutex_unlock(&reader->lock);
    return config;
}

