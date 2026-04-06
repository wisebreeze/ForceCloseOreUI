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

jobject getGlobalContext(JNIEnv *env) {
  jclass activity_thread = env->FindClass("android/app/ActivityThread");
  jmethodID current_activity_thread =
      env->GetStaticMethodID(activity_thread, "currentActivityThread",
                             "()Landroid/app/ActivityThread;");
  jobject at =
      env->CallStaticObjectMethod(activity_thread, current_activity_thread);
  jmethodID get_application = env->GetMethodID(
      activity_thread, "getApplication", "()Landroid/app/Application;");
  jobject context = env->CallObjectMethod(at, get_application);
  if (env->ExceptionCheck())
    env->ExceptionClear();
  return context;
}

std::string getAbsolutePath(JNIEnv *env, jobject file) {
  jclass file_class = env->GetObjectClass(file);
  jmethodID get_abs_path =
      env->GetMethodID(file_class, "getAbsolutePath", "()Ljava/lang/String;");
  auto jstr = (jstring)env->CallObjectMethod(file, get_abs_path);
  if (env->ExceptionCheck())
    env->ExceptionClear();
  const char *cstr = env->GetStringUTFChars(jstr, nullptr);
  std::string result(cstr);
  env->ReleaseStringUTFChars(jstr, cstr);
  return result;
}

std::string getPackageName(JNIEnv *env, jobject context) {
  jclass context_class = env->GetObjectClass(context);
  jmethodID get_pkg_name =
      env->GetMethodID(context_class, "getPackageName", "()Ljava/lang/String;");
  auto jstr = (jstring)env->CallObjectMethod(context, get_pkg_name);
  if (env->ExceptionCheck())
    env->ExceptionClear();
  const char *cstr = env->GetStringUTFChars(jstr, nullptr);
  std::string result(cstr);
  env->ReleaseStringUTFChars(jstr, cstr);
  return result;
}

std::string getInternalStoragePath(JNIEnv *env) {
  jclass env_class = env->FindClass("android/os/Environment");
  jmethodID get_storage_dir = env->GetStaticMethodID(
      env_class, "getExternalStorageDirectory", "()Ljava/io/File;");
  jobject storage_dir = env->CallStaticObjectMethod(env_class, get_storage_dir);
  return getAbsolutePath(env, storage_dir);
}

// Toast 显示函数
void showToast(JNIEnv* env, const std::string& message) {
  if (!env) return;
  
  // 获取全局 Context
  jobject context = getGlobalContext(env);
  if (!context) {
    LOGI("Failed to get context for Toast");
    return;
  }
  
  // 获取 Toast 类
  jclass toastClass = env->FindClass("android/widget/Toast");
  if (!toastClass) {
    LOGI("Failed to find Toast class");
    env->DeleteLocalRef(context);
    return;
  }
  
  jmethodID makeTextMethod = env->GetStaticMethodID(
      toastClass, "makeText", 
      "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;"
  );
  
  if (!makeTextMethod) {
    LOGI("Failed to find makeText method");
    env->DeleteLocalRef(toastClass);
    env->DeleteLocalRef(context);
    return;
  }
  
  // 创建 Java 字符串
  jstring jMessage = env->NewStringUTF(message.c_str());
  
  // 调用 makeText (LENGTH_LONG = 1)
  jobject toast = env->CallStaticObjectMethod(
      toastClass, makeTextMethod, context, jMessage, 1
  );
  
  if (toast) {
    // 调用 show
    jmethodID showMethod = env->GetMethodID(toastClass, "show", "()V");
    if (showMethod) {
      env->CallVoidMethod(toast, showMethod);
    }
    env->DeleteLocalRef(toast);
  }
  
  // 清理局部引用
  env->DeleteLocalRef(jMessage);
  env->DeleteLocalRef(toastClass);
  env->DeleteLocalRef(context);
}

std::string GetModsFilesPath(JNIEnv *env) {
  jobject app_context = getGlobalContext(env);
  if (!app_context) {
    showToast(env, "Failed to get app context");
    LOGI("Failed to get app context");
    return "";
  }
  
  auto package_name = getPackageName(env, app_context);
  for (auto &c : package_name)
    c = tolower(c);
  
  std::string path = (fs::path(getInternalStoragePath(env)) / "Android" / "data" /
                      package_name / "mods");
  
  // 检查目录是否可访问
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    std::string msg = "Config dir not exist: " + path;
    LOGI("%s", msg.c_str());
    showToast(env, msg);
  }
  
  env->DeleteLocalRef(app_context);
  return path;
}

SKY_AUTO_STATIC_HOOK(
    Hook1, memory::HookPriority::Normal,
    std::initializer_list<const char *>(
        {"? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? "
         "91 ? ? ? D5 ? ? ? F9 ? ? ? F8 ? ? ? 39 ? ? ? 34 ? ? ? 12"}),
    int, void *_this, JavaVM *vm) {

  vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_4);
  if (env) {
    showToast(env, "ForceCloseOreUI initialized");
    LOGI("Hook1 triggered, JNIEnv obtained");
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
      if (env) showToast(env, "Using config dir: " + primary);
      return primary;
    }
  }
  
  if (!env) {
    LOGI("JNIEnv is null, cannot use Android/data path");
    if (env) showToast(env, "JNIEnv is null, using fallback path");
    return primary;
  }
  
  std::string base = GetModsFilesPath(env);
  if (!base.empty()) {
    base += "/ForceCloseOreUI/";
    if (testDirWritable(base)) {
      if (env) showToast(env, "Using config dir: " + base);
      return base;
    } else {
      std::string msg = "Cannot write to: " + base + ", using fallback";
      LOGI("%s", msg.c_str());
      if (env) showToast(env, msg);
    }
  }
  
  if (env) showToast(env, "Using fallback config dir: " + primary);
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
      LOGI("%s", error.c_str());
      if (env) showToast(env, error);
      throw std::runtime_error(path);
    }
    std::string jsonStr = j.dump(4);
    std::fwrite(jsonStr.data(), 1, jsonStr.size(), f);
    std::fclose(f);
    
    LOGI("Config saved successfully to: %s", path.c_str());
    if (env) showToast(env, "Config saved to: " + path);
  } catch (const std::exception &e) {
    std::string error = "Save config failed: " + std::string(e.what());
    LOGI("%s", error.c_str());
    if (env) showToast(env, error);
  }
}

SKY_AUTO_STATIC_HOOK(Hook2, memory::HookPriority::Normal, OREUI_PATTERN, void,
                     void *a1, void *a2, void *a3, void *a4, void *a5, void *a6,
                     void *a7, void *a8, void *a9, OreUi &a10, void *a11) {
  
  if (env) {
    showToast(env, "ForceCloseOreUI Hook2 triggered");
  }
  
  dirPath = getConfigDir();
  filePath = dirPath + "config.json";

  if (std::filesystem::exists(filePath)) {
    try {
      std::ifstream inFile(filePath);
      inFile >> outputJson;
      inFile.close();
      LOGI("Config loaded from: %s", filePath.c_str());
      if (env) showToast(env, "Config loaded from: " + filePath);
    } catch (const std::exception &e) {
      LOGI("Failed to load config: %s", e.what());
      if (env) showToast(env, "Failed to load config: " + std::string(e.what()));
    }
  } else {
    std::string msg = "Config not found, creating default: " + filePath;
    LOGI("%s", msg.c_str());
    if (env) showToast(env, msg);
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
  
  std::string summary = "Loaded " + std::to_string(a10.mConfigs.size()) + 
                        " configs, " + std::to_string(enabledCount) + " enabled";
  LOGI("%s", summary.c_str());
  if (env) showToast(env, summary);

  if (updated || !std::filesystem::exists(filePath)) {
    saveJson(filePath, outputJson);
  }

  origin(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

} // namespace
