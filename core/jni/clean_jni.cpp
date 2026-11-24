// JNI bridge from Kotlin (Android and desktop JVM) to a manure robot's session: the C APIs in
// core/session_c.h and core/clean_c.h. Kotlin side: native/shared/src/jniMain/kotlin/.../CoreJni.kt.
// No logic here, only copying bytes and strings across.
#include <jni.h>

#include <cstdint>
#include <string>
#include <vector>

#include "clean_c.h"
#include "discovery_c.h"

namespace {

std::string str(JNIEnv* e, jstring s) {
  if (!s) return {};
  const char* c = e->GetStringUTFChars(s, nullptr);
  std::string r(c ? c : "");
  if (c) e->ReleaseStringUTFChars(s, c);
  return r;
}

session* S(jlong h) { return reinterpret_cast<session*>(h); }

jstring text(JNIEnv* e, session* s, int32_t (*fn)(session*, char*, int32_t)) {
  int32_t n = fn(s, nullptr, 0);
  std::vector<char> buf(static_cast<size_t>(n) + 1);
  fn(s, buf.data(), n + 1);
  return e->NewStringUTF(buf.data());
}

jbyteArray bytes(JNIEnv* e, const uint8_t* data, int32_t n) {
  jbyteArray a = e->NewByteArray(n);
  e->SetByteArrayRegion(a, 0, n, reinterpret_cast<const jbyte*>(data));
  return a;
}

}  // namespace

#define JNI_FN(name) JNIEXPORT JNICALL Java_com_eschouten_agrigate_clean_core_CoreJni_##name

extern "C" {

// 0 for a driver that is not in this build.
jlong JNI_FN(nativeOpen)(JNIEnv* e, jobject, jstring driver, jstring pin, jstring memory, jstring language) {
  session* s = session_open(str(e, driver).c_str(), "", "", str(e, pin).c_str());
  if (!s) return 0;
  clean_set_language(s, str(e, language).c_str());
  clean_remember(s, str(e, memory).c_str());
  return reinterpret_cast<jlong>(s);
}

void JNI_FN(nativeClose)(JNIEnv*, jobject, jlong h) { session_close(S(h)); }

void JNI_FN(nativeIncoming)(JNIEnv* e, jobject, jlong h, jbyteArray data) {
  jsize n = e->GetArrayLength(data);
  std::vector<uint8_t> b(static_cast<size_t>(n));
  e->GetByteArrayRegion(data, 0, n, reinterpret_cast<jbyte*>(b.data()));
  session_incoming(S(h), b.data(), n, 0);
}

// The next bytes to write, null when there are none.
jbyteArray JNI_FN(nativeOutgoing)(JNIEnv* e, jobject, jlong h) {
  std::vector<uint8_t> b(static_cast<size_t>(session_outgoing(S(h), nullptr, 0)));
  if (b.empty()) return nullptr;
  session_outgoing(S(h), b.data(), static_cast<int32_t>(b.size()));
  return bytes(e, b.data(), static_cast<int32_t>(b.size()));
}

void JNI_FN(nativeLinkDown)(JNIEnv*, jobject, jlong h) { session_link_down(S(h)); }
void JNI_FN(nativeWait)(JNIEnv*, jobject, jlong h, jint ms) { session_wait(S(h), ms); }
jint JNI_FN(nativeResult)(JNIEnv*, jobject, jlong h) { return session_result(S(h)); }

jint JNI_FN(nativeConnect)(JNIEnv*, jobject, jlong h) { return clean_connect(S(h)); }
jint JNI_FN(nativeStartRoute)(JNIEnv*, jobject, jlong h, jchar letter, jboolean dryRun) {
  return clean_start_route(S(h), static_cast<char>(letter), dryRun ? 1 : 0);
}
jint JNI_FN(nativeSetPlanning)(JNIEnv*, jobject, jlong h, jboolean on) { return clean_set_planning(S(h), on ? 1 : 0); }
jint JNI_FN(nativeStopRoute)(JNIEnv*, jobject, jlong h) { return clean_stop_route(S(h)); }
jint JNI_FN(nativeAnswerStop)(JNIEnv*, jobject, jlong h, jboolean finish) { return clean_answer_stop(S(h), finish ? 1 : 0); }
jint JNI_FN(nativeReadRoutes)(JNIEnv*, jobject, jlong h) { return clean_read_routes(S(h)); }
jint JNI_FN(nativeFinish)(JNIEnv*, jobject, jlong h) { return clean_finish(S(h)); }
jint JNI_FN(nativeInput)(JNIEnv* e, jobject, jlong h, jstring name, jboolean hold) {
  return clean_input(S(h), str(e, name).c_str(), hold ? 1 : 0);
}
void JNI_FN(nativeRelease)(JNIEnv*, jobject, jlong h) { clean_release(S(h)); }
void JNI_FN(nativeCancel)(JNIEnv*, jobject, jlong h) { clean_cancel(S(h)); }

jstring JNI_FN(nativeStatusJson)(JNIEnv* e, jobject, jlong h) { return text(e, S(h), clean_status_json); }
jstring JNI_FN(nativeRoutesJson)(JNIEnv* e, jobject, jlong h) { return text(e, S(h), clean_routes_json); }
jstring JNI_FN(nativeNextStep)(JNIEnv* e, jobject, jlong h) { return text(e, S(h), clean_next_step); }
jstring JNI_FN(nativeMemory)(JNIEnv* e, jobject, jlong h) { return text(e, S(h), clean_memory); }
jstring JNI_FN(nativeLastError)(JNIEnv* e, jobject, jlong h) { return text(e, S(h), session_last_error); }

jstring JNI_FN(nativeDriversJson)(JNIEnv* e, jobject) {
  std::vector<char> buf(static_cast<size_t>(session_drivers_json(nullptr, 0)) + 1);
  session_drivers_json(buf.data(), static_cast<int32_t>(buf.size()));
  return e->NewStringUTF(buf.data());
}

jbyteArray JNI_FN(nativeEncryptPin)(JNIEnv* e, jobject, jstring pin) {
  uint8_t buf[64];
  int32_t n = discovery_encrypt_pin(str(e, pin).c_str(), buf, sizeof buf);
  return bytes(e, buf, n > (int32_t)sizeof buf ? (int32_t)sizeof buf : n);
}

}  // extern "C"
