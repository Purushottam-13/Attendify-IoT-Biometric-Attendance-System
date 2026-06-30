/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  Fingerprint HAL - R307 Sensor Driver
 * ============================================================
 */

#ifndef FINGERPRINT_HAL_H
#define FINGERPRINT_HAL_H

#include <Arduino.h>

// ============================================================
// FINGERPRINT CONFIGURATION
// ============================================================
#define FP_RX_PIN  16
#define FP_TX_PIN  17
#define FP_BAUD    57600

enum FingerprintEnrollError {
	FP_ENROLL_ERROR_NONE = 0,
	FP_ENROLL_ERROR_ID_OUT_OF_RANGE,
	FP_ENROLL_ERROR_TIMEOUT_FIRST_SCAN,
	FP_ENROLL_ERROR_COMMUNICATION,
	FP_ENROLL_ERROR_IMAGE_CONVERT_1,
	FP_ENROLL_ERROR_TIMEOUT_SECOND_SCAN,
	FP_ENROLL_ERROR_IMAGE_CONVERT_2,
	FP_ENROLL_ERROR_FINGER_MISMATCH,
	FP_ENROLL_ERROR_STORE_FAILED
};

// ============================================================
// FINGERPRINT FUNCTIONS
// ============================================================

/**
 * Initialize fingerprint sensor
 * @return true if sensor detected
 */
bool fingerprint_init();

/**
 * Check if sensor is connected
 * @return true if sensor responds
 */
bool fingerprint_check();

/**
 * Enroll a new fingerprint
 * @param id Fingerprint ID (1-127)
 * @return true if enrollment successful
 */
bool fingerprint_enroll(uint16_t id);

/**
 * Returns last enrollment error code
 */
FingerprintEnrollError fingerprint_get_last_enroll_error();

/**
 * Returns short message for last enrollment error
 */
const char *fingerprint_get_last_enroll_error_text();

/**
 * Verify a fingerprint against database
 * @param matchedId Output: ID of matched fingerprint
 * @return true if match found
 */
bool fingerprint_verify(uint16_t* matchedId);

/**
 * Delete a fingerprint from database
 * @param id Fingerprint ID to delete
 * @return true if deleted
 */
bool fingerprint_delete(uint16_t id);

/**
 * Get number of stored templates
 * @return Count of stored fingerprints
 */
uint16_t fingerprint_count();

/**
 * Empty the fingerprint database
 * @return true if cleared
 */
bool fingerprint_clear_all();

/**
 * Download a fingerprint template from the sensor database
 * @param id Fingerprint ID to download
 * @param templateData Output buffer of size 512 bytes
 * @return true if download successful
 */
bool fingerprint_download_template(uint16_t id, uint8_t* templateData);

/**
 * Upload a fingerprint template to the sensor database
 * @param id Fingerprint ID to store
 * @param templateData Input buffer of size 512 bytes
 * @return true if upload and store successful
 */
bool fingerprint_upload_template(uint16_t id, const uint8_t* templateData);

/**
 * Restore all fingerprint templates from the SD card to the sensor database
 * @return true if successful
 */
bool fingerprint_restore_all_from_sd();

/**
 * Check if a fingerprint slot is occupied on the sensor
 * @param id Fingerprint ID to check
 * @return true if a template exists at that slot
 */
bool fingerprint_is_slot_occupied(uint16_t id);

/**
 * Turn off fingerprint sensor LED
 */
void fingerprint_led_off();

/**
 * Turn on fingerprint sensor LED
 */
void fingerprint_led_on();

#endif // FINGERPRINT_HAL_H
