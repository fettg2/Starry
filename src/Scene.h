// Scene.h
#ifndef SCENE_H
#define SCENE_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <cmath>
#include "Octree.h"

// 声明外部需要的全局变量（在 gravity_sim.cpp 中定义）
extern float sizeRatio;

// 声明外部的辅助函数（在 gravity_sim.cpp 中实现）
void CreateVBOVAO(GLuint& VAO, GLuint& VBO, const float* vertices, size_t vertexCount);
glm::vec3 sphericalToCartesian(float r, float theta, float phi);

// 物体类
class Object {
public:
    GLuint VAO, VBO;
    glm::vec3 position = glm::vec3(400, 300, 0); // 物体位置
    glm::vec3 velocity = glm::vec3(0, 0, 0);     // 物体速度
    size_t vertexCount;
    glm::vec4 color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

    bool Initalizing = false;
    bool Launched = false;
    bool target = false;

    float mass;
    float density;  // kg / m^3  天体密度
    float radius;

    glm::vec3 LastPos = position;// 上一帧位置
    bool glow;

    Object(glm::vec3 initPosition, glm::vec3 initVelocity, float mass, float density = 3344, glm::vec4 color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), bool Glow = false) {   
        this->position = initPosition;
        this->velocity = initVelocity;
        this->mass = mass;
        this->density = density;
        this->radius = pow(((3 * this->mass/this->density)/(4 * 3.14159265359)), (1.0f/3.0f)) / sizeRatio;
        this->color = color;
        this->glow = Glow;
        
        // 生成顶点（以原点为中心）
        std::vector<float> vertices = Draw(); //接受所有顶点坐标
        vertexCount = vertices.size();

        CreateVBOVAO(VAO, VBO, vertices.data(), vertexCount);
    }

    std::vector<float> Draw() {
        std::vector<float> vertices;
        int stacks = 32; // 堆叠数
        int sectors = 32; // 扇区数

        // 以球体积分思想构造球体，theta为层数，phi为片圆，glm::pi<float>()返回pi
        for(float i = 0.0f; i <= stacks; ++i){
            float theta1 = (i / stacks) * glm::pi<float>();
            float theta2 = (i+1) / stacks * glm::pi<float>();
            for (float j = 0.0f; j < sectors; ++j){
                float phi1 = j / sectors * 2 * glm::pi<float>();
                float phi2 = (j+1) / sectors * 2 * glm::pi<float>();
                glm::vec3 v1 = sphericalToCartesian(this->radius, theta1, phi1);
                glm::vec3 v2 = sphericalToCartesian(this->radius, theta1, phi2);
                glm::vec3 v3 = sphericalToCartesian(this->radius, theta2, phi1);
                glm::vec3 v4 = sphericalToCartesian(this->radius, theta2, phi2);

                // 三角形 1: v1-v2-v3
                vertices.insert(vertices.end(), {v1.x, v1.y, v1.z}); //      /|
                vertices.insert(vertices.end(), {v2.x, v2.y, v2.z}); //     / |
                vertices.insert(vertices.end(), {v3.x, v3.y, v3.z}); //    /__|
                
                // 三角形 2: v2-v4-v3
                vertices.insert(vertices.end(), {v2.x, v2.y, v2.z});
                vertices.insert(vertices.end(), {v4.x, v4.y, v4.z});
                vertices.insert(vertices.end(), {v3.x, v3.y, v3.z});
            }   
        }
        return vertices;
    }
    
    // 更新物体位置
    void UpdatePos(){
        this->position[0] += this->velocity[0] / 94; //p = p + v·Δt
        this->position[1] += this->velocity[1] / 94;
        this->position[2] += this->velocity[2] / 94;
        this->radius = pow(((3 * this->mass/this->density)/(4 * 3.14159265359)), (1.0f/3.0f)) / sizeRatio;
    }
    
    void UpdateVertices() {
        // 以当前半径生成新的顶点
        std::vector<float> vertices = Draw();
        
        // 更新VBO与新的顶点数据
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    }
    
    glm::vec3 GetPos() const {
        return this->position;
    }
    
    void accelerate(float x, float y, float z){
        this->velocity[0] += x / 96; //v = v + a·Δt
        this->velocity[1] += y / 96;
        this->velocity[2] += z / 96;
    }
    
    float CheckCollision(Object& other) {
        float dx = other.position[0] - this->position[0]; //计算距离差
        float dy = other.position[1] - this->position[1];
        float dz = other.position[2] - this->position[2];
        float distance = std::pow(dx*dx + dy*dy + dz*dz, (1.0f/2.0f));
        if (other.radius + this->radius > distance){
            // 计算碰撞法线
            glm::vec3 normal(dx, dy, dz);
            if (distance > 0) {
                normal = glm::normalize(normal);
            }
            
            // 计算相对速度
            glm::vec3 relativeVelocity = this->velocity - other.velocity;
            
            // 计算相对速度在法线上的分量
            float velocityAlongNormal = glm::dot(relativeVelocity, normal);
            
            // 如果物体正在分离，不处理碰撞
            if (velocityAlongNormal > 0) {
                return 1.0f;
            }
            
            // 计算碰撞后的速度变化（完全弹性碰撞）
            float restitution = 0.8f; //  restitution系数
            float impulseScalar = -(1 + restitution) * velocityAlongNormal;
            impulseScalar /= (1/this->mass + 1/other.mass);
            
            // 应用冲量到两个物体
            glm::vec3 impulse = normal * impulseScalar;
            this->velocity += impulse / this->mass;
            other.velocity -= impulse / other.mass;
            
            // 防止物体重叠
            float overlap = (this->radius + other.radius) - distance;
            if (overlap > 0) {
                glm::vec3 separation = normal * (overlap * 0.5f);
                this->position -= separation;
                other.position += separation;
            }
            
            return 1.0f;
        }
        return 1.0f;
    }
};

// 声明初始化天体场景的函数
void InitCelestialBodies(std::vector<Object>& objs);

#endif // SCENE_H