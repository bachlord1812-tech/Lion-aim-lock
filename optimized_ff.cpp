                // ================================================================
// HeadHyperTrick.cpp – Module bám đầu cao cấp, tối ưu cho Free Fire
// Tính năng: Tìm mục tiêu theo bán kính crosshair (tâm màn hình)
//            Siêu bám đầu, tăng sensitivity khi kéo, giảm giật khi dính
// Sử dụng: Copy file này vào dự án, #include và dùng class HeadHyperTrick
// ================================================================

#include <cmath>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>

// -------------------- CẤU TRÚC DỮ LIỆU --------------------

// Thông tin một đối tượng (người chơi)
struct Entity {
    float headX, headY;     // Vị trí đầu (pixel)
    float neckX, neckY;     // Vị trí cổ
    float bodyX, bodyY;     // Vị trí thân
    float width, height;    // Kích thước bounding box
    float distance;         // Khoảng cách tới camera
    float velX, velY;       // Vận tốc (pixel/giây)
    int health;             // Máu hiện tại
    int team;               // Đội (0 = địch)
    bool alive;
    bool visible;
    bool moving;
    bool crouching;
};

// Cấu hình module
struct HyperConfig {
    // ---- Bám đầu ----
    float lockStrength;     // Lực bám chính (0.5 – 2.0)
    float lockSpeed;        // Tốc độ bám (0.5 – 3.0)
    float snapDistance;     // Khoảng cách tự động snap (pixel)

    // ---- Tìm mục tiêu theo bán kính ----
    float searchRadius;     // Bán kính tìm kiếm (pixel) – thay thế cho FOV

    // ---- Nhẹ tâm / Mượt ----
    float smoothness;       // Độ mượt mục tiêu (0.7 – 0.99)
    float inertia;          // Quán tính (0.0 – 0.5)

    // ---- Fix rung (giảm giật) ----
    float lowPassAlpha;     // Lọc thông thấp (0.0 – 1.0)

    // ---- Fix lố ----
    float maxStep;          // Bước di chuyển tối đa mỗi frame (pixel)

    // ---- Fix lạc đạn ----
    bool predictionEnabled;
    float gravityComp;      // Bù trọng lực
    float velocityComp;     // Bù vận tốc đối thủ
    float bulletSpeed;      // Tốc độ đạn (pixel/giây)

    // ---- Hỗ trợ kéo vào đầu (tăng sensitivity) ----
    float attractionRadius; // Bán kính vùng hút (pixel)
    float attractionForce;  // Lực hút vào đầu
    float dragSensitivity;  // Hệ số tăng sensitivity khi kéo (1.0 – 3.0)

    // ---- Nhiễu (anti-detection) ----
    float noise;            // Nhiễu nhân tạo (pixel)
};

// -------------------- LỚP CHÍNH --------------------

class HeadHyperTrick {
public:
    HeadHyperTrick();
    ~HeadHyperTrick() = default;

    // Khởi tạo với kích thước màn hình
    void init(float screenWidth, float screenHeight);

    // Cập nhật danh sách entity (gọi mỗi khi có dữ liệu mới)
    void setEntities(const Entity* entities, int count);

    // Vòng lặp chính (gọi mỗi frame)
    // dt: thời gian giữa các frame (giây)
    // centerX, centerY: vị trí crosshair hiện tại (tâm màn hình)
    // isDragging: true nếu người dùng đang kéo chuột/cảm ứng
    void update(float dt, float centerX, float centerY, bool isDragging);

    // Lấy vị trí crosshair mới
    void getCrosshair(float& x, float& y) const;

    // Kiểm tra đã khóa vào đầu chưa
    bool isLocked() const;

    // Lấy lực bám hiện tại
    float getLockStrength() const;

    // Thiết lập cấu hình
    void setConfig(const HyperConfig& cfg);
    const HyperConfig& getConfig() const { return config; }

    // Reset trạng thái
    void reset();

private:
    struct Tracker {
        float curX, curY;       // Vị trí crosshair hiện tại
        float tarX, tarY;       // Vị trí mục tiêu (đầu đối thủ)
        float filteredX, filteredY; // Sau lọc rung
        float strength;         // Lực bám (0 – 1)
        bool locked;            // Đã khóa?
        float inertiaX, inertiaY; // Quán tính
        float lastCenterX, lastCenterY; // Vị trí tâm frame trước (để phát hiện kéo)
    };

    Tracker tracker;
    HyperConfig config;
    std::vector<Entity> entities;
    float screenW, screenH;
    float centerX, centerY;
    bool lastDragging;
    std::mt19937 rng;
    std::normal_distribution<float> noiseDist;

    // Hàm nội bộ
    Entity findBestTarget(float cx, float cy);
    float calculatePriority(const Entity& e, float cx, float cy);
    void predictHead(const Entity& e, float& outX, float& outY, float dt);
    void applyLowPass(float& value, float target, float alpha);
    void applyInertia(float& value, float target, float inertiaFactor);
};

// -------------------- TRIỂN KHAI --------------------

HeadHyperTrick::HeadHyperTrick() {
    screenW = 1920.0f;
    screenH = 1080.0f;
    centerX = screenW / 2.0f;
    centerY = screenH / 2.0f;
    lastDragging = false;

    // Cấu hình mặc định (tối ưu cho Free Fire)
    config.lockStrength = 1.3f;
    config.lockSpeed = 2.0f;
    config.snapDistance = 80.0f;
    config.searchRadius = 300.0f;      // Bán kính tìm kiếm (pixel)
    config.smoothness = 0.88f;
    config.inertia = 0.12f;
    config.lowPassAlpha = 0.25f;
    config.maxStep = 600.0f;
    config.predictionEnabled = true;
    config.gravityComp = 1.0f;
    config.velocityComp = 1.2f;
    config.bulletSpeed = 850.0f;
    config.attractionRadius = 180.0f;
    config.attractionForce = 1.5f;
    config.dragSensitivity = 2.0f;
    config.noise = 0.5f;

    reset();

    // Seed cho random
    unsigned seed = std::chrono::steady_clock::now().time_since_epoch().count();
    rng.seed(seed);
    noiseDist = std::normal_distribution<float>(0.0f, 1.0f);
}

void HeadHyperTrick::init(float screenWidth, float screenHeight) {
    screenW = screenWidth;
    screenH = screenHeight;
    centerX = screenW / 2.0f;
    centerY = screenH / 2.0f;
    reset();
}

void HeadHyperTrick::reset() {
    tracker.curX = screenW / 2.0f;
    tracker.curY = screenH / 2.0f;
    tracker.tarX = tracker.curX;
    tracker.tarY = tracker.curY;
    tracker.filteredX = tracker.curX;
    tracker.filteredY = tracker.curY;
    tracker.strength = 0.0f;
    tracker.locked = false;
    tracker.inertiaX = 0.0f;
    tracker.inertiaY = 0.0f;
    tracker.lastCenterX = centerX;
    tracker.lastCenterY = centerY;
    lastDragging = false;
}

void HeadHyperTrick::setEntities(const Entity* entities, int count) {
    this->entities.clear();
    for (int i = 0; i < count; ++i) {
        this->entities.push_back(entities[i]);
    }
}

// -------------------- TÍNH ĐIỂM ƯU TIÊN (THEO BÁN KÍNH) --------------------
float HeadHyperTrick::calculatePriority(const Entity& e, float cx, float cy) {
    if (!e.alive || !e.visible) return -9999.0f;

    float d = std::hypot(e.headX - cx, e.headY - cy);

    // Loại bỏ nếu nằm ngoài bán kính tìm kiếm
    if (d > config.searchRadius) return -9999.0f;

    float score = 1000.0f;
    score -= d * 0.8f;  // Càng gần tâm càng cao điểm

    // Các yếu tố ưu tiên khác
    if (e.health < 30) score += 400.0f;
    if (e.crouching) score += 250.0f;
    if (e.moving) score -= 200.0f;
    if (e.team != 0) score += 150.0f;
    if (e.distance < 300.0f) score += 200.0f;

    return score;
}

Entity HeadHyperTrick::findBestTarget(float cx, float cy) {
    Entity best;
    best.alive = false;
    float bestScore = -99999.0f;

    for (const auto& e : entities) {
        float score = calculatePriority(e, cx, cy);
        if (score > bestScore) {
            bestScore = score;
            best = e;
        }
    }
    return best;
}

// -------------------- DỰ ĐOÁN VỊ TRÍ ĐẦU --------------------
void HeadHyperTrick::predictHead(const Entity& e, float& outX, float& outY, float dt) {
    if (!config.predictionEnabled) {
        outX = e.headX;
        outY = e.headY;
        return;
    }

    float bulletTime = e.distance / config.bulletSpeed;
    bulletTime = std::max(0.01f, bulletTime);

    float leadX = e.velX * bulletTime * config.velocityComp;
    float leadY = e.velY * bulletTime * config.velocityComp;
    float gravityOffset = 0.5f * 980.0f * bulletTime * bulletTime * config.gravityComp;

    outX = e.headX + leadX;
    outY = e.headY + leadY + gravityOffset;
}

// -------------------- BỘ LỌC --------------------
void HeadHyperTrick::applyLowPass(float& value, float target, float alpha) {
    value = value + (target - value) * alpha;
}

void HeadHyperTrick::applyInertia(float& value, float target, float inertiaFactor) {
    float diff = target - value;
    float maxInertia = std::abs(diff) * inertiaFactor;
    float step = diff * 0.5f;
    float inertiaStep = std::min(maxInertia, std::abs(step)) * (step >= 0 ? 1.0f : -1.0f);
    value += inertiaStep;
}

// -------------------- VÒNG LẶP CHÍNH --------------------
void HeadHyperTrick::update(float dt, float cx, float cy, bool isDragging) {
    centerX = cx;
    centerY = cy;

    // Nếu không có entity, reset
    if (entities.empty()) {
        if (tracker.locked) {
            tracker.locked = false;
            tracker.strength = 0.0f;
        }
        return;
    }

    // Chọn mục tiêu
    Entity target = findBestTarget(centerX, centerY);
    if (!target.alive) {
        if (tracker.locked) {
            tracker.locked = false;
            tracker.strength = 0.0f;
        }
        return;
    }

    // Dự đoán vị trí đầu
    float predX, predY;
    predictHead(target, predX, predY, dt);

    // ---- Hỗ trợ kéo: tăng sensitivity khi đang kéo ----
    float effectiveSensitivity = 1.0f;
    if (isDragging) {
        float dx = centerX - tracker.lastCenterX;
        float dy = centerY - tracker.lastCenterY;
        float speed = std::hypot(dx, dy);
        float speedFactor = std::min(speed / 10.0f, 1.0f);
        effectiveSensitivity = 1.0f + (config.dragSensitivity - 1.0f) * speedFactor;
    }

    // ---- Vùng hút (attraction) ----
    float attractX = predX, attractY = predY;
    float dToTarget = std::hypot(predX - centerX, predY - centerY);
    if (dToTarget < config.attractionRadius) {
        float attractFactor = config.attractionForce * (1.0f - dToTarget / config.attractionRadius);
        attractFactor = std::clamp(attractFactor, 0.0f, 1.0f);
        attractX = centerX + (predX - centerX) * attractFactor * effectiveSensitivity;
        attractY = centerY + (predY - centerY) * attractFactor * effectiveSensitivity;
    }

    // ---- Lọc rung ----
    applyLowPass(tracker.filteredX, attractX, config.lowPassAlpha);
    applyLowPass(tracker.filteredY, attractY, config.lowPassAlpha);

    // ---- Cập nhật mục tiêu ----
    if (!tracker.locked) {
        tracker.tarX = tracker.filteredX;
        tracker.tarY = tracker.filteredY;
        tracker.locked = true;
        tracker.strength = config.lockStrength;
    } else {
        float smooth = config.smoothness;
        tracker.tarX += (tracker.filteredX - tracker.tarX) * smooth;
        tracker.tarY += (tracker.filteredY - tracker.tarY) * smooth;
    }

    // ---- Quán tính (nhẹ tâm) ----
    applyInertia(tracker.inertiaX, tracker.tarX - tracker.curX, config.inertia);
    applyInertia(tracker.inertiaY, tracker.tarY - tracker.curY, config.inertia);

    // ---- Di chuyển crosshair ----
    float dx = tracker.tarX - tracker.curX + tracker.inertiaX;
    float dy = tracker.tarY - tracker.curY + tracker.inertiaY;
    float d = std::hypot(dx, dy);

    if (d > 0.5f) {
        float maxStep = config.maxStep * dt * config.lockSpeed * tracker.strength;
        maxStep = std::min(maxStep, d * 0.85f);
        float step = std::min(d, maxStep);
        float angle = std::atan2(dy, dx);

        tracker.curX += std::cos(angle) * step;
        tracker.curY += std::sin(angle) * step;

        // Nhiễu nhẹ
        float noise = noiseDist(rng) * config.noise * 0.3f;
        tracker.curX += noise;
        tracker.curY += noise;
    }

    // ---- Cập nhật lực bám dựa trên khoảng cách ----
    float dToTargetReal = std::hypot(tracker.curX - tracker.tarX, tracker.curY - tracker.tarY);
    if (dToTargetReal < config.snapDistance) {
        tracker.strength = std::min(1.0f, tracker.strength + dt * 2.0f);
    } else {
        tracker.strength = std::max(0.2f, tracker.strength - dt * 0.5f);
    }

    // Nếu quá xa, giải phóng
    if (dToTargetReal > config.snapDistance * 6.0f) {
        tracker.locked = false;
        tracker.strength = 0.0f;
    }

    // Cập nhật locked
    tracker.locked = (tracker.strength > 0.3f);

    // Lưu trạng thái kéo cho frame sau
    tracker.lastCenterX = centerX;
    tracker.lastCenterY = centerY;
    lastDragging = isDragging;
}

// -------------------- GETTERS / SETTERS --------------------
void HeadHyperTrick::getCrosshair(float& x, float& y) const {
    x = tracker.curX;
    y = tracker.curY;
}

bool HeadHyperTrick::isLocked() const {
    return tracker.locked;
}

float HeadHyperTrick::getLockStrength() const {
    return tracker.strength;
}

void HeadHyperTrick::setConfig(const HyperConfig& cfg) {
    config = cfg;
}
