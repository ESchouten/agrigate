// The C API of "clean" (core/clean_c.h): each call goes to the session's robot.
#include "clean_c.h"

#include "Session.h"

using agrigate::copyOut;

extern "C" {

int32_t clean_connect(session* s) { return s->robot ? s->robot->connect() : SESSION_UNSUPPORTED; }
int32_t clean_start_route(session* s, char letter, int32_t dry_run) { return s->robot ? s->robot->startRoute(letter, dry_run != 0) : SESSION_UNSUPPORTED; }
int32_t clean_stop_route(session* s) { return s->robot ? s->robot->stopRoute() : SESSION_UNSUPPORTED; }
int32_t clean_answer_stop(session* s, int32_t finish) { return s->robot ? s->robot->answerStop(finish != 0) : SESSION_UNSUPPORTED; }
int32_t clean_set_planning(session* s, int32_t on) { return s->robot ? s->robot->setPlanning(on != 0) : SESSION_UNSUPPORTED; }
int32_t clean_read_routes(session* s) { return s->robot ? s->robot->readRoutes() : SESSION_UNSUPPORTED; }
int32_t clean_finish(session* s) { return s->robot ? s->robot->finish() : SESSION_UNSUPPORTED; }
int32_t clean_input(session* s, const char* name, int32_t hold) { return s->robot ? s->robot->input(name ? name : "", hold != 0) : SESSION_UNSUPPORTED; }

void clean_release(session* s) {
  if (s->robot) s->robot->release();
}

void clean_cancel(session* s) {
  if (s->robot) s->robot->cancel();
}

void clean_set_language(session* s, const char* language) {
  if (s->robot) s->robot->language(language ? language : "");
}

void clean_remember(session* s, const char* memory) {
  if (s->robot) s->robot->remember(memory ? memory : "");
}

int32_t clean_status_json(session* s, char* buf, int32_t size) { return copyOut(s->robot ? s->robot->status() : "{}", buf, size); }
int32_t clean_routes_json(session* s, char* buf, int32_t size) { return copyOut(s->robot ? s->robot->routes() : "{}", buf, size); }

int32_t clean_next_step(session* s, char* buf, int32_t size) {
  if (s->robot && s->step.empty()) s->step = s->robot->nextStep();
  const std::string step = s->step;
  if (buf && size > (int32_t)step.size()) s->step.clear();   // given: the next call gives the next step
  return copyOut(step, buf, size);
}

int32_t clean_memory(session* s, char* buf, int32_t size) { return copyOut(s->robot ? s->robot->memory() : "", buf, size); }

}  // extern "C"
