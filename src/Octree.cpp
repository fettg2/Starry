// Octree.cpp
#include "Octree.h"
#include "Scene.h"

// 物理常量
const double G = 6.6743e-11; // m^3 kg^-1 s^-2

// OctreeNode构造函数
OctreeNode::OctreeNode(const glm::vec3& min, const glm::vec3& max) {
    minBound = min;//左下后顶点
    maxBound = max;//右上前顶点
    isLeaf = true;//末尾叶节点
    totalMass = 0;
    centerOfMass = glm::vec3(0, 0, 0);
    for (int i = 0; i < 8; ++i) {
        children[i] = nullptr;
    }
}

// OctreeNode析构函数
OctreeNode::~OctreeNode() {
    if (!isLeaf) {
        for (int i = 0; i < 8; ++i) {
            if (children[i]) {
                delete children[i];
            }
        }
    }
}

bool OctreeNode::contains(const glm::vec3& point) {
    return point.x >= minBound.x && point.x <= maxBound.x &&
           point.y >= minBound.y && point.y <= maxBound.y &&
           point.z >= minBound.z && point.z <= maxBound.z;
}

// 空间重分配
void OctreeNode::split() {
    glm::vec3 center = (minBound + maxBound) * 0.5f;
    //创建八个子节点
    children[0] = new OctreeNode(minBound, center);//左下后
    children[1] = new OctreeNode(glm::vec3(center.x, minBound.y, minBound.z), glm::vec3(maxBound.x, center.y, center.z));//右下后
    children[2] = new OctreeNode(glm::vec3(minBound.x, center.y, minBound.z), glm::vec3(center.x, maxBound.y, center.z));//左上后
    children[3] = new OctreeNode(glm::vec3(center.x, center.y, minBound.z), glm::vec3(maxBound.x, maxBound.y, center.z));//右上后
    children[4] = new OctreeNode(glm::vec3(minBound.x, minBound.y, center.z), glm::vec3(center.x, center.y, maxBound.z));//左下前
    children[5] = new OctreeNode(glm::vec3(center.x, minBound.y, center.z), glm::vec3(maxBound.x, center.y, maxBound.z));//右下前
    children[6] = new OctreeNode(glm::vec3(minBound.x, center.y, center.z), glm::vec3(center.x, maxBound.y, maxBound.z));//左上前
    children[7] = new OctreeNode(glm::vec3(center.x, center.y, center.z), maxBound);//右上前 

    for (Object* obj : objects) {
        for (int i = 0; i < 8; ++i) {
            if (children[i]->contains(obj->position)) {
                children[i]->insert(obj);//将物体指针 obj 插入到第 i 个子节点中
                break;
            }
        }
    }

    objects.clear();
    isLeaf = false;
}

// 插入天体
void OctreeNode::insert(Object* obj) {
    if (!contains(obj->position)) {
        return;
    }

    if (isLeaf) {
        objects.push_back(obj);
        updateCenterOfMass();
        if (objects.size() > MAX_OBJECTS) {
            split();
        }
    } else {
        for (int i = 0; i < 8; ++i) {
            if (children[i]->contains(obj->position)) {
                children[i]->insert(obj);//将天体递归插入到叶子节点
                break;
            }
        }
        updateCenterOfMass();
    }
}

// 更新质心
void OctreeNode::updateCenterOfMass() {
    if (isLeaf) {
        totalMass = 0;
        centerOfMass = glm::vec3(0, 0, 0);
        for (Object* obj : objects) {
            totalMass += obj->mass;
            centerOfMass += obj->position * obj->mass;
        }
        if (totalMass > 0) {
            centerOfMass /= totalMass;
        }
    } else {
        totalMass = 0;
        centerOfMass = glm::vec3(0, 0, 0);
        for (int i = 0; i < 8; ++i) {
            if (children[i]) {
                children[i]->updateCenterOfMass();//确保区块（父节点）内子节点的质心已计算
                totalMass += children[i]->totalMass;
                centerOfMass += children[i]->centerOfMass * children[i]->totalMass;
            }
        }
        if (totalMass > 0) {
            centerOfMass /= totalMass;
        }
    }
}

// 计算引力
void OctreeNode::calculateForce(Object* obj, float& forceX, float& forceY, float& forceZ) {
    if (totalMass == 0) {
        return;
    }

    glm::vec3 distanceVec = centerOfMass - obj->position;
    float distance = glm::length(distanceVec);

    if (distance < 0.1f) {
        return;
    }

    if (isLeaf) {
        for (Object* other : objects) {
            if (other != obj) {
                glm::vec3 objDistance = other->position - obj->position;
                float objDist = glm::length(objDistance);
                if (objDist > 0.1f) {
                    float Gforce = (G * obj->mass * other->mass) / (objDist * objDist * 1000000.0f);
                    glm::vec3 direction = glm::normalize(objDistance);
                    forceX += direction.x * Gforce;//x方向向量
                    forceY += direction.y * Gforce;//y方向向量
                    forceZ += direction.z * Gforce;//z方向向量
                }
            }
        }
    } else {
        float nodeSize = glm::length(maxBound - minBound);
        //比例小于0.5：天体距离较远，用质心近似计算引力
        if (nodeSize / distance < 0.5f) {
            float Gforce = (G * obj->mass * totalMass) / (distance * distance * 1000000.0f);
            glm::vec3 direction = glm::normalize(distanceVec);
            forceX += direction.x * Gforce;
            forceY += direction.y * Gforce;
            forceZ += direction.z * Gforce;
        } else {
            //递归到子节点，部分计算引力
            for (int i = 0; i < 8; ++i) {
                if (children[i]) {
                    children[i]->calculateForce(obj, forceX, forceY, forceZ);
                }
            }
        }
    }
}

// 递归检测可能的碰撞，potentialCollisions在octree中
void OctreeNode::checkCollisions(Object* obj, std::vector<Object*>& potentialCollisions) {
    if (isLeaf) {
        for (Object* other : objects) {
            if (other != obj) {
                potentialCollisions.push_back(other);
            }
        }
    } else {
        for (int i = 0; i < 8; ++i) {
            if (children[i]) {
                children[i]->checkCollisions(obj, potentialCollisions);
            }
        }
    }
}

// Octree构造函数
Octree::Octree(const glm::vec3& minBound, const glm::vec3& maxBound) {
    root = new OctreeNode(minBound, maxBound);
}

// Octree析构函数
Octree::~Octree() {
    delete root;
}

void Octree::build(const std::vector<Object>& objects) {
    delete root;
    glm::vec3 minBound = glm::vec3(-20000, -20000, -20000);
    glm::vec3 maxBound = glm::vec3(20000, 20000, 20000);
    root = new OctreeNode(minBound, maxBound);

    for (const auto& obj : objects) {
        if (!obj.Initalizing) {
            root->insert(const_cast<Object*>(&obj));
        }
    }

    root->updateCenterOfMass();
}

// 计算加速度，更新速度
void Octree::calculateForces(std::vector<Object>& objects) {
    for (size_t i = 0; i < objects.size(); i++) {
        auto& obj = objects[i];
        if (!obj.Initalizing && i != 0) { // 跳过第一个天体（中心天体）
            float forceX = 0, forceY = 0, forceZ = 0;
            root->calculateForce(&obj, forceX, forceY, forceZ);//计算引力
            
            float accX = forceX / obj.mass;
            float accY = forceY / obj.mass;
            float accZ = forceZ / obj.mass;
            obj.accelerate(accX, accY, accZ);
        }
    }
}

// 检测碰撞，更新速度
void Octree::checkCollisions(std::vector<Object>& objects) {
    for (size_t i = 0; i < objects.size(); i++) {
        auto& obj = objects[i];
        if (!obj.Initalizing && i != 0) { // 跳过第一个天体（中心天体）
            std::vector<Object*> potentialCollisions;
            root->checkCollisions(&obj, potentialCollisions);
            for (Object* other : potentialCollisions) {
                obj.CheckCollision(*other);
            }
        }
    }
}
