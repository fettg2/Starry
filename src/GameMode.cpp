// GameMode.cpp
#include "GameMode.h"
#include "imgui.h"
#include <iostream>

// 全局游戏模式实例
GameMode gGameMode;

// 构造函数
GameMode::GameMode() : 
    state(GameState::IDLE),
    score(0.0f),
    lives(3),
    timeElapsed(0.0f),
    gameStarted(false) {
}

// 初始化游戏模式
void GameMode::init() {
    state = GameState::IDLE;
    score = 0.0f;
    lives = 3;
    timeElapsed = 0.0f;
    gameStarted = false;
    objectives.clear();
    generateObjectives();
    std::cout << "Game Mode initialized" << std::endl;
}

// 生成游戏目标
void GameMode::generateObjectives() {
    objectives.clear();
    
    // 固定游戏目标
    objectives.push_back(GameObjective("靠近10个天体", false, 200));
    objectives.push_back(GameObjective("生存60秒", false, 300));
    objectives.push_back(GameObjective("达到速度2000", false, 250));
}

// 更新游戏目标
void GameMode::updateObjectives() {
    // 这里可以根据游戏状态更新目标完成情况
    // 例如检查是否靠近了足够的天体，是否达到了速度要求等
}

// 更新游戏状态
void GameMode::update(float deltaTime, const std::vector<Object>& objs, const glm::vec3& shipPos, const glm::vec3& shipVel) {
    if (state != GameState::ACTIVE) return;
    
    timeElapsed += deltaTime;
    
    // 更新游戏目标
    updateObjectives();
    
    // 检查是否完成所有目标
    bool allCompleted = true;
    for (const auto& obj : objectives) {
        if (!obj.completed) {
            allCompleted = false;
            break;
        }
    }
    
    if (allCompleted) {
        // 完成所有目标
        score += 1000;
        generateObjectives();
        std::cout << "All objectives completed!" << std::endl;
    }
    
    // 检查游戏结束条件
    if (lives <= 0) {
        end();
    }
}

// 渲染游戏HUD
void GameMode::renderHUD() {
    if (state == GameState::IDLE) return;
    
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::Begin("Game Mode", nullptr, 
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | 
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    
    // 增大字体大小 - 设置窗口字体缩放
    ImGui::SetWindowFontScale(2.0f);
    
    ImGui::Text("Score: %.1f", score);
    ImGui::Text("Lives: %d", lives);
    ImGui::Text("Time: %.1f s", timeElapsed);
    
    ImGui::Separator();
    ImGui::Text("Objectives:");
    for (const auto& obj : objectives) {
        ImGui::Text("%s: %s", obj.description.c_str(), obj.completed ? "✓" : "✗");
    }
    
    if (state == GameState::GAME_OVER) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "GAME OVER!");
        ImGui::Text("Final Score: %.1f", score);
        if (ImGui::Button("Restart")) {
            init();
            start();
        }
    }
    
    // 恢复默认字体大小
    ImGui::SetWindowFontScale(1.0f);
    
    ImGui::End();
}

// 处理碰撞事件
void GameMode::handleCollision(const Object& obj) {
    if (state != GameState::ACTIVE) return;
    
    // 碰撞时减少生命值
    lives--;
    score -= 100;
    if (score < 0) score = 0;
    
    std::cout << "Collision! Lives left: " << lives << std::endl;
    
    if (lives <= 0) {
        end();
    }
}

// 开始游戏
void GameMode::start() {
    if (state == GameState::IDLE || state == GameState::GAME_OVER) {
        state = GameState::ACTIVE;
        gameStarted = true;
        std::cout << "Game started!" << std::endl;
    }
}

// 暂停游戏
void GameMode::pause() {
    if (state == GameState::ACTIVE) {
        state = GameState::PAUSED;
        std::cout << "Game paused" << std::endl;
    }
}

// 恢复游戏
void GameMode::resume() {
    if (state == GameState::PAUSED) {
        state = GameState::ACTIVE;
        std::cout << "Game resumed" << std::endl;
    }
}

// 结束游戏
void GameMode::end() {
    state = GameState::GAME_OVER;
    std::cout << "Game over! Final score: " << score << std::endl;
}

// 获取游戏状态
GameState GameMode::getState() const {
    return state;
}

float GameMode::getScore() const {
    return score;
}

int GameMode::getLives() const {
    return lives;
}

// 检查游戏是否激活
bool GameMode::isActive() const {
    return state == GameState::ACTIVE;
}
