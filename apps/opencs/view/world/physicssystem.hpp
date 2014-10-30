#ifndef CSV_WORLD_PHYSICSSYSTEM_H
#define CSV_WORLD_PHYSICSSYSTEM_H

#include <string>
#include <map>
#include <list>

namespace Ogre
{
    class Vector3;
    class Quaternion;
    class SceneManager;
    class Camera;
}

namespace OEngine
{
    namespace Physic
    {
        class PhysicEngine;
    }
}

namespace CSVWorld
{
    class PhysicsSystem
    {
            static PhysicsSystem *mPhysicsSystemInstance;
            std::map<std::string, std::string> mSceneNodeToRefId;
            std::map<std::string, std::map<Ogre::SceneManager *, std::string> > mRefIdToSceneNode;
            std::map<std::string, std::string> mSceneNodeToMesh;
            std::list<Ogre::SceneManager *> mSceneManagers; // FIXME: change to list per OEngine
            OEngine::Physic::PhysicEngine* mEngine;

            Ogre::SceneManager *mSceneMgr;

        public:

            PhysicsSystem();
            ~PhysicsSystem();

            static PhysicsSystem *instance();

            void addSceneManager(Ogre::SceneManager *sceneMgr);

            void addObject(const std::string &mesh,
                    const std::string &sceneNodeName, const std::string &referenceId, float scale,
                    const Ogre::Vector3 &position, const Ogre::Quaternion &rotation,
                    bool placeable=false);

            void removeObject(const std::string &sceneNodeName);

            void moveObject(const std::string &sceneNodeName,
                    const Ogre::Vector3 &position, const Ogre::Quaternion &rotation);

            void addHeightField(float* heights,
                    int x, int y, float yoffset, float triSize, float sqrtVerts);

            void removeHeightField(int x, int y);

            void toggleDebugRendering(Ogre::SceneManager *sceneMgr);

            // return the object's SceneNode name and position for the given SceneManager
            std::pair<std::string, Ogre::Vector3> castRay(float mouseX,
                    float mouseY, Ogre::SceneManager *sceneMgr, Ogre::Camera *camera);

            std::string sceneNodeToRefId(std::string sceneNodeName);
            std::string sceneNodeToMesh(std::string sceneNodeName);

        private:

            void updateSelectionHighlight(std::string sceneNode, const Ogre::Vector3 &position);
            std::string refIdToSceneNode(std::string referenceId, Ogre::SceneManager *sceneMgr);
    };
}

#endif // CSV_WORLD_PHYSICSSYSTEM_H
