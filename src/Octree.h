// Octree.h
#ifndef OCTREE_H
#define OCTREE_H

#include <vector>
#include <glm/glm.hpp>

// 前向声明Object类，避免循环依赖
class Object;

// 八叉树节点
class OctreeNode {
public:
    glm::vec3 minBound;
    glm::vec3 maxBound;
    std::vector<Object*> objects;//存储该八叉树节点内的所有物体指针
    OctreeNode* children[8];//指向OctreeNode类对象的数组指针
    bool isLeaf;
    glm::vec3 centerOfMass;
    float totalMass;
    const int MAX_OBJECTS = 8;
    
    //构造，初始化属性
    OctreeNode(const glm::vec3& min, const glm::vec3& max);
    
    //析构，删除子节点
    ~OctreeNode();

    bool contains(const glm::vec3& point);
    
    //空间重分配
    void split();
    
    //插入天体
    void insert(Object* obj);
    
    //更新质心
    void updateCenterOfMass();
    
    //计算引力
    void calculateForce(Object* obj, float& forceX, float& forceY, float& forceZ);
    
    //递归检测可能的碰撞，potentialCollisions在octree中
    void checkCollisions(Object* obj, std::vector<Object*>& potentialCollisions);
};

class Octree {
public:
    OctreeNode* root;

    Octree(const glm::vec3& minBound, const glm::vec3& maxBound);

    ~Octree();

    void build(const std::vector<Object>& objects);
    
    //计算加速度，更新速度
    void calculateForces(std::vector<Object>& objects);
    
    //检测碰撞，更新速度
    void checkCollisions(std::vector<Object>& objects);
};

#endif // OCTREE_H
