#include "editor/ObjectLinking.h"
#include "model/Transform.h"

#include <algorithm>
#include <cstdio>

std::vector<LinkPreview> computeLinkPreviews(const Scene& scene) {
    std::vector<LinkPreview> previews;
    for (const auto& [fromId, toId] : scene.objectLinks) {
        const SceneObject* from = nullptr;
        const SceneObject* to = nullptr;
        for (const auto& object : scene.objects) {
            if (object.id == fromId) from = &object;
            if (object.id == toId) to = &object;
        }
        if (!from || !to || from->paths.empty() || to->paths.empty()) continue;

        LinkPreview preview;
        preview.fromObjectId = fromId;
        preview.toObjectId = toId;
        preview.worldFrom = applyTransform(from->transform, from->paths.back().to);
        preview.worldTo = applyTransform(to->transform, to->paths.front().from);
        previews.push_back(preview);
    }
    return previews;
}

bool bakeLinkToTravel(Scene& scene, int fromObjectId, int toObjectId) {
    SceneObject* from = scene.findObject(fromObjectId);
    SceneObject* to = scene.findObject(toObjectId);
    if (!from || !to || from->paths.empty() || to->paths.empty()) return false;

    glm::dvec3 worldTo = applyTransform(to->transform, to->paths.front().from);
    glm::dvec3 localTo = inverseApplyTransform(from->transform, worldTo);
    glm::dvec3 localFrom = from->paths.back().to; // already local space -- the from-object's own last point

    // Insert the new line right after the from-object's current last
    // path's line. That path's srcLine is, by definition, the highest
    // srcLine among any of this object's paths (they're stored/parsed in
    // file order) -- so no OTHER path's srcLine needs shifting because of
    // this insertion, only whatever non-path trailing lines (comments,
    // "END", ...) come after it, which nothing indexes by line number.
    int insertAt = from->paths.back().srcLine + 1;
    if (insertAt < 0 || insertAt > static_cast<int>(from->sourceLines.size())) {
        insertAt = static_cast<int>(from->sourceLines.size());
    }

    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "LIN {X %.3f,Y %.3f,Z %.3f} ; GCODEFORGE BAKED LINK",
                  localTo.x, localTo.y, localTo.z);
    from->sourceLines.insert(from->sourceLines.begin() + insertAt, buffer);

    int nextNumber = 0;
    for (const auto& p : from->paths) nextNumber = std::max(nextNumber, p.number);
    ++nextNumber;

    Path bakedTravel;
    bakedTravel.number = nextNumber;
    bakedTravel.from = localFrom;
    bakedTravel.to = localTo;
    bakedTravel.type = PathType::Travel;
    bakedTravel.layer = -1;
    bakedTravel.motion = "LIN";
    bakedTravel.srcLine = insertAt;
    from->paths.push_back(bakedTravel);

    scene.objectLinks.erase({fromObjectId, toObjectId});
    return true;
}
