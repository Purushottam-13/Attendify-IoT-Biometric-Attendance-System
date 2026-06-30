#pragma once
#include <Arduino.h>

bool admin_is_authorized();
bool admin_authorize(const String& adminName);
bool admin_verify();
