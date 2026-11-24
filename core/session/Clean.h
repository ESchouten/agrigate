// What a driver that serves "clean" (core/clean_c.h) makes for a session: the robot behind it.
// A command starts here and runs on as the session goes on; the robot says when it is done. A
// robot is a conversation with its own timing (a key, then the screen that follows), so it also
// takes the link's bytes itself and may run on a thread of its own: every call here may then come
// from the host's thread while a command runs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace agrigate {
namespace clean {

class Robot {
 public:
  virtual ~Robot() = default;

  // ---- commands: SESSION_OK (started) or SESSION_IN_PROGRESS (another one runs)
  virtual int connect() = 0;
  virtual int startRoute(char letter, bool dryRun) = 0;
  virtual int stopRoute() = 0;
  virtual int answerStop(bool finish) = 0;
  virtual int setPlanning(bool on) = 0;
  virtual int readRoutes() = 0;
  virtual int finish() = 0;
  /** The robot's own: an input by the driver's name for it, tapped or held until release(). */
  virtual int input(const std::string& name, bool hold) = 0;
  virtual void release() = 0;
  virtual void cancel() = 0;

  // ---- how it went and what there is to know
  /** SESSION_IN_PROGRESS while a command runs, then its result. */
  virtual int result() = 0;
  /** What went wrong with the last command, in the language given; "" when it went well. */
  virtual std::string error() = 0;
  virtual void language(const std::string& code) = 0;
  /** JSON, as clean_c.h describes them. */
  virtual std::string status() = 0;
  virtual std::string routes() = 0;
  /** The next step of the running command that was not given yet, "" when there is none. */
  virtual std::string nextStep() = 0;
  /** What to keep for this robot until the next session, and getting it back. */
  virtual std::string memory() = 0;
  virtual void remember(const std::string& memory) = 0;

  // ---- the link's bytes
  virtual void incoming(const uint8_t* data, size_t len) = 0;
  /** The next bytes to write (taken off), false when there are none. */
  virtual bool outgoing(std::string& bytes) = 0;
  virtual void linkDown() = 0;
  /** Returns when there is something new for the host since the last wait, or after [ms]. */
  virtual void wait(int ms) = 0;
};

}  // namespace clean
}  // namespace agrigate
