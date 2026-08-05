
#include "raylib.h"
#include <deque>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>

// Raw configuration data payload to be exported
const std::string configData = R"raw_config(Aimlock Extreme Performance Config

<?xml version="1.0" encoding="UTF-8"?>
<project version="1">
  <component name="JavaVersion" value="1" />
  <component name="WriteSettingHideVersion" value="1" />
  <option name="bin.mt.plus">
    <Asset$DataAsset>
      <option name="externalDataAsset" strings="$WRITE_EXTERNALS$" />
      <option name="com.android.accesspointernetwork" strings="#WRITE_EXTENAL_SETTING_PERMISSION" />
      <option name="com.android.ACCESS_POINTER_SPEED" strings="MAX_POINTER_SPEED_100%" />
      <option name="COMPRESS" strings="true" />
    </Asset$DataAsset>
  </option>
</project>

<project version="2">
  <Asset$DataAsset id="1">
    <option name="on=1" strings="$WRITE_FILE$" />
    <option name="off=0" strings="$WRITE_FILE$" />
    <option name="com.act_conf_seclect_sync_device_act_allow.file_code_unlock_connectInject_RDR_aimLockBase64_0x7608F0_set_on_auto_cws.uncrack.list=1" strings="$WRITE_REGULATOR_DATA$" />
    <option name="com.act_conf_seclect_sync_device_act_allow.file_code_unlock_connectInject_OptimalAimlockffbasex64_set_on_auto_cws.uncrack.strings=1" strings="$WRITE_REGULATOR_DATA$" />
    <option name="com.acp_conf_seclect_seclect_sync_device_act_rick.file_code_on_function_aimlockffbase64_set_aimlock_auto_cws.uncrack.list=1" strings="$WRITE_REGULATOR_DATA$" />
    <option name="Sensitivity_Multiplier" value="2.0" />
    <option name="Lock_Precision_Ratio" value="1.0" />
    <option name="Smooth_Lock_Rate" value="100%" />
    <option name="DucNam" boolean="1" />
  </Asset$DataAsset>
</project>

<project version="3">
  <Asset$DataAsset> 
    <option name="acp_click=1" strings="$WRITE_REGULATOR_DATA$" />
    <option name="on_com.dts.freefireth_launcher=1" strings="$WRITE_REGULATOR_DATA$" />
    <option name="com.restore-device.launcher=1" strings="$WRITE_REGULATOR_DATA$" />
    <option name="DucNam" boolean="1" />
  </Asset$DataAsset>
</project>
)raw_config";

// Function to automatically write configuration string to file
bool ExportConfigFile(const std::string& filename) {
    std::ofstream outFile(filename);
    if (outFile.is_open()) {
        outFile << configData;
        outFile.close();
        return true;
    }
    return false;
}

// Particle entity with physics calculation and visual trail
struct SmoothParticle {
    Vector2 position;
    Vector2 currentVelocity;
    Vector2 targetVelocity;
    float radius;
    float smoothingFactor;
    Color color;

    float speed;
    float acceleration;
    std::deque<Vector2> positionHistory;
    size_t maxTrailLength;

    SmoothParticle(float x, float y, float r, float smoothing)
        : position{x, y}, 
          currentVelocity{0.0f, 0.0f}, 
          targetVelocity{0.0f, 0.0f},
          radius(r), 
          smoothingFactor(smoothing), 
          color(SKYBLUE),
          speed(0.0f), 
          acceleration(0.0f), 
          maxTrailLength(32) {}

    void Update(float dt, int screenWidth, int screenHeight) {
        // Random velocity pulse target
        targetVelocity.x = static_cast<float>((rand() % 1200) - 600);
        targetVelocity.y = static_cast<float>((rand() % 1200) - 600);

        // Frame-rate independent exponential smoothing
        Vector2 previousVelocity = currentVelocity;
        float alpha = 1.0f - std::pow(1.0f - smoothingFactor, dt * 120.0f);
        
        currentVelocity.x += (targetVelocity.x - currentVelocity.x) * alpha;
        currentVelocity.y += (targetVelocity.y - currentVelocity.y) * alpha;

        // Compute speed and acceleration
        speed = std::sqrt(currentVelocity.x * currentVelocity.x + currentVelocity.y * currentVelocity.y);
        
        float dvx = currentVelocity.x - previousVelocity.x;
        float dvy = currentVelocity.y - previousVelocity.y;
        acceleration = (dt > 0.0f) ? (std::sqrt(dvx * dvx + dvy * dvy) / dt) : 0.0f;

        // Dynamic color shifting based on speed
        color = ColorFromHSV(fmodf(speed * 0.5f, 360.0f), 0.8f, 1.0f);

        // Position integration
        position.x += currentVelocity.x * dt * 3.0f;
        position.y += currentVelocity.y * dt * 3.0f;

        // Boundary collision with dampening
        if (position.x - radius < 0.0f) {
            position.x = radius;
            currentVelocity.x *= -0.7f;
        } else if (position.x + radius > static_cast<float>(screenWidth)) {
            position.x = static_cast<float>(screenWidth) - radius;
            currentVelocity.x *= -0.7f;
        }

        if (position.y - radius < 0.0f) {
            position.y = radius;
            currentVelocity.y *= -0.7f;
        } else if (position.y + radius > static_cast<float>(screenHeight)) {
            position.y = static_cast<float>(screenHeight) - radius;
            currentVelocity.y *= -0.7f;
        }

        // Record motion history trail
        positionHistory.push_front(position);
        if (positionHistory.size() > maxTrailLength) {
            positionHistory.pop_back();
        }
    }

    void Draw() const {
        // Draw particle trail
        for (size_t i = 0; i < positionHistory.size(); ++i) {
            float fadeAlpha = 1.0f - (static_cast<float>(i) / static_cast<float>(positionHistory.size()));
            float trailRadius = radius * fadeAlpha;
            Color trailColor = ColorAlpha(color, fadeAlpha * 0.45f);
            DrawCircleV(positionHistory[i], trailRadius, trailColor);
        }

        // Draw main entity
        DrawCircleV(position, radius, color);
        DrawCircleLines(static_cast<int>(position.x), static_cast<int>(position.y), radius + 2.0f, WHITE);
    }
};

int main() {
    // Export text configuration file on startup
    const std::string txtFileName = "aimlock_config.txt";
    bool isFileExported = ExportConfigFile(txtFileName);

    const int screenWidth = 1280;
    const int screenHeight = 720;
    const int targetFPS = 240;

    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    InitWindow(screenWidth, screenHeight, "Ultra Motion Engine & Config Generator");
    SetTargetFPS(targetFPS);

    SmoothParticle particle(screenWidth / 2.0f, screenHeight / 2.0f, 20.0f, 0.25f);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        particle.Update(dt, screenWidth, screenHeight);

        BeginDrawing();
        ClearBackground(GetColor(0x181818FF));

        particle.Draw();

        // Render Telemetry OSD
        DrawFPS(20, 20);
        DrawText(TextFormat("Target Frame Rate: %d FPS (MAX)", targetFPS), 20, 45, 18, GREEN);
        DrawText(TextFormat("Frame Time: %.3f ms", dt * 1000.0f), 20, 68, 18, LIME);
        
        DrawRectangle(15, 95, 420, 150, ColorAlpha(BLACK, 0.6f));
        DrawRectangleLines(15, 95, 420, 150, DARKGRAY);

        DrawText(TextFormat("Position (X, Y): (%.1f, %.1f)", particle.position.x, particle.position.y), 25, 105, 18, RAYWHITE);
        DrawText(TextFormat("Velocity (Vx, Vy): (%.1f, %.1f)", particle.currentVelocity.x, particle.currentVelocity.y), 25, 128, 18, RAYWHITE);
        DrawText(TextFormat("Current Speed: %.2f px/s", particle.speed), 25, 151, 18, YELLOW);
        DrawText(TextFormat("Acceleration: %.2f px/s2", particle.acceleration), 25, 174, 18, ORANGE);
        DrawText(TextFormat("Smoothing Factor: %.2f (Ultra Fast)", particle.smoothingFactor), 25, 197, 18, SKYBLUE);

        // Status banner
        if (isFileExported) {
            DrawText(TextFormat("STATUS: Config exported to '%s' SUCCESS!", txtFileName.c_str()), 20, screenHeight - 35, 18, GREEN);
        } else {
            DrawText("STATUS: Failed to write config file!", 20, screenHeight - 35, 18, RED);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
