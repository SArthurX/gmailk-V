#ifndef APP_OPTIONS_H
#define APP_OPTIONS_H

#include <string>

extern const char *const kDefaultRpiUrl;

struct AppOptions {
  std::string scrfdModelPath;
  std::string arcfaceCvimodelPath;
  bool enableOLED = false;
  bool enableRemoteDB = false;
  std::string rpiUrl;
};

void PrintUsage(const char *programName);
bool ParseArgs(int argc, char *argv[], AppOptions *opts);

#endif  // APP_OPTIONS_H
