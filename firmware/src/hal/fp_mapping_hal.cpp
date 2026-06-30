#include "fp_mapping_hal.h"
#include "fingerprint_hal.h"
#include "sd_hal.h"
#include <SD.h>

static std::vector<FPMapping> mappings;
const char *MAP_FILE = "/attendance/fp_map.csv";

static String csv_escape_field(const String &value) {
  String escaped = value;
  escaped.replace("\"", "\"\"");

  if (escaped.indexOf(',') >= 0 || escaped.indexOf('"') >= 0 ||
      escaped.indexOf('\n') >= 0 || escaped.indexOf('\r') >= 0) {
    escaped = "\"" + escaped + "\"";
  }

  return escaped;
}

static std::vector<String> parse_csv_line(const String &line) {
  std::vector<String> fields;
  String field = "";
  bool inQuotes = false;

  for (int i = 0; i < (int)line.length(); i++) {
    char c = line.charAt(i);
    if (c == '"') {
      if (inQuotes && i + 1 < (int)line.length() && line.charAt(i + 1) == '"') {
        field += '"';
        i++;
      } else {
        inQuotes = !inQuotes;
      }
    } else if (c == ',' && !inQuotes) {
      field.trim();
      fields.push_back(field);
      field = "";
    } else {
      field += c;
    }
  }

  field.trim();
  fields.push_back(field);
  return fields;
}

static bool fp_map_save_all() {
  if (SD.exists(MAP_FILE)) {
    SD.remove(MAP_FILE);
  }

  File file = SD.open(MAP_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("[FP-MAP] ERROR: Failed to write mapping file!");
    return false;
  }

  for (const auto &m : mappings) {
    file.print(m.internalId);
    file.print(",");
    file.print(m.rollNumber);
    file.print(",");
    file.println(csv_escape_field(m.studentName));
  }

  file.close();
  return true;
}

void fp_map_init() {
  mappings.clear();
  if (!SD.exists(MAP_FILE)) {
    Serial.println("[FP-MAP] No mapping file found, starting fresh.");
    return;
  }

  File file = SD.open(MAP_FILE, FILE_READ);
  if (!file)
    return;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
      continue;

    std::vector<String> fields = parse_csv_line(line);
    if (fields.size() >= 2) {
      FPMapping m;
      m.internalId = fields[0].toInt();
      m.rollNumber = fields[1].toInt();
      m.studentName = fields.size() >= 3 ? fields[2] : "";
      if (m.internalId > 0 && m.rollNumber > 0) {
        mappings.push_back(m);
      }
    }
  }
  file.close();
  Serial.printf("[FP-MAP] Loaded %d mappings from SD\n", mappings.size());
}

uint32_t fp_map_get_roll(uint16_t internalId) {
  for (const auto &m : mappings) {
    if (m.internalId == internalId)
      return m.rollNumber;
  }
  return 0; // Not found
}

String fp_map_get_name(uint16_t internalId) {
  for (const auto &m : mappings) {
    if (m.internalId == internalId)
      return m.studentName;
  }
  return "";
}

uint16_t fp_map_get_internal_id(uint32_t rollNumber) {
  for (const auto &m : mappings) {
    if (m.rollNumber == rollNumber)
      return m.internalId;
  }
  return 0; // Not found
}

uint16_t fp_map_get_next_free_id() {
  // Scan the full student ID range and find a slot that is free in BOTH
  // the in-memory mapping table AND the actual fingerprint sensor hardware.
  // This prevents ID collisions when the mapping file is lost/corrupted.
  for (uint16_t id = STUDENT_ID_START; id <= STUDENT_ID_END; id++) {
    // Check 1: Is this ID already in our mapping table?
    bool inMapping = false;
    for (const auto &m : mappings) {
      if (m.internalId == id) {
        inMapping = true;
        break;
      }
    }
    if (inMapping) continue;

    // Check 2: Is this slot occupied on the actual sensor hardware?
    // This is the critical safety net — even if our mapping file was lost,
    // we won't overwrite an existing fingerprint on the sensor.
    if (fingerprint_is_slot_occupied(id)) {
      Serial.printf("[FP-MAP] WARNING: Slot %d occupied on sensor but not in mapping! Skipping.\n", id);
      continue;
    }

    return id;
  }

  Serial.println("[FP-MAP] ERROR: Student sensor capacity full!");
  return 0;
}

bool fp_map_register(uint16_t internalId, uint32_t rollNumber,
                     const String &studentName) {
  // Handle re-enrollment: update only if BOTH keys match (same student)
  // or if the roll number matches (student changing finger/slot).
  // Remove any stale conflicting entries to prevent data corruption.
  bool updated = false;

  // First pass: find and handle conflicts
  for (int i = (int)mappings.size() - 1; i >= 0; i--) {
    auto &m = mappings[i];

    // Exact match (same student, same slot) — just update
    if (m.internalId == internalId && m.rollNumber == rollNumber) {
      m.studentName = studentName;
      updated = true;
      break;
    }

    // Same roll number, different slot — student is re-enrolling with a new finger
    if (m.rollNumber == rollNumber && m.internalId != internalId) {
      Serial.printf("[FP-MAP] Re-enroll: Roll %u moved from Slot %d to Slot %d\n",
                    rollNumber, m.internalId, internalId);
      m.internalId = internalId;
      m.studentName = studentName;
      updated = true;
      break;
    }

    // Same internal ID, different roll — CONFLICT! Stale mapping from lost CSV.
    // Remove the stale entry so the new student can take this slot.
    if (m.internalId == internalId && m.rollNumber != rollNumber) {
      Serial.printf("[FP-MAP] WARNING: Slot %d was mapped to Roll %u, now reassigning to Roll %u\n",
                    internalId, m.rollNumber, rollNumber);
      mappings.erase(mappings.begin() + i);
      // Don't break — continue to check for other conflicts, then add new entry below
    }
  }

  if (!updated) {
    FPMapping m = {internalId, rollNumber, studentName};
    mappings.push_back(m);
  }

  if (fp_map_save_all()) {
    Serial.printf("[FP-MAP] Registered: Slot %d -> Roll %u\n", internalId,
                  rollNumber);
    return true;
  }

  return false;
}


void fp_map_reset() {
  Serial.println("[FP-MAP] Resetting all student data...");

  // 1. Delete fingerprints from sensor in student range
  for (uint16_t id = STUDENT_ID_START; id <= STUDENT_ID_END; id++) {
    fingerprint_delete(id);
  }

  // 2. Clear memory mapping
  mappings.clear();

  // 3. Delete mapping file from SD
  if (SD.exists(MAP_FILE)) {
    SD.remove(MAP_FILE);
    Serial.println("[FP-MAP] Deleted mapping file from SD.");
  }

  Serial.println("[FP-MAP] Reset complete.");
}
