#pragma once

#include "model/SceneObject.h"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

// The whole editing session: every imported object, plus which objects are
// linked to which (see docs/PLAN.md milestone 10 -- linking generates a
// travel connecting one object's end point to the next object's start
// point; baking that into permanent Path data comes later).
class Scene {
public:
    SceneObject& addObject(SceneObject object) {
        object.id = nextObjectId_++;
        objects.push_back(std::move(object));
        if (activeObjectId == 0) activeObjectId = objects.back().id;
        return objects.back();
    }

    SceneObject* findObject(int id) {
        auto it = std::find_if(objects.begin(), objects.end(),
                                [id](const SceneObject& o) { return o.id == id; });
        return it != objects.end() ? &(*it) : nullptr;
    }

    SceneObject* activeObject() { return findObject(activeObjectId); }

    void toggleLink(int fromObjectId, int toObjectId) {
        auto key = std::make_pair(fromObjectId, toObjectId);
        auto it = objectLinks.find(key);
        if (it != objectLinks.end()) {
            objectLinks.erase(it);
        } else {
            objectLinks.insert(key);
        }
    }

    std::vector<SceneObject> objects;
    std::set<std::pair<int, int>> objectLinks; // (fromObjectId, toObjectId)
    int activeObjectId = 0;

private:
    int nextObjectId_ = 1;
};
