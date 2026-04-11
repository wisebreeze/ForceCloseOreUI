#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <unordered_map>

#include "api/memory/Hook.h"
#include <cstdio>

namespace fs = std::filesystem;
#if _WIN32

#include <shlobj.h>
#include <string>
#include <vector>
#include <windows.h>

#endif

#if __arm__
#include <unistd.h>
extern "C" int __wrap_getpagesize() { return sysconf(_SC_PAGESIZE); }

#endif

#if __arm__ || __aarch64__
#include "jni.h"
#include <android/log.h>

JNIEnv *env = nullptr;

#define LOGI(...)                                                              \
  __android_log_print(ANDROID_LOG_INFO, "LeviLogger", __VA_ARGS__)

// 安全显示 Toast（在主线程中执行）
void showToastSafe(const std::string& message) {
  if (!env) return;
  
  // 获取全局 Context
  jclass activity_thread = env->FindClass("android/app/ActivityThread");
  if (!activity_thread) return;
  
  jmethodID current_activity_thread =
      env->GetStaticMethodID(activity_thread, "currentActivityThread",
                             "()Landroid/app/ActivityThread;");
  if (!current_activity_thread) {
    env->DeleteLocalRef(activity_thread);
    return;
  }
  
  jobject at =
      env->CallStaticObjectMethod(activity_thread, current_activity_thread);
  if (env->ExceptionCheck()) env->ExceptionClear();
  
  jmethodID get_application = env->GetMethodID(
      activity_thread, "getApplication", "()Landroid/app/Application;");
  if (!get_application) {
    env->DeleteLocalRef(activity_thread);
    if (at) env->DeleteLocalRef(at);
    return;
  }
  
  jobject context = env->CallObjectMethod(at, get_application);
  if (env->ExceptionCheck()) env->ExceptionClear();
  
  if (!context) {
    env->DeleteLocalRef(activity_thread);
    if (at) env->DeleteLocalRef(at);
    return;
  }
  
  // 获取主线程 Looper
  jclass looper_class = env->FindClass("android/os/Looper");
  if (!looper_class) {
    env->DeleteLocalRef(context);
    env->DeleteLocalRef(activity_thread);
    if (at) env->DeleteLocalRef(at);
    return;
  }
  
  jmethodID get_main_looper = env->GetStaticMethodID(
      looper_class, "getMainLooper", "()Landroid/os/Looper;");
  if (!get_main_looper) {
    env->DeleteLocalRef(looper_class);
    env->DeleteLocalRef(context);
    env->DeleteLocalRef(activity_thread);
    if (at) env->DeleteLocalRef(at);
    return;
  }
  
  jobject main_looper = env->CallStaticObjectMethod(looper_class, get_main_looper);
  if (env->ExceptionCheck()) env->ExceptionClear();
  
  // 创建 Toast
  jclass toast_class = env->FindClass("android/widget/Toast");
  if (!toast_class) {
    env->DeleteLocalRef(looper_class);
    if (main_looper) env->DeleteLocalRef(main_looper);
    env->DeleteLocalRef(context);
    env->DeleteLocalRef(activity_thread);
    if (at) env->DeleteLocalRef(at);
    return;
  }
  
  jmethodID make_text = env->GetStaticMethodID(
      toast_class, "makeText",
      "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;");
  if (!make_text) {
    env->DeleteLocalRef(toast_class);
    env->DeleteLocalRef(looper_class);
    if (main_looper) env->DeleteLocalRef(main_looper);
    env->DeleteLocalRef(context);
    env->DeleteLocalRef(activity_thread);
    if (at) env->DeleteLocalRef(at);
    return;
  }
  
  jstring j_msg = env->NewStringUTF(message.c_str());
  jobject toast = env->CallStaticObjectMethod(
      toast_class, make_text, context, j_msg, 1); // 1 = LENGTH_LONG
  
  if (toast && !env->ExceptionCheck()) {
    jmethodID show = env->GetMethodID(toast_class, "show", "()V");
    if (show) {
      env->CallVoidMethod(toast, show);
    }
    env->DeleteLocalRef(toast);
  }
  
  env->DeleteLocalRef(j_msg);
  env->DeleteLocalRef(toast_class);
  env->DeleteLocalRef(looper_class);
  if (main_looper) env->DeleteLocalRef(main_looper);
  env->DeleteLocalRef(context);
  env->DeleteLocalRef(activity_thread);
  if (at) env->DeleteLocalRef(at);
}

// 安全显示日志（不依赖 JNI）
void logMessage(const std::string& msg) {
  LOGI("%s", msg.c_str());
}

// 获取包名（安全版本）
std::string getPackageNameSafe(JNIEnv* jenv, jobject context) {
  if (!jenv || !context) return "";
  
  jclass context_class = jenv->GetObjectClass(context);
  if (!context_class) return "";
  
  jmethodID get_pkg_name =
      jenv->GetMethodID(context_class, "getPackageName", "()Ljava/lang/String;");
  if (!get_pkg_name) {
    jenv->DeleteLocalRef(context_class);
    return "";
  }
  
  auto jstr = (jstring)jenv->CallObjectMethod(context, get_pkg_name);
  if (jenv->ExceptionCheck()) {
    jenv->ExceptionClear();
    jenv->DeleteLocalRef(context_class);
    return "";
  }
  
  const char* cstr = jenv->GetStringUTFChars(jstr, nullptr);
  std::string result(cstr);
  jenv->ReleaseStringUTFChars(jstr, cstr);
  jenv->DeleteLocalRef(jstr);
  jenv->DeleteLocalRef(context_class);
  
  // 转换为小写
  for (auto& c : result) c = tolower(c);
  return result;
}

std::string getInternalStoragePath(JNIEnv* jenv) {
  if (!jenv) return "";
  
  jclass env_class = jenv->FindClass("android/os/Environment");
  if (!env_class) return "";
  
  jmethodID get_storage_dir = jenv->GetStaticMethodID(
      env_class, "getExternalStorageDirectory", "()Ljava/io/File;");
  if (!get_storage_dir) {
    jenv->DeleteLocalRef(env_class);
    return "";
  }
  
  jobject storage_dir = jenv->CallStaticObjectMethod(env_class, get_storage_dir);
  if (jenv->ExceptionCheck()) {
    jenv->ExceptionClear();
    jenv->DeleteLocalRef(env_class);
    return "";
  }
  
  // 获取绝对路径
  jclass file_class = jenv->GetObjectClass(storage_dir);
  jmethodID get_abs_path =
      jenv->GetMethodID(file_class, "getAbsolutePath", "()Ljava/lang/String;");
  if (!get_abs_path) {
    jenv->DeleteLocalRef(file_class);
    jenv->DeleteLocalRef(env_class);
    if (storage_dir) jenv->DeleteLocalRef(storage_dir);
    return "";
  }
  
  auto jstr = (jstring)jenv->CallObjectMethod(storage_dir, get_abs_path);
  if (jenv->ExceptionCheck()) {
    jenv->ExceptionClear();
    jenv->DeleteLocalRef(file_class);
    jenv->DeleteLocalRef(env_class);
    if (storage_dir) jenv->DeleteLocalRef(storage_dir);
    return "";
  }
  
  const char* cstr = jenv->GetStringUTFChars(jstr, nullptr);
  std::string result(cstr);
  jenv->ReleaseStringUTFChars(jstr, cstr);
  
  jenv->DeleteLocalRef(jstr);
  jenv->DeleteLocalRef(file_class);
  jenv->DeleteLocalRef(env_class);
  if (storage_dir) jenv->DeleteLocalRef(storage_dir);
  
  return result;
}

std::string GetModsFilesPath(JNIEnv* jenv) {
  if (!jenv) {
    logMessage("GetModsFilesPath: JNIEnv is null");
    return "";
  }
  
  // 获取全局 Context
  jclass activity_thread = jenv->FindClass("android/app/ActivityThread");
  if (!activity_thread) {
    logMessage("GetModsFilesPath: Failed to find ActivityThread class");
    return "";
  }
  
  jmethodID current_activity_thread =
      jenv->GetStaticMethodID(activity_thread, "currentActivityThread",
                              "()Landroid/app/ActivityThread;");
  if (!current_activity_thread) {
    logMessage("GetModsFilesPath: Failed to get currentActivityThread method");
    jenv->DeleteLocalRef(activity_thread);
    return "";
  }
  
  jobject at =
      jenv->CallStaticObjectMethod(activity_thread, current_activity_thread);
  if (jenv->ExceptionCheck()) {
    jenv->ExceptionClear();
    logMessage("GetModsFilesPath: Exception getting ActivityThread");
    jenv->DeleteLocalRef(activity_thread);
    return "";
  }
  
  jmethodID get_application = jenv->GetMethodID(
      activity_thread, "getApplication", "()Landroid/app/Application;");
  if (!get_application) {
    logMessage("GetModsFilesPath: Failed to get getApplication method");
    jenv->DeleteLocalRef(activity_thread);
    if (at) jenv->DeleteLocalRef(at);
    return "";
  }
  
  jobject context = jenv->CallObjectMethod(at, get_application);
  if (jenv->ExceptionCheck()) {
    jenv->ExceptionClear();
    logMessage("GetModsFilesPath: Exception getting application context");
    jenv->DeleteLocalRef(activity_thread);
    if (at) jenv->DeleteLocalRef(at);
    return "";
  }
  
  if (!context) {
    logMessage("GetModsFilesPath: Failed to get application context");
    jenv->DeleteLocalRef(activity_thread);
    if (at) jenv->DeleteLocalRef(at);
    return "";
  }
  
  auto package_name = getPackageNameSafe(jenv, context);
  if (package_name.empty()) {
    logMessage("GetModsFilesPath: Failed to get package name");
    jenv->DeleteLocalRef(context);
    jenv->DeleteLocalRef(activity_thread);
    if (at) jenv->DeleteLocalRef(at);
    return "";
  }
  
  std::string internal_storage = getInternalStoragePath(jenv);
  if (internal_storage.empty()) {
    logMessage("GetModsFilesPath: Failed to get internal storage path");
    jenv->DeleteLocalRef(context);
    jenv->DeleteLocalRef(activity_thread);
    if (at) jenv->DeleteLocalRef(at);
    return "";
  }
  
  std::string path = (fs::path(internal_storage) / "Android" / "data" /
                      package_name / "mods");
  
  logMessage("GetModsFilesPath: Using path: " + path);
  
  jenv->DeleteLocalRef(context);
  jenv->DeleteLocalRef(activity_thread);
  if (at) jenv->DeleteLocalRef(at);
  
  return path;
}

jobject getGlobalContext(JNIEnv *jenv) {
  if (!jenv) return nullptr;
  
  jclass activity_thread = jenv->FindClass("android/app/ActivityThread");
  if (!activity_thread) return nullptr;
  
  jmethodID current_activity_thread =
      jenv->GetStaticMethodID(activity_thread, "currentActivityThread",
                             "()Landroid/app/ActivityThread;");
  if (!current_activity_thread) {
    jenv->DeleteLocalRef(activity_thread);
    return nullptr;
  }
  
  jobject at =
      jenv->CallStaticObjectMethod(activity_thread, current_activity_thread);
  if (jenv->ExceptionCheck()) {
    jenv->ExceptionClear();
    jenv->DeleteLocalRef(activity_thread);
    return nullptr;
  }
  
  jmethodID get_application = jenv->GetMethodID(
      activity_thread, "getApplication", "()Landroid/app/Application;");
  if (!get_application) {
    jenv->DeleteLocalRef(activity_thread);
    if (at) jenv->DeleteLocalRef(at);
    return nullptr;
  }
  
  jobject context = jenv->CallObjectMethod(at, get_application);
  if (jenv->ExceptionCheck()) {
    jenv->ExceptionClear();
    jenv->DeleteLocalRef(activity_thread);
    if (at) jenv->DeleteLocalRef(at);
    return nullptr;
  }
  
  jenv->DeleteLocalRef(activity_thread);
  if (at) jenv->DeleteLocalRef(at);
  
  return context;
}

SKY_AUTO_STATIC_HOOK(
    Hook1, memory::HookPriority::Normal,
    std::initializer_list<const char *>(
        {"? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? "
         "91 ? ? ? D5 ? ? ? F9 ? ? ? F8 ? ? ? 39 ? ? ? 34 ? ? ? 12"}),
    int, void *_this, JavaVM *vm) {

  vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_4);
  if (env) {
    logMessage("Hook1 triggered, JNIEnv obtained");
  }
  return origin(_this, vm);
}

#endif

class OreUIConfig {
public:
  void *mUnknown1;
  void *mUnknown2;
  std::function<bool()> mUnknown3;
  std::function<bool()> mUnknown4;
};

class OreUi {
public:
  std::unordered_map<std::string, OreUIConfig> mConfigs;
};

// clang-format off
#if __arm__
#define OREUI_PATTERN 
   {""}

#elif __aarch64__
#define OREUI_PATTERN                                                                     \
     std::initializer_list<const char *>({                                                \
      "? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 ? ? ? D1 ? ? ? D5 FA 03 00 AA F5 03 07 AA", \
      "? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 ? ? ? D1 ? ? ? D5 FB 03 00 AA F5 03 07 AA", \
      "? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? F9 ? ? ? D5 FB 03 00 AA ? ? ? F9 F5 03 07 AA", \
      "? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 ? ? ? D1 ? ? ? D5 FA 03 00 AA F6 03 07 AA" \
  })                                                                                                                    \

#elif _WIN32

#include <shlobj.h>
#include <string>
#include <vector>
#include <windows.h>


#define OREUI_PATTERN                                                                                                    \
     std::initializer_list<const char *>({                                                                               \
    "40 53 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 68 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 49 8B E9 4C 89 44 24 ? 4C 8B EA 48 8B F9 48 89 4C 24", \
    "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC 18 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 49 8B F1 4C 89 44 24", \
    "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC B8 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 49 8B F1 4C 89 44 24", \
    "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC 98 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4D 8B F1 4C 89 44 24" \
  })                                                                                                 \

 #endif

// clang-format on

namespace {

#if defined(_WIN32)

std::string getMinecraftModsPath() {
  char appDataPath[MAX_PATH];
  if (FAILED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
    printf("Failed to get APPDATA path.\n");
    return "";
  }

  std::string path = std::string(appDataPath) + "\\Minecraft Bedrock\\mods";
  return path;
}

std::string getUWPModsDir() {
  std::string appDataPath = getMinecraftModsPath();
  std::string uwpMods = appDataPath + "\\ForceCloseOreUI\\";
  return uwpMods;
}
#endif

bool testDirWritable(const std::string &dir) {
  std::error_code _;
  std::filesystem::create_directories(dir, _);
  std::string testFile = dir + "._perm_test";
  std::ofstream ofs(testFile);
  bool ok = ofs.is_open();
  ofs.close();
  if (ok)
    std::filesystem::remove(testFile, _);
  return ok;
}

std::string getConfigDir() {
#if defined(_WIN32)
  std::string primary = "mods/ForceCloseOreUI/";
  std::string fallback = getUWPModsDir();
  if (testDirWritable(fallback))
    return fallback;
  return primary;
#else
  std::string primary = "/sdcard/games";
  if (!primary.empty()) {
    primary += "/ForceCloseOreUI/";
    if (testDirWritable(primary)) {
#if __arm__ || __aarch64__
      logMessage("Using config dir: " + primary);
#endif
      return primary;
    }
  }
  
  if (!env) {
#if __arm__ || __aarch64__
    logMessage("JNIEnv is null, cannot use Android/data path");
#endif
    return primary;
  }
  
  std::string base = GetModsFilesPath(env);
  if (!base.empty()) {
    base += "/ForceCloseOreUI/";
    if (testDirWritable(base)) {
#if __arm__ || __aarch64__
      logMessage("Using config dir: " + base);
#endif
      return base;
    } else {
      std::string msg = "Cannot write to: " + base + ", using fallback";
#if __arm__ || __aarch64__
      logMessage(msg);
#endif
    }
  }
  
#if __arm__ || __aarch64__
  logMessage("Using fallback config dir: " + primary);
#endif
  return primary;
#endif
}

nlohmann::json outputJson;
std::string dirPath = "";
std::string filePath = dirPath + "config.json";
bool updated = false;

void saveJson(const std::string &path, const nlohmann::json &j) {
  try {
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    FILE *f = std::fopen(path.c_str(), "w");
    if (!f) {
      std::string error = "Failed to open file for writing: " + path;
#if __arm__ || __aarch64__
      logMessage(error);
#endif
      throw std::runtime_error(path);
    }
    std::string jsonStr = j.dump(4);
    std::fwrite(jsonStr.data(), 1, jsonStr.size(), f);
    std::fclose(f);
    
#if __arm__ || __aarch64__
    logMessage("Config saved successfully to: " + path);
#endif
  } catch (const std::exception &e) {
    std::string error = "Save config failed: " + std::string(e.what());
#if __arm__ || __aarch64__
    logMessage(error);
#endif
  }
}

SKY_AUTO_STATIC_HOOK(Hook2, memory::HookPriority::Normal, OREUI_PATTERN, void,
                     void *a1, void *a2, void *a3, void *a4, void *a5, void *a6,
                     void *a7, void *a8, void *a9, OreUi &a10, void *a11) {
  
#if __arm__ || __aarch64__
  logMessage("ForceCloseOreUI Hook2 triggered");
#endif
  
  dirPath = getConfigDir();
  filePath = dirPath + "config.json";

  if (std::filesystem::exists(filePath)) {
    try {
      std::ifstream inFile(filePath);
      inFile >> outputJson;
      inFile.close();
#if __arm__ || __aarch64__
      logMessage("Config loaded from: " + filePath);
#endif
    } catch (const std::exception &e) {
#if __arm__ || __aarch64__
      logMessage(std::string("Failed to load config: ") + e.what());
#endif
    }
  } else {
#if __arm__ || __aarch64__
    logMessage("Config not found, creating default: " + filePath);
#endif
  }

  int enabledCount = 0;
  for (auto &data : a10.mConfigs) {

    bool value = false;
    if (outputJson.contains(data.first) &&
        outputJson[data.first].is_boolean()) {
      value = outputJson[data.first];
      if (value) enabledCount++;
    } else {
      outputJson[data.first] = false;
      updated = true;
    }
    data.second.mUnknown3 = [value]() { return value; };
    data.second.mUnknown4 = [value]() { return value; };
  }
  
#if __arm__ || __aarch64__
  std::string summary = "Loaded " + std::to_string(a10.mConfigs.size()) + 
                        " configs, " + std::to_string(enabledCount) + " enabled";
  logMessage(summary);
#endif

  if (updated || !std::filesystem::exists(filePath)) {
    saveJson(filePath, outputJson);
  }

  origin(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

} // namespace
