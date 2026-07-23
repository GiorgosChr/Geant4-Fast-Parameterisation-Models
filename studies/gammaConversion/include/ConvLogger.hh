/// \file ConvLogger.hh
/// \brief Definition of the ConvLogger class

#ifndef CONV_LOGGER_HH
#define CONV_LOGGER_HH

#include "G4String.hh"
#include "G4Types.hh"
#include "G4UIsession.hh"

#include <fstream>
#include <mutex>

/**
 * @brief Tees everything Geant4 prints to a log file, keeping the terminal.
 *
 * One instance is shared by every thread: it is installed on the master with
 * G4UImanager::SetCoutDestination, and again on each worker from
 * ConvActionInitialization::Build(). Installing it on the workers is what makes
 * the log complete -- a worker's default G4MTcoutDestination writes its
 * "G4WT0 > " lines straight to the terminal and never reaches the master
 * destination, so worker-side G4Exception warnings would otherwise be visible
 * on screen but missing from the log.
 *
 * Because the workers share the one file stream, every write is serialised and
 * carries the thread prefix that G4MTcoutDestination would have added.
 *
 * A material scan runs one process per material (see gammaConversion.cc), so a
 * single log per process -- opened here at construction -- is already one log
 * per material.
 */
class ConvLogger : public G4UIsession
{
  public:
    /// Opens aPath for writing, creating its directory if needed.
    explicit ConvLogger(const G4String& aPath);
    ~ConvLogger() override;

    G4int ReceiveG4cout(const G4String& aMessage) override;
    G4int ReceiveG4cerr(const G4String& aMessage) override;

  private:
    /// Writes to the terminal and the log under the lock, prefixing the
    /// thread id when called from a worker.
    G4int Write(const G4String& aMessage, std::ostream& aTerminal);

    std::ofstream fFile;
    std::mutex fMutex;
};

#endif /* CONV_LOGGER_HH */
