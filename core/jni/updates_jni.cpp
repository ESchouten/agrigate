// Desktop updates through Velopack's C library (linked in, see CMakeLists.txt), for
// app/DesktopUpdates.kt. The app downloads and verifies the signed feed and the package itself
// and hands Velopack a folder holding only those: Velopack applies it once the app has exited
// and starts the new version.
#include <jni.h>

#include <string>
#include <vector>

#include "Velopack.h"

namespace {
std::string str(JNIEnv* e, jstring s) {
  if (!s) return "";
  const char* c = e->GetStringUTFChars(s, nullptr);
  std::string out(c);
  e->ReleaseStringUTFChars(s, c);
  return out;
}

std::string lastError(const char* what) {
  char buf[512] = {0};
  vpkc_get_last_error(buf, sizeof buf);
  return std::string(what) + (buf[0] ? std::string(": ") + buf : std::string());
}
}  // namespace

extern "C" {

// First thing in main: Velopack's install, update and uninstall hooks run here and exit.
JNIEXPORT void JNICALL Java_nl_eschouten_scraper_app_DesktopUpdates_nativeAppRun(JNIEnv* e, jclass, jobjectArray args) {
  std::vector<std::string> keep;
  jsize n = args ? e->GetArrayLength(args) : 0;
  for (jsize i = 0; i < n; i++) keep.push_back(str(e, (jstring)e->GetObjectArrayElement(args, i)));
  std::vector<char*> ptrs;
  for (auto& s : keep) ptrs.push_back(&s[0]);
  vpkc_app_set_args(ptrs.empty() ? nullptr : ptrs.data(), ptrs.size());
  vpkc_app_run(nullptr);
}

// The installed version ("" when not installed by Velopack, e.g. run from a build).
JNIEXPORT jstring JNICALL Java_nl_eschouten_scraper_app_DesktopUpdates_nativeInstalledVersion(JNIEnv* e, jclass) {
  vpkc_update_manager_t* m = nullptr;
  std::string v;
  // a manager needs a source; this one is never read, only the installed copy is looked at
  if (vpkc_new_update_manager("https://localhost/none", nullptr, nullptr, &m)) {
    char buf[64] = {0};
    vpkc_get_current_version(m, buf, sizeof buf);
    v = buf;
    vpkc_free_update_manager(m);
  }
  return e->NewStringUTF(v.c_str());
}

// dir: a folder with a verified feed (releases.<channel>.json) and its package. Prepares the
// update to be applied once this process exits, then restarts the app. "" on success, else why.
JNIEXPORT jstring JNICALL Java_nl_eschouten_scraper_app_DesktopUpdates_nativeApply(JNIEnv* e, jclass, jstring dir) {
  std::string path = str(e, dir), err;
  vpkc_update_source_t* src = vpkc_new_source_file(path.c_str());
  vpkc_update_manager_t* m = nullptr;
  vpkc_update_info_t* info = nullptr;
  if (!src || !vpkc_new_update_manager_with_source(src, nullptr, nullptr, &m)) {
    err = lastError("not an installed copy");
  } else if (vpkc_check_for_updates(m, &info) != UPDATE_AVAILABLE || !info) {
    err = lastError("no newer version in the feed");
  } else if (!vpkc_download_updates(m, info, nullptr, nullptr)) {
    err = lastError("package not taken");
  } else if (!vpkc_wait_exit_then_apply_updates(m, info->TargetFullRelease, false, true, nullptr, 0)) {
    err = lastError("update not started");
  }
  if (info) vpkc_free_update_info(info);
  if (m) vpkc_free_update_manager(m);
  if (src) vpkc_free_source(src);
  return e->NewStringUTF(err.c_str());
}

}  // extern "C"
