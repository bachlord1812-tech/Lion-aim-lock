// optimized_ff.cpp
// Tối ưu cho Free Fire – Hỗ trợ HeadLock + DeepScan + Hook
// Biên dịch: ndk-build hoặc CMake với Android NDK r23+
#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/input.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <unistd.h>
#include <cstring>
#include <cmath>
#include <vector>
#include <chrono>
#include <thread>
#include <algorithm>
#include <atomic>
#include <random>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <dlfcn.h>
#include <mutex>
#include <shared_mutex>

#define LOG_TAG "FF_Opt"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ==================== CẤU TRÚC DỮ LIỆU ====================
struct Entity {
    float x, y, width, height;
    float headX, headY, headRadius;
    float neckX, neckY;
    float bodyX, bodyY;
    float distance;
    float velX, velY, accX, accY;
    int health, maxHealth, armor, team;
    bool visible, alive, moving, jumping, crouching;
    uintptr_t address;           // Địa chỉ entity trong bộ nhớ
    float pos[3];                // Vị trí 3D
    std::chrono::steady_clock::time_point lastSeen, firstSeen;
};

struct Tracker {
    float curX, curY, tarX, tarY, velX, velY;
    float strength, accuracy;
    int hits, shots;
    float hitRate;
    bool locked;
};

struct HookEntry {
    uintptr_t target, hook;
    std::vector<uint8_t> orig, patch;
    bool active;
};

// ==================== BIẾN TOÀN CỤC ====================
static int W, H;
static float density;
static std::atomic<bool> running{true};
static std::atomic<bool> aimActive{false};
static std::atomic<bool> fireActive{false};

static std::shared_mutex entityMutex;
static std::vector<Entity> entities;
static Entity current, best;
static Tracker tracker;
static std::mt19937 rng;
static std::normal_distribution<float> jitter(0.0f, 0.012f);

static int memFd = -1;
static std::mutex memMutex;
static uintptr_t libBase = 0;
static pid_t gamePid = 0;
static const char* TARGET_PKG = "com.dts.freefireth";
static const char* TARGET_LIB = "libil2cpp.so";

static std::vector<HookEntry> hooks;
static std::shared_mutex hookMutex;

// ==================== HÀM TIỆN ÍCH ====================
static float dist(float x1, float y1, float x2, float y2) {
    return sqrtf((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1));
}

static pid_t findPid() {
    DIR* dir = opendir("/proc");
    if (!dir) return -1;
    struct dirent* e;
    while ((e = readdir(dir))) {
        if (e->d_type != DT_DIR) continue;
        int pid = atoi(e->d_name);
        if (!pid) continue;
        char path[256]; snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        char cmd[128]; if (fgets(cmd, sizeof(cmd), f) && strstr(cmd, TARGET_PKG)) {
            fclose(f); closedir(dir); return pid;
        }
        fclose(f);
    }
    closedir(dir);
    return -1;
}

static int openMem() {
    std::lock_guard<std::mutex> lock(memMutex);
    if (memFd >= 0) return memFd;
    memFd = open("/dev/mem", O_RDWR);
    if (memFd < 0) memFd = open("/dev/mem", O_RDONLY);
    return memFd;
}

static void closeMem() {
    std::lock_guard<std::mutex> lock(memMutex);
    if (memFd >= 0) { close(memFd); memFd = -1; }
}

static bool readMem(uintptr_t addr, void* buf, size_t sz) {
    int fd = openMem();
    if (fd < 0) return false;
    lseek(fd, addr, SEEK_SET);
    ssize_t n = read(fd, buf, sz);
    return n == (ssize_t)sz;
}

static bool writeMem(uintptr_t addr, const void* buf, size_t sz) {
    int fd = openMem();
    if (fd < 0) return false;
    lseek(fd, addr, SEEK_SET);
    ssize_t n = write(fd, buf, sz);
    return n == (ssize_t)sz;
}

static uintptr_t getModuleBase(const char* name) {
    pid_t pid = findPid();
    if (pid <= 0) return 0;
    char path[256]; snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        uintptr_t start, end;
        char perms[8], fname[256] = {0};
        if (sscanf(line, "%lx-%lx %s %*x %*x:%*x %*d %255s", &start, &end, perms, fname) == 4) {
            if (strstr(fname, name) && perms[0] == 'r') {
                base = start;
                break;
            }
        }
    }
    fclose(f);
    return base;
}

// ==================== QUÉT PATTERN ====================
static uintptr_t scanPattern(const std::vector<uint8_t>& pat, const std::vector<uint8_t>& mask, const char* module = nullptr) {
    pid_t pid = findPid();
    if (pid <= 0) return 0;
    char path[256]; snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    uintptr_t found = 0;
    while (fgets(line, sizeof(line), f) && !found) {
        uintptr_t start, end; char perms[8], fname[256] = {0};
        if (sscanf(line, "%lx-%lx %s %*x %*x:%*x %*d %255s", &start, &end, perms, fname) == 4) {
            if (module && !strstr(fname, module)) continue;
            if (!strchr(perms, 'r')) continue;
            size_t sz = end - start;
            std::vector<uint8_t> buf(sz);
            if (!readMem(start, buf.data(), sz)) continue;
            for (size_t i = 0; i <= sz - pat.size(); ++i) {
                bool ok = true;
                for (size_t j = 0; j < pat.size(); ++j) {
                    if (mask[j] && buf[i+j] != pat[j]) { ok = false; break; }
                }
                if (ok) { found = start + i; break; }
            }
        }
    }
    fclose(f);
    return found;
}

// ==================== ENTITY SCANNER ====================
static void scanEntities() {
    if (libBase == 0) libBase = getModuleBase(TARGET_LIB);
    if (!libBase) return;

    // Đọc danh sách entity (cần tìm offset thực tế)
    uintptr_t entityList = libBase + 0x123456; // Thay bằng offset thực
    uintptr_t listPtr; if (!readMem(entityList, &listPtr, sizeof(listPtr))) return;
    int count; if (!readMem(entityList + 8, &count, sizeof(count))) return;
    if (count > 100) count = 100;

    std::vector<Entity> newEnts;
    for (int i = 0; i < count; ++i) {
        uintptr_t ptr; if (!readMem(listPtr + i*8, &ptr, sizeof(ptr)) || !ptr) continue;
        Entity e{};
        e.address = ptr;
        readMem(ptr + 0x100, &e.health, 4);
        readMem(ptr + 0x104, &e.maxHealth, 4);
        readMem(ptr + 0x108, &e.armor, 4);
        readMem(ptr + 0x10C, &e.team, 4);
        readMem(ptr + 0x110, e.pos, 12);
        readMem(ptr + 0x120, &e.velX, 12);
        if (e.health > 0 && e.team >= 0) {
            e.alive = true;
            e.distance = sqrtf(e.pos[0]*e.pos[0] + e.pos[1]*e.pos[1] + e.pos[2]*e.pos[2]);
            newEnts.push_back(e);
        }
    }
    if (!newEnts.empty()) {
        std::unique_lock lock(entityMutex);
        entities = std::move(newEnts);
    }
}

// ==================== HEAD LOCK ====================
static void updateHeadLock(float dt) {
    if (!aimActive) {
        if (tracker.locked) { tracker.locked = false; tracker.strength = 0; }
        return;
    }

    // Chọn target tốt nhất trong FOV
    float bestScore = -1e9;
    Entity bestEnt;
    bool found = false;
    {
        std::shared_lock lock(entityMutex);
        for (const auto& e : entities) {
            if (!e.alive) continue;
            float dx = e.headX - W/2, dy = e.headY - H/2;
            float d = sqrtf(dx*dx + dy*dy);
            if (d > W*0.8f) continue;
            float score = 1000.0f - d*0.5f;
            if (e.health < 30) score += 300;
            if (e.crouching) score += 200;
            if (e.moving) score -= 150;
            if (e.team != 0) score += 100;
            if (score > bestScore) { bestScore = score; bestEnt = e; found = true; }
        }
    }
    if (!found) {
        if (tracker.locked) { tracker.locked = false; tracker.strength = 0; }
        return;
    }

    // Dự đoán vị trí đầu với lead và gravity
    float bulletTime = bestEnt.distance / 850.0f;
    float leadX = bestEnt.velX * bulletTime * 0.85f;
    float leadY = bestEnt.velY * bulletTime * 0.85f;
    float gravityOff = 0.5f * 980.0f * density * bulletTime * bulletTime;
    float targetX = bestEnt.headX + leadX;
    float targetY = bestEnt.headY + leadY + gravityOff;

    // Làm mượt + nhiễu nhân tạo
    if (!tracker.locked) {
        tracker.tarX = targetX; tracker.tarY = targetY;
        tracker.locked = true;
        tracker.strength = 1.0f;
    } else {
        float smooth = 0.82f;
        tracker.tarX += (targetX - tracker.tarX) * smooth;
        tracker.tarY += (targetY - tracker.tarY) * smooth;
    }

    // Điều chỉnh crosshair
    float dx = tracker.tarX - tracker.curX;
    float dy = tracker.tarY - tracker.curY;
    float d = sqrtf(dx*dx + dy*dy);
    if (d > 0.5f) {
        float maxStep = 1200.0f * density * dt * tracker.strength;
        float step = fminf(d, maxStep);
        float angle = atan2f(dy, dx);
        tracker.curX += cosf(angle) * step;
        tracker.curY += sinf(angle) * step;
        // nhiễu nhẹ
        tracker.curX += jitter(rng) * density * 0.3f;
        tracker.curY += jitter(rng) * density * 0.3f;
    }
}

// ==================== THREAD QUÉT ====================
static void* scanThread(void*) {
    while (running) {
        scanEntities();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return nullptr;
}

static void* hookThread(void*) {
    while (running) {
        std::shared_lock lock(hookMutex);
        for (auto& hk : hooks) {
            if (!hk.active) {
                // patch inline hook
                uintptr_t page = hk.target & ~(getpagesize()-1);
                mprotect((void*)page, getpagesize(), PROT_READ|PROT_WRITE|PROT_EXEC);
                writeMem(hk.target, hk.patch.data(), hk.patch.size());
                hk.active = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return nullptr;
}

// ==================== JNI INTERFACE ====================
extern "C" {

JNIEXPORT void JNICALL
Java_com_ffopt_OptimizedService_nativeInit(JNIEnv* env, jobject thiz,
                                            jobject surface, jobject inputQueue) {
    ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
    W = ANativeWindow_getWidth(win);
    H = ANativeWindow_getHeight(win);
    density = W / 1080.0f;

    gamePid = findPid();
    libBase = getModuleBase(TARGET_LIB);
    if (!libBase) LOGE("Không tìm thấy libil2cpp.so");

    // Khởi tạo tracker
    tracker.curX = W/2; tracker.curY = H/2;
    tracker.tarX = W/2; tracker.tarY = H/2;
    tracker.locked = false;

    rng.seed(std::chrono::steady_clock::now().time_since_epoch().count());

    // Khởi chạy thread
    pthread_t t1, t2;
    pthread_create(&t1, nullptr, scanThread, nullptr);
    pthread_create(&t2, nullptr, hookThread, nullptr);

    LOGI("Optimized FF Service initialized. W=%d H=%d density=%.2f", W, H, density);
}

JNIEXPORT void JNICALL
Java_com_ffopt_OptimizedService_nativeUpdateTargets(JNIEnv* env, jobject thiz,
                                                     jfloatArray data) {
    jfloat* arr = env->GetFloatArrayElements(data, nullptr);
    jsize len = env->GetArrayLength(data);
    // Dữ liệu đầu vào: [x,y,w,h,headX,headY,...] mỗi entity 22 float
    std::vector<Entity> newEnts;
    for (int i = 0; i+21 < len; i+=22) {
        Entity e{};
        e.x = arr[i]; e.y = arr[i+1]; e.width = arr[i+2]; e.height = arr[i+3];
        e.headX = arr[i+4]; e.headY = arr[i+5];
        e.neckX = arr[i+6]; e.neckY = arr[i+7];
        e.bodyX = arr[i+8]; e.bodyY = arr[i+9];
        e.distance = arr[i+10];
        e.velX = arr[i+11]; e.velY = arr[i+12];
        e.accX = arr[i+13]; e.accY = arr[i+14];
        e.health = (int)arr[i+15]; e.maxHealth = 100;
        e.team = (int)arr[i+16];
        e.visible = arr[i+17] > 0.5f;
        e.alive = e.health > 0;
        e.moving = (fabsf(e.velX) > 0.5f || fabsf(e.velY) > 0.5f);
        e.lastSeen = std::chrono::steady_clock::now();
        if (e.alive) newEnts.push_back(e);
    }
    env->ReleaseFloatArrayElements(data, arr, JNI_ABORT);

    if (!newEnts.empty()) {
        std::unique_lock lock(entityMutex);
        entities = std::move(newEnts);
    }
}

JNIEXPORT void JNICALL
Java_com_ffopt_OptimizedService_nativeUpdateAim(JNIEnv* env, jobject thiz,
                                                 jfloat deltaTime) {
    updateHeadLock(deltaTime);
}

JNIEXPORT jboolean JNICALL
Java_com_ffopt_OptimizedService_nativeIsLocked(JNIEnv* env, jobject thiz) {
    return tracker.locked ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jfloat JNICALL
Java_com_ffopt_OptimizedService_nativeGetLockStrength(JNIEnv* env, jobject thiz) {
    return tracker.strength;
}

JNIEXPORT void JNICALL
Java_com_ffopt_OptimizedService_nativeSetAimActive(JNIEnv* env, jobject thiz,
                                                    jboolean active) {
    aimActive = active;
    if (!active) {
        tracker.locked = false;
        tracker.strength = 0.0f;
    }
}

JNIEXPORT void JNICALL
Java_com_ffopt_OptimizedService_nativeSetFireActive(JNIEnv* env, jobject thiz,
                                                     jboolean active) {
    fireActive = active;
}

JNIEXPORT void JNICALL
Java_com_ffopt_OptimizedService_nativeDestroy(JNIEnv* env, jobject thiz) {
    running = false;
    closeMem();
    LOGI("Optimized FF Service destroyed.");
}

} // extern "C"
