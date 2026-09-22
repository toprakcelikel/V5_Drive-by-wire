#pragma once

class Vehicle;

namespace SerialTestMode {

void begin();
void readCommands(Vehicle &vehicle);
void report(const char *format, ...);
void flushReports();

}