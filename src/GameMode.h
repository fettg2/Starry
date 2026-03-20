// GameMode.h
#ifndef GAME_MODE_H
#define GAME_MODE_H

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include "Scene.h"

// 游戏模式状态枚举
enum class GameState {
    IDLE,
    ACTIVE,
    PAUSED,
    GAME_OVER
};

// 游戏模式类前向声明
class GameMode;

// 全局游戏模式实例
extern GameMode gGameMode;

// 游戏模式类
class GameMode {
private:
    GameState state;
    float score;
    int lives;
    float timeElapsed;
    bool gameStarted;
    
    // 游戏目标
    struct GameObjective {
        std::string description;
        bool completed;
        int reward;
        
        GameObjective(const std::string& desc, bool comp, int rew) :
            description(desc), completed(comp), reward(rew) {}
    };
    
    std::vector<GameObjective> objectives;
    
    // 内部方法
    void generateObjectives();
    void updateObjectives();
    
public:
    GameMode();
    
    // 初始化游戏模式
    void init();
    
    // 更新游戏状态
    void update(float deltaTime, const std::vector<Object>& objs, const glm::vec3& shipPos, const glm::vec3& shipVel);
    
    // 渲染游戏HUD
    void renderHUD();
    
    // 处理碰撞事件
    void handleCollision(const Object& obj);
    
    // 开始/暂停/结束游戏
    void start();
    void pause();
    void resume();
    void end();
    
    // 获取游戏状态
    GameState getState() const;
    float getScore() const;
    int getLives() const;
    
    // 检查游戏是否激活
    bool isActive() const;
};

#endif // GAME_MODE_H
