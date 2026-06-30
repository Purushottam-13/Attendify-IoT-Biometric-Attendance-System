/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 10
 *  Lecture History UI - Implementation
 * ============================================================
 */

#include "lecture_history_ui.h"
#include "../core/system_state.h"
#include "../hal/button_hal.h"
#include "../hal/display_hal.h"
#include "../hal/sd_hal.h"
#include "../security/teacher_auth.h"
#include <SD.h>
#include <vector>

// ============================================================
// STATE
// ============================================================
static std::vector<String> lectureFiles;
static int lectureSelection = 0;
static String currentTeacher = "";
static String currentLectureFile = "";

// Lecture detail data
static int detailStudentCount = 0;
static std::vector<String> detailStudents;
static int detailScrollPos = 0;

// ============================================================
// INITIALIZATION
// ============================================================
void lecture_history_init() {
  lectureFiles.clear();
  lectureSelection = 0;
  currentTeacher = "";
  currentLectureFile = "";
  detailStudentCount = 0;
  detailStudents.clear();
  detailScrollPos = 0;
  Serial.println("[LECTURE_HISTORY] Initialized");
}

// ============================================================
// LOAD LECTURES FROM SD
// ============================================================
void lecture_history_load(const String &teacherName) {
  lectureFiles = sd_list_teacher_files(teacherName);
  lectureSelection = 0;
  currentTeacher = teacherName;

  Serial.printf("[LECTURE_HISTORY] Loaded %d lectures for %s\n",
                lectureFiles.size(), teacherName.c_str());
}

// ============================================================
// LOAD LECTURE DETAIL (Students in a session)
// ============================================================
static void loadLectureDetail(const String &fileName) {
  detailStudents.clear();
  detailStudentCount = 0;
  detailScrollPos = 0;
  currentLectureFile = fileName;

  String safeTeacher = sd_sanitize_filename(currentTeacher);
  String path = "/attendance/" + safeTeacher + "/" + fileName;

  Serial.print("[LECTURE_HISTORY] Reading: ");
  Serial.println(path);

  File file = SD.open(path.c_str(), FILE_READ);
  if (!file) {
    Serial.println("[LECTURE_HISTORY] File empty or not found");
    return;
  }
  if (file.size() == 0) {
    Serial.println("[LECTURE_HISTORY] File empty");
    file.close();
    return;
  }

  int lineNum = 0;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();

    if (lineNum > 0 && line.length() > 5) { // Skip header
      // Parse CSV:
      // Timestamp,Date,Time,SessionID,Subject,Teacher,StudentID,DeviceID We
      // want StudentID (index 6)
      int commaCount = 0;
      int tokenStart = 0;
      String studentId = "";

      for (int i = 0; i < line.length(); i++) {
        if (line.charAt(i) == ',') {
          if (commaCount == 6) {
            studentId = line.substring(tokenStart, i);
            break;
          }
          commaCount++;
          tokenStart = i + 1;
        }
      }

      if (commaCount == 6) {
        studentId = line.substring(tokenStart);
      }

      if (studentId.length() > 0) {
        detailStudents.push_back(studentId);
      }
    }
    lineNum++;
  }
  file.close();

  detailStudentCount = detailStudents.size();
  Serial.printf("[LECTURE_HISTORY] Found %d students\n", detailStudentCount);
}

// ============================================================
// HANDLE LECTURE HISTORY
// ============================================================
void lecture_history_handle() {
  SystemState state = stateManager.getState();
  ButtonEvent event = button_poll();

  // --------------------------------------------------------
  // LECTURE LIST STATE
  // --------------------------------------------------------
  if (state == STATE_LECTURE_HISTORY) {
    display_clear();
    display_header("LECTURE HISTORY");

    if (lectureFiles.size() == 0) {
      display_message("No lectures found");
      display_message_at(4, "[BACK] Return");
      display_update();

      if (event == BTN_BACK) {
        stateManager.setState(STATE_SUBJECT_SELECT);
      }
      return;
    }

    // Show list with selection
    int startIdx = (lectureSelection / 4) * 4;
    for (int i = 0; i < 4 && (startIdx + i) < (int)lectureFiles.size(); i++) {
      String displayName = lectureFiles[startIdx + i];
      // Remove .csv extension for display
      if (displayName.endsWith(".csv")) {
        displayName = displayName.substring(0, displayName.length() - 4);
      }

      if (startIdx + i == lectureSelection) {
        displayName = "> " + displayName;
      } else {
        displayName = "  " + displayName;
      }
      display_message_at(2 + i, displayName.c_str());
    }

    char countBuf[20];
    snprintf(countBuf, 20, "%d/%d", lectureSelection + 1, lectureFiles.size());
    display_message_at(6, countBuf);
    display_update();

    if (event == BTN_UP && lectureSelection > 0) {
      lectureSelection--;
    } else if (event == BTN_DOWN &&
               lectureSelection < (int)lectureFiles.size() - 1) {
      lectureSelection++;
    } else if (event == BTN_SELECT) {
      loadLectureDetail(lectureFiles[lectureSelection]);
      stateManager.setState(STATE_LECTURE_DETAIL);
    } else if (event == BTN_BACK) {
      stateManager.setState(STATE_SUBJECT_SELECT);
    }
  }

  // --------------------------------------------------------
  // LECTURE DETAIL STATE
  // --------------------------------------------------------
  else if (state == STATE_LECTURE_DETAIL) {
    display_clear();

    // Show lecture name as header
    String header = currentLectureFile;
    if (header.endsWith(".csv")) {
      header = header.substring(0, header.length() - 4);
    }
    if (header.length() > 16) {
      header = header.substring(0, 16);
    }
    display_header(header.c_str());

    char countBuf[30];
    snprintf(countBuf, 30, "Students: %d", detailStudentCount);
    display_message(countBuf);

    if (detailStudentCount == 0) {
      display_message_at(3, "No records");
    } else {
      // Show student IDs (scrollable)
      int startIdx = detailScrollPos;
      for (int i = 0; i < 3 && (startIdx + i) < detailStudentCount; i++) {
        char buf[20];
        snprintf(buf, 20, "ID: %s", detailStudents[startIdx + i].c_str());
        display_message_at(3 + i, buf);
      }
    }

    display_message_at(6, "[BACK] Return");
    display_update();

    if (event == BTN_UP && detailScrollPos > 0) {
      detailScrollPos--;
    } else if (event == BTN_DOWN && detailScrollPos < detailStudentCount - 3) {
      detailScrollPos++;
    } else if (event == BTN_BACK) {
      stateManager.setState(STATE_LECTURE_HISTORY);
    }
  }
}
