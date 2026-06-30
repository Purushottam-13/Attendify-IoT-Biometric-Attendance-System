/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  Fingerprint HAL - R307 Sensor Implementation
 * ============================================================
 */

#include "fingerprint_hal.h"
#include "sd_hal.h"
#include <Adafruit_Fingerprint.h>

// ============================================================
// SENSOR OBJECT
// ============================================================
static HardwareSerial fpSerial(2); // UART2
static Adafruit_Fingerprint finger(&fpSerial);
static FingerprintEnrollError lastEnrollError = FP_ENROLL_ERROR_NONE;

// Helper to write raw packets directly to the sensor UART.
// Avoids using internal protected library functions.
static void write_raw_packet(uint8_t packettype, uint16_t len, const uint8_t *packet) {
  // 1. Send start code (0xEF01)
  fpSerial.write(0xEF);
  fpSerial.write(0x01);
  // 2. Send address (0xFFFFFFFF)
  fpSerial.write(0xFF);
  fpSerial.write(0xFF);
  fpSerial.write(0xFF);
  fpSerial.write(0xFF);
  // 3. Send packet type
  fpSerial.write(packettype);
  // 4. Send length (data length + 2 for checksum)
  uint16_t wire_length = len + 2;
  fpSerial.write((uint8_t)(wire_length >> 8));
  fpSerial.write((uint8_t)(wire_length & 0xFF));
  // 5. Send data and calculate checksum
  uint16_t sum = ((wire_length) >> 8) + ((wire_length) & 0xFF) + packettype;
  for (uint16_t i = 0; i < len; i++) {
    fpSerial.write(packet[i]);
    sum += packet[i];
  }
  // 6. Send checksum
  fpSerial.write((uint8_t)(sum >> 8));
  fpSerial.write((uint8_t)(sum & 0xFF));
}

// Helper to read command ACK packets directly from the sensor UART.
static bool read_raw_ack(uint8_t &confirmationCode, uint16_t timeout = 2000) {
  uint8_t byte;
  uint16_t idx = 0;
  uint16_t len = 0;
  unsigned long startTime = millis();

  while (true) {
    if ((millis() - startTime) >= timeout) {
      Serial.println("[FP] Read ACK timeout");
      return false;
    }
    if (!fpSerial.available()) {
      delay(1);
      continue;
    }
    byte = fpSerial.read();
    switch (idx) {
    case 0:
      if (byte != 0xEF) continue;
      break;
    case 1:
      if (byte != 0x01) { idx = 0; continue; }
      break;
    case 2:
    case 3:
    case 4:
    case 5:
      // Address bytes (expecting 0xFF)
      break;
    case 6:
      // Packet type (expecting FINGERPRINT_ACKPACKET = 0x07)
      if (byte != 0x07) { idx = 0; continue; }
      break;
    case 7:
      len = (uint16_t)byte << 8;
      break;
    case 8:
      len |= byte;
      break;
    case 9:
      confirmationCode = byte;
      break;
    default:
      // Read remaining bytes of the packet (including checksum)
      if ((idx - 8) >= len) {
        return true;
      }
      break;
    }
    idx++;
  }
}

static void setEnrollError(FingerprintEnrollError error) {
  lastEnrollError = error;
}

// ============================================================
// INITIALIZATION
// ============================================================
bool fingerprint_init() {
  fpSerial.begin(FP_BAUD, SERIAL_8N1, FP_RX_PIN, FP_TX_PIN);

  // Wait for sensor to power up (some sensors need 500ms+)
  delay(300);

  finger.begin(FP_BAUD);

  // Try multiple times - sensor may need warm-up time
  for (int attempt = 0; attempt < 3; attempt++) {
    if (finger.verifyPassword()) {
      Serial.println("[FP] Sensor found and verified");
      Serial.print("[FP] Capacity: ");
      Serial.println(finger.capacity);
      return true;
    }
    Serial.printf("[FP] Attempt %d failed, retrying...\n", attempt + 1);
    delay(200);
  }

  Serial.println("[FP] Sensor NOT found after 3 attempts");
  return false;
}

// ============================================================
// STATUS CHECKS
// ============================================================
bool fingerprint_check() { return finger.verifyPassword(); }

uint16_t fingerprint_count() {
  finger.getTemplateCount();
  return finger.templateCount;
}

// ============================================================
// ENROLLMENT
// ============================================================
bool fingerprint_enroll(uint16_t id) {
  Serial.print("[FP] Enrolling ID: ");
  Serial.println(id);
  setEnrollError(FP_ENROLL_ERROR_NONE);

  // Safety check for sensor capacity (R307/R308 typically handle 256 or 512)
  if (id == 0 || id > 511) {
    Serial.println("[FP] ERROR: ID out of range (1-511)");
    setEnrollError(FP_ENROLL_ERROR_ID_OUT_OF_RANGE);
    return false;
  }

  finger.LEDcontrol(true);

  // Step 1: Get first image
  Serial.println("[FP] Place finger...");
  int p = -1;
  unsigned long startTime = millis();

  while (p != FINGERPRINT_OK) {
    if (millis() - startTime > 15000) { // 15s timeout
      Serial.println("[FP] Timeout waiting for finger");
      finger.LEDcontrol(false);
      setEnrollError(FP_ENROLL_ERROR_TIMEOUT_FIRST_SCAN);
      return false;
    }

    p = finger.getImage();
    switch (p) {
    case FINGERPRINT_OK:
      Serial.println("[FP] Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("[FP] Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      Serial.println("[FP] Imaging error");
      break;
    default:
      Serial.println("[FP] Unknown error");
      break;
    }

    // If error other than NOFINGER, we used to return false immediately.
    // But for "Imaging error" (bad read), we might want to let them retry
    // within the timeout? For now, let's stick to simple retry unless it's a
    // fatal comm error.
    if (p == FINGERPRINT_PACKETRECIEVEERR) {
      finger.LEDcontrol(false);
      setEnrollError(FP_ENROLL_ERROR_COMMUNICATION);
      return false;
    }

    delay(50);
  }

  // Step 2: Convert to template 1
  delay(100); // Give the sensor a moment to stabilize the image buffer
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.println("[FP] Image conversion failed");
    finger.LEDcontrol(false);
    setEnrollError(FP_ENROLL_ERROR_IMAGE_CONVERT_1);
    return false;
  }

  // Step 3: Convert to template 2 (using same image for single-scan)
  delay(50);
  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) {
    Serial.println("[FP] Image conversion 2 failed");
    finger.LEDcontrol(false);
    setEnrollError(FP_ENROLL_ERROR_IMAGE_CONVERT_2);
    return false;
  }

  // Step 6: Create model
  p = finger.createModel();
  if (p != FINGERPRINT_OK) {
    Serial.println("[FP] Fingerprints did not match");
    finger.LEDcontrol(false);
    setEnrollError(FP_ENROLL_ERROR_FINGER_MISMATCH);
    return false;
  }

  // Step 7: Store model
  p = finger.storeModel(id);
  if (p != FINGERPRINT_OK) {
    Serial.println("[FP] Failed to store fingerprint");
    finger.LEDcontrol(false);
    setEnrollError(FP_ENROLL_ERROR_STORE_FAILED);
    return false;
  }

  Serial.print("[FP] Enrolled successfully as ID: ");
  Serial.println(id);
  finger.LEDcontrol(false);

  // Backup to SD card
  uint8_t templateData[512];
  if (fingerprint_download_template(id, templateData)) {
    char filename[32];
    snprintf(filename, sizeof(filename), "/fps/template_%d.dat", id);
    if (sd_write_binary(filename, templateData, 512)) {
      Serial.printf("[FP] Backed up template %d to SD card\n", id);
    } else {
      Serial.printf("[FP] ERROR: Failed to write template %d backup to SD card\n", id);
    }
  } else {
    Serial.printf("[FP] ERROR: Failed to download template %d for backup\n", id);
  }

  finger.LEDcontrol(false); // Extra safety check to turn off LED after template download finishes
  return true;
}

FingerprintEnrollError fingerprint_get_last_enroll_error() {
  return lastEnrollError;
}

const char *fingerprint_get_last_enroll_error_text() {
  switch (lastEnrollError) {
  case FP_ENROLL_ERROR_NONE:
    return "No error";
  case FP_ENROLL_ERROR_ID_OUT_OF_RANGE:
    return "ID out of range";
  case FP_ENROLL_ERROR_TIMEOUT_FIRST_SCAN:
    return "Timeout first scan";
  case FP_ENROLL_ERROR_COMMUNICATION:
    return "Sensor communication error";
  case FP_ENROLL_ERROR_IMAGE_CONVERT_1:
    return "Image convert #1 failed";
  case FP_ENROLL_ERROR_TIMEOUT_SECOND_SCAN:
    return "Timeout second scan";
  case FP_ENROLL_ERROR_IMAGE_CONVERT_2:
    return "Image convert #2 failed";
  case FP_ENROLL_ERROR_FINGER_MISMATCH:
    return "Finger mismatch";
  case FP_ENROLL_ERROR_STORE_FAILED:
    return "Store model failed";
  default:
    return "Unknown enroll error";
  }
}

// ============================================================
// VERIFICATION
// ============================================================
bool fingerprint_verify(uint16_t *matchedId) {
  // Don't control LED here - called frequently, causes flicker/stuck LED
  // LED is controlled during enrollment only

  int p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    return false;
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    return false;
  }

  p = finger.fingerSearch();
  if (p != FINGERPRINT_OK) {
    return false;
  }

  // Match found
  *matchedId = finger.fingerID;
  Serial.print("[FP] Match found! ID: ");
  Serial.print(finger.fingerID);
  Serial.print(" Confidence: ");
  Serial.println(finger.confidence);

  return true;
}

// ============================================================
// MANAGEMENT
// ============================================================
bool fingerprint_delete(uint16_t id) {
  int retries = 3;
  int p = -1;
  while (retries > 0) {
    p = finger.deleteModel(id);
    if (p == FINGERPRINT_OK) {
      // Verify deletion by trying to load it - it should fail
      int loadCheck = finger.loadModel(id);
      if (loadCheck != FINGERPRINT_OK) {
        break; // Successfully deleted
      } else {
        Serial.printf("[FP] WARNING: deleteModel returned OK but loadModel still succeeded for ID %d. Retrying...\n", id);
      }
    } else {
      Serial.printf("[FP] deleteModel(%d) failed with code 0x%02X. Retrying...\n", id, p);
    }
    retries--;
    delay(50);
  }

  if (p == FINGERPRINT_OK) {
    Serial.print("[FP] Deleted ID: ");
    Serial.println(id);
    char filename[32];
    snprintf(filename, sizeof(filename), "/fps/template_%d.dat", id);
    if (sd_file_exists(filename)) {
      sd_delete_file(filename);
      Serial.printf("[FP] Deleted SD backup file: %s\n", filename);
    }
    return true;
  }
  
  Serial.printf("[FP] ERROR: Failed to delete ID %d after retries.\n", id);
  return false;
}

bool fingerprint_clear_all() {
  int retries = 3;
  int p = -1;
  while (retries > 0) {
    p = finger.emptyDatabase();
    if (p == FINGERPRINT_OK) {
      // Verify count is 0
      uint16_t count = fingerprint_count();
      if (count == 0) {
        break;
      } else {
        Serial.printf("[FP] WARNING: emptyDatabase returned OK but count is %d. Retrying...\n", count);
      }
    } else {
      Serial.printf("[FP] emptyDatabase failed with code 0x%02X. Retrying...\n", p);
    }
    retries--;
    delay(100);
  }

  if (p == FINGERPRINT_OK) {
    Serial.println("[FP] Database cleared");
    for (uint16_t id = 1; id < 512; id++) {
      char filename[32];
      snprintf(filename, sizeof(filename), "/fps/template_%d.dat", id);
      if (sd_file_exists(filename)) {
        sd_delete_file(filename);
      }
    }
    Serial.println("[FP] Deleted all template backups from SD");
    return true;
  }
  
  Serial.println("[FP] ERROR: Failed to clear fingerprint database after retries.");
  return false;
}

bool fingerprint_download_template(uint16_t id, uint8_t* templateData) {
  Serial.printf("[FP] Downloading template for ID %d...\n", id);

  // 1. Clear any serial data
  while (fpSerial.available()) {
    fpSerial.read();
  }

  // 2. Load model
  uint8_t p = finger.loadModel(id);
  if (p != FINGERPRINT_OK) {
    Serial.printf("[FP] loadModel(%d) failed, code: 0x%02X\n", id, p);
    return false;
  }

  // 3. Request model transfer
  p = finger.getModel();
  if (p != FINGERPRINT_OK) {
    Serial.printf("[FP] getModel() failed for ID %d, code: 0x%02X\n", id, p);
    return false;
  }

  // 4. Read bytes from serial
  uint8_t bytesReceived[534];
  memset(bytesReceived, 0, sizeof(bytesReceived));

  uint32_t starttime = millis();
  int i = 0;
  while (i < 534 && (millis() - starttime) < 2000) { // 2s timeout
    if (fpSerial.available()) {
      bytesReceived[i++] = fpSerial.read();
    }
  }

  if (i < 534) {
    Serial.printf("[FP] Timeout: read only %d/534 bytes\n", i);
    return false;
  }

  // 5. Decode template
  int uindx = 9;
  memcpy(templateData, bytesReceived + uindx, 256);
  uindx += 256;
  uindx += 2; // skip checksum
  uindx += 9; // skip next header
  memcpy(templateData + 256, bytesReceived + uindx, 256);

  Serial.println("[FP] Template downloaded successfully");
  return true;
}

bool fingerprint_upload_template(uint16_t id, const uint8_t* templateData) {
  Serial.printf("[FP] Uploading template for ID %d...\n", id);

  // 1. Clear serial buffer
  while (fpSerial.available()) {
    fpSerial.read();
  }

  // 2. Send FINGERPRINT_DOWNLOAD command (prepare sensor buffer CharBuffer 1)
  uint8_t cmd[] = { 0x09, 0x01 }; // DownChar command (0x09) with buffer ID 1 (0x01)
  write_raw_packet(FINGERPRINT_COMMANDPACKET, sizeof(cmd), cmd);

  // 3. Read command ACK
  uint8_t confirmationCode = 0xFF;
  if (!read_raw_ack(confirmationCode) || confirmationCode != FINGERPRINT_OK) {
    Serial.printf("[FP] Upload initialization failed, code: 0x%02X\n", confirmationCode);
    return false;
  }

  // 4. Send Packet 1 (first 256 bytes)
  write_raw_packet(FINGERPRINT_DATAPACKET, 256, templateData);
  delay(20); // allow sensor to process

  // 5. Send Packet 2 (second 256 bytes)
  write_raw_packet(FINGERPRINT_ENDDATAPACKET, 256, templateData + 256);

  // 6. Read transfer ACK
  if (!read_raw_ack(confirmationCode) || confirmationCode != FINGERPRINT_OK) {
    Serial.printf("[FP] Upload data transfer failed, code: 0x%02X\n", confirmationCode);
    return false;
  }

  // 7. Store template permanently
  uint8_t store_status = finger.storeModel(id);
  if (store_status != FINGERPRINT_OK) {
    Serial.printf("[FP] storeModel(%d) failed, code: 0x%02X\n", id, store_status);
    return false;
  }

  Serial.printf("[FP] Template uploaded and saved successfully to ID %d\n", id);
  return true;
}

bool fingerprint_restore_all_from_sd() {
  Serial.println("[FP] Starting biometric restore from SD card...");
  if (!sd_is_ready()) {
    Serial.println("[FP] SD card not ready, cannot restore");
    return false;
  }

  int restored_count = 0;
  int failed_count = 0;

  for (uint16_t id = 1; id < 512; id++) {
    char filename[32];
    snprintf(filename, sizeof(filename), "/fps/template_%d.dat", id);

    if (sd_file_exists(filename)) {
      uint8_t templateData[512];
      if (sd_read_binary(filename, templateData, 512)) {
        if (fingerprint_upload_template(id, templateData)) {
          Serial.printf("[FP] Restored template %d successfully\n", id);
          restored_count++;
        } else {
          Serial.printf("[FP] Failed to upload template %d\n", id);
          failed_count++;
        }
      } else {
        Serial.printf("[FP] Failed to read template file: %s\n", filename);
        failed_count++;
      }
    }
  }

  Serial.printf("[FP] Restore complete. Restored: %d, Failed: %d\n", restored_count, failed_count);
  return (restored_count > 0 && failed_count == 0);
}

bool fingerprint_is_slot_occupied(uint16_t id) {
  if (id == 0 || id > 511) return false;
  // loadModel returns FINGERPRINT_OK if a template exists at this slot
  uint8_t p = finger.loadModel(id);
  return (p == FINGERPRINT_OK);
}

void fingerprint_led_off() { finger.LEDcontrol(false); }

void fingerprint_led_on() { finger.LEDcontrol(true); }
