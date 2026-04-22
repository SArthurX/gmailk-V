#include "app_options.h"

#include <iostream>
#include <string>

const char *const kDefaultRpiUrl = "http://192.168.42.1:3000";

void PrintUsage(const char *programName) {
  std::cout << "\nUsage: " << programName
            << " SCRFDFACE_MODEL_PATH [ARCFACE_CVIMODEL] [--oled] [--rpi [url]]\n\n"
            << "\tSCRFDFACE_MODEL_PATH, path to scrfdface model.\n"
            << "\tARCFACE_CVIMODEL (optional), path to ArcFace .cvimodel file (TPU).\n"
            << "\t--oled, optional flag to enable OLED display (I2C-2).\n"
            << "\t--rpi [url], optional flag to enable remote database.\n"
            << "\t           default url: " << kDefaultRpiUrl << "\n"
            << "\nExample:\n"
            << "\t" << programName << " models/scrfd.cvimodel\n"
            << "\t" << programName
            << " models/scrfd.cvimodel models/arcface.cvimodel --oled\n"
            << "\t" << programName << " models/scrfd.cvimodel --rpi\n"
            << "\t" << programName
            << " models/scrfd.cvimodel --rpi http://192.168.42.1:3000\n"
            << std::endl;
}

bool ParseArgs(int argc, char *argv[], AppOptions *opts) {
  if (!opts || argc < 2)
    return false;

  opts->scrfdModelPath = argv[1];

  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];

    if (arg == "--oled") {
      opts->enableOLED = true;
      continue;
    }

    if (arg == "--rpi") {
      opts->enableRemoteDB = true;
      if (i + 1 < argc && argv[i + 1][0] != '-')
        opts->rpiUrl = argv[++i];
      else
        opts->rpiUrl = kDefaultRpiUrl;
      continue;
    }

    if (!arg.empty() && arg[0] == '-') {
      std::cerr << "Unknown option: " << arg << std::endl;
      return false;
    }

    if (opts->arcfaceCvimodelPath.empty()) {
      opts->arcfaceCvimodelPath = arg;
      continue;
    }

    std::cerr << "Unexpected positional argument: " << arg << std::endl;
    return false;
  }

  if (opts->enableRemoteDB && opts->rpiUrl.empty())
    opts->rpiUrl = kDefaultRpiUrl;

  return true;
}
