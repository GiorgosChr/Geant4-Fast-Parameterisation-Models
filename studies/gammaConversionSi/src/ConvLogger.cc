/// \file ConvLogger.cc
/// \brief Implementation of the ConvLogger class

#include "ConvLogger.hh"

#include "G4Threading.hh"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>

ConvLogger::ConvLogger(const G4String& aPath)
{
  const std::filesystem::path path(aPath.c_str());
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }

  fFile.open(path);
  if (!fFile) {
    // G4cout is not usable here: it is what this class is about to receive
    std::cerr << "ConvLogger: cannot write to '" << aPath << "', logging to the terminal only"
              << std::endl;
    return;
  }

  const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  fFile << "# gammaConversionSi run started " << std::ctime(&now);
}

ConvLogger::~ConvLogger()
{
  if (fFile.is_open()) fFile.close();
}

G4int ConvLogger::Write(const G4String& aMessage, std::ostream& aTerminal)
{
  const G4int threadId = G4Threading::G4GetThreadId();

  std::lock_guard<std::mutex> lock(fMutex);
  if (threadId >= 0) {
    // Same shape as the prefix G4MTcoutDestination puts on worker output
    aTerminal << "G4WT" << threadId << " > ";
    if (fFile.is_open()) fFile << "G4WT" << threadId << " > ";
  }
  aTerminal << aMessage << std::flush;
  if (fFile.is_open()) fFile << aMessage << std::flush;
  return 0;
}

G4int ConvLogger::ReceiveG4cout(const G4String& aMessage)
{
  return Write(aMessage, std::cout);
}

G4int ConvLogger::ReceiveG4cerr(const G4String& aMessage)
{
  return Write(aMessage, std::cerr);
}
