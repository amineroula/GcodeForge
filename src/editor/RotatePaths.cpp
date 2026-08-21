#include "editor/RotatePaths.h"

#include <cmath>

void rotateSelectedPaths(SceneObject& object, double angleDegrees) {
    if (object.selectedPaths.empty()) return;

    // Centroid of every endpoint touched by the selection -- the average
    // of each selected path's from/to, in local space. Using the
    // selection's own centroid (not the object's pivot) is what makes
    // "rotate the selected paths" mean "spin them in place" rather than
    // "swing them around wherever the object's origin happens to be".
    glm::dvec3 centroid(0.0);
    int count = 0;
    for (const auto& path : object.paths) {
        if (!object.selectedPaths.count(path.number)) continue;
        centroid += path.from;
        centroid += path.to;
        count += 2;
    }
    if (count == 0) return;
    centroid /= static_cast<double>(count);

    const double radians = angleDegrees * 3.14159265358979323846 / 180.0;
    const double c = std::cos(radians);
    const double s = std::sin(radians);

    auto rotatePoint = [&](glm::dvec3 p) {
        glm::dvec3 rel = p - centroid;
        double x = rel.x * c - rel.y * s;
        double y = rel.x * s + rel.y * c;
        return glm::dvec3(centroid.x + x, centroid.y + y, p.z); // Z untouched -- this is a Z-axis rotation
    };

    for (auto& path : object.paths) {
        if (!object.selectedPaths.count(path.number)) continue;
        path.from = rotatePoint(path.from);
        path.to = rotatePoint(path.to);
    }
}
