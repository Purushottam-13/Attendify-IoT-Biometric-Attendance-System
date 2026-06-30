#ifndef FP_MAPPING_HAL_H
#define FP_MAPPING_HAL_H

#include <Arduino.h>
#include <vector>

struct FPMapping {
  uint16_t internalId;
  uint32_t rollNumber;
  String studentName;
};

const uint16_t STUDENT_ID_START = 50;
const uint16_t STUDENT_ID_END = 450;

// Initialize mapping from SD card
void fp_map_init();

// Get the roll number for a scanned internal ID
uint32_t fp_map_get_roll(uint16_t internalId);

// Get the student name for a scanned internal ID
String fp_map_get_name(uint16_t internalId);

// Get the internal ID for a roll number (if already enrolled)
uint16_t fp_map_get_internal_id(uint32_t rollNumber);

// Get the next available internal ID for a new enrollment
uint16_t fp_map_get_next_free_id();

// Save a new mapping to SD and memory
bool fp_map_register(uint16_t internalId, uint32_t rollNumber,
                     const String &studentName = "");

// Reset all student mappings and fingerprints
void fp_map_reset();

#endif
