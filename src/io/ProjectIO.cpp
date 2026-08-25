#include "io/ProjectIO.h"

#include <fstream>
#include <sstream>

namespace {

constexpr int kFormatVersion = 1;

// Source lines and names can contain spaces, so they can't be read back
// with a plain `>>`. Everything after the key on those records is taken
// verbatim to end-of-line instead.
std::string restOfLine(std::istringstream& stream) {
    std::string rest;
    std::getline(stream, rest);
    if (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
    return rest;
}

void writeOptional(std::ofstream& file, const char* key, const std::optional<double>& value) {
    if (value.has_value()) file << key << " " << *value << "\n";
}

} // namespace

bool saveProject(const std::string& path, const ProjectData& project) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    file << "GCODEFORGE_PROJECT " << kFormatVersion << "\n";

    file << "colorMode " << static_cast<int>(project.colorMode) << "\n";
    file << "renderMode " << static_cast<int>(project.render.mode) << "\n";
    file << "selectionStyle " << static_cast<int>(project.render.selectionStyle) << "\n";
    file << "beadWidth " << project.render.beadWidthMm << "\n";
    file << "beadHeight " << project.render.beadHeightMm << "\n";
    file << "backfaceCulling " << (project.render.backfaceCulling ? 1 : 0) << "\n";
    file << "selectBackfacing " << (project.render.selectBackfacing ? 1 : 0) << "\n";
    file << "showPrintPaths " << (project.render.showPrintPaths ? 1 : 0) << "\n";
    file << "showTravels " << (project.render.showTravels ? 1 : 0) << "\n";
    file << "showStartPoint " << (project.render.showStartPoint ? 1 : 0) << "\n";

    file << "bedWidth " << project.bed.widthMm << "\n";
    file << "bedDepth " << project.bed.depthMm << "\n";
    file << "bedOriginX " << project.bed.originXMm << "\n";
    file << "bedOriginY " << project.bed.originYMm << "\n";
    file << "bedOriginZ " << project.bed.originZMm << "\n";
    file << "bedGridSpacing " << project.bed.gridSpacingMm << "\n";
    file << "bedShowGrid " << (project.bed.showGrid ? 1 : 0) << "\n";
    file << "safePointMeasured " << (project.bed.safePointMeasured ? 1 : 0) << "\n";
    file << "safePointX " << project.bed.safePointXMm << "\n";
    file << "safePointY " << project.bed.safePointYMm << "\n";
    file << "safePointZ " << project.bed.safePointZMm << "\n";

    file << "heightmapVisible " << (project.heightmap.visible ? 1 : 0) << "\n";
    file << "heightmapCols " << project.heightmap.cols << "\n";
    file << "heightmapRows " << project.heightmap.rows << "\n";
    for (float z : project.heightmap.elevationsMm) file << "heightmapValue " << z << "\n";

    for (const auto& light : project.lighting.lights) {
        file << "light " << light.direction.x << " " << light.direction.y << " " << light.direction.z
             << " " << light.color.r << " " << light.color.g << " " << light.color.b
             << " " << (light.enabled ? 1 : 0) << "\n";
    }

    for (const auto& link : project.scene.objectLinks) {
        file << "link " << link.first << " " << link.second << "\n";
    }
    file << "activeObjectId " << project.scene.activeObjectId << "\n";

    for (const auto& object : project.scene.objects) {
        file << "OBJECT " << object.id << "\n";
        file << "name " << object.name << "\n";
        file << "visible " << (object.visible ? 1 : 0) << "\n";
        file << "color " << object.color.r << " " << object.color.g << " " << object.color.b << "\n";
        file << "transform " << object.transform.x << " " << object.transform.y << " " << object.transform.z
             << " " << object.transform.rotZDegrees
             << " " << (object.transform.flipX ? 1 : 0) << " " << (object.transform.flipY ? 1 : 0) << "\n";

        // Source lines must survive verbatim: SrcExporter patches THESE,
        // and anything it doesn't understand (E1-E6, C_VEL, interrupt
        // logic, comments) is preserved only because it's copied
        // untouched. Losing them would silently break export fidelity.
        file << "sourceLineCount " << object.sourceLines.size() << "\n";
        for (const auto& line : object.sourceLines) file << "srcline " << line << "\n";

        for (const auto& p : object.paths) {
            file << "path " << p.number
                 << " " << p.from.x << " " << p.from.y << " " << p.from.z
                 << " " << p.to.x << " " << p.to.y << " " << p.to.z
                 << " " << (p.type == PathType::Print ? 1 : 0)
                 << " " << p.layer
                 << " " << p.srcLine
                 << " " << p.cloneTemplateSrcLine
                 << " " << p.motion << "\n";
            writeOptional(file, "pathSpeed", p.speed);
            writeOptional(file, "pathSpeedOverride", p.speedOverride);
            writeOptional(file, "pathA", p.a);
            writeOptional(file, "pathB", p.b);
            writeOptional(file, "pathC", p.c);
        }

        for (const auto& layer : object.layers) {
            file << "layer " << layer.layer << " " << layer.z << " " << layer.startPath << " " << layer.endPath << "\n";
        }

        for (int selected : object.selectedPaths) file << "selected " << selected << "\n";
        for (int hidden : object.hiddenPaths) file << "hidden " << hidden << "\n";

        for (const auto& group : object.selectionGroups) {
            file << "groupBegin " << group.color.r << " " << group.color.g << " " << group.color.b << "\n";
            file << "groupId " << group.id << "\n";
            file << "groupName " << group.name << "\n";
            for (int n : group.pathNumbers) file << "groupPath " << n << "\n";
            file << "groupEnd\n";
        }

        for (const auto& action : object.layerActions) {
            file << "actionLayer " << action.layer << "\n";
            file << "actionLabel " << action.label << "\n";
            file << "actionText " << action.krlText << "\n";
        }

        if (object.startPoint.present) {
            const auto& sp = object.startPoint;
            file << "startPoint " << sp.srcLine << " " << (sp.jointSpace ? 1 : 0)
                 << " " << sp.joints.a1 << " " << sp.joints.a2 << " " << sp.joints.a3
                 << " " << sp.joints.a4 << " " << sp.joints.a5 << " " << sp.joints.a6
                 << " " << (sp.movedByOperator ? 1 : 0) << "\n";
            if (sp.position.has_value()) {
                file << "startPointPos " << sp.position->x << " " << sp.position->y << " " << sp.position->z << "\n";
            }
        }

        file << "ENDOBJECT\n";
    }

    return true;
}

bool loadProject(const std::string& path, ProjectData& project) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    // Build into locals and only commit on success, so a truncated or
    // malformed file can't leave the caller with half a session.
    ProjectData result;
    result.scene.objects.clear();
    // BedHeightmap default-constructs with cols*rows zeros (that default
    // initializer is what fixed an out-of-bounds crash -- see
    // model/BedHeightmap.h). Here it works against us: heightmapValue
    // records are push_back'd, so without clearing first they'd append to
    // the 50 pre-existing zeros, the count wouldn't match cols*rows, and
    // the consistency check below would replace the lot with zeros --
    // silently discarding every measurement in the file.
    result.heightmap.elevationsMm.clear();

    std::string line;
    if (!std::getline(file, line)) return false;
    {
        std::istringstream header(line);
        std::string magic;
        int version = 0;
        if (!(header >> magic >> version) || magic != "GCODEFORGE_PROJECT") return false;
        if (version > kFormatVersion) return false; // written by a newer build -- refuse rather than misread
    }

    SceneObject current;
    bool inObject = false;
    int pendingObjectId = 0;
    SelectionGroup pendingGroup;
    bool inGroup = false;
    int highestObjectId = 0;
    // LightingSettings default-constructs with one light. The first saved
    // light must REPLACE that default rather than stack on top of it, or
    // every load/save cycle would silently gain a light.
    bool sawAnyLight = false;

    while (std::getline(file, line)) {
        std::istringstream s(line);
        std::string key;
        if (!(s >> key)) continue;

        if (key == "OBJECT") {
            current = SceneObject{};
            current.sourceLines.clear();
            inObject = true;
            s >> pendingObjectId;
            current.id = pendingObjectId;
            highestObjectId = std::max(highestObjectId, pendingObjectId);
            continue;
        }
        if (key == "ENDOBJECT") {
            if (inObject) result.scene.objects.push_back(current);
            inObject = false;
            continue;
        }

        if (inObject) {
            if (key == "name") current.name = restOfLine(s);
            else if (key == "visible") { int v = 1; s >> v; current.visible = (v != 0); }
            else if (key == "color") s >> current.color.r >> current.color.g >> current.color.b;
            else if (key == "transform") {
                int fx = 0, fy = 0;
                s >> current.transform.x >> current.transform.y >> current.transform.z
                  >> current.transform.rotZDegrees >> fx >> fy;
                current.transform.flipX = (fx != 0);
                current.transform.flipY = (fy != 0);
            }
            else if (key == "srcline") current.sourceLines.push_back(restOfLine(s));
            else if (key == "path") {
                Path p;
                int isPrint = 0;
                s >> p.number >> p.from.x >> p.from.y >> p.from.z >> p.to.x >> p.to.y >> p.to.z
                  >> isPrint >> p.layer >> p.srcLine >> p.cloneTemplateSrcLine >> p.motion;
                p.type = isPrint ? PathType::Print : PathType::Travel;
                current.paths.push_back(p);
            }
            else if (key == "pathSpeed" && !current.paths.empty()) { double v; if (s >> v) current.paths.back().speed = v; }
            else if (key == "pathSpeedOverride" && !current.paths.empty()) { double v; if (s >> v) current.paths.back().speedOverride = v; }
            else if (key == "pathA" && !current.paths.empty()) { double v; if (s >> v) current.paths.back().a = v; }
            else if (key == "pathB" && !current.paths.empty()) { double v; if (s >> v) current.paths.back().b = v; }
            else if (key == "pathC" && !current.paths.empty()) { double v; if (s >> v) current.paths.back().c = v; }
            else if (key == "layer") {
                Layer l;
                s >> l.layer >> l.z >> l.startPath >> l.endPath;
                current.layers.push_back(l);
            }
            else if (key == "selected") { int n; if (s >> n) current.selectedPaths.insert(n); }
            else if (key == "hidden") { int n; if (s >> n) current.hiddenPaths.insert(n); }
            else if (key == "groupBegin") {
                pendingGroup = SelectionGroup{};
                s >> pendingGroup.color.r >> pendingGroup.color.g >> pendingGroup.color.b;
                inGroup = true;
            }
            else if (key == "groupId" && inGroup) pendingGroup.id = restOfLine(s);
            else if (key == "groupName" && inGroup) pendingGroup.name = restOfLine(s);
            else if (key == "groupPath" && inGroup) { int n; if (s >> n) pendingGroup.pathNumbers.push_back(n); }
            else if (key == "groupEnd" && inGroup) { current.selectionGroups.push_back(pendingGroup); inGroup = false; }
            else if (key == "actionLayer") { LayerAction a; s >> a.layer; current.layerActions.push_back(a); }
            else if (key == "actionLabel" && !current.layerActions.empty()) current.layerActions.back().label = restOfLine(s);
            else if (key == "actionText" && !current.layerActions.empty()) current.layerActions.back().krlText = restOfLine(s);
            else if (key == "startPoint") {
                int isJoint = 1, moved = 0;
                auto& sp = current.startPoint;
                sp.present = true;
                s >> sp.srcLine >> isJoint >> sp.joints.a1 >> sp.joints.a2 >> sp.joints.a3
                  >> sp.joints.a4 >> sp.joints.a5 >> sp.joints.a6 >> moved;
                sp.jointSpace = (isJoint != 0);
                sp.movedByOperator = (moved != 0);
            }
            else if (key == "startPointPos") {
                glm::dvec3 p;
                if (s >> p.x >> p.y >> p.z) current.startPoint.position = p;
            }
            continue;
        }

        // Scene-level records
        int i = 0;
        double d = 0.0;
        if (key == "colorMode") { s >> i; result.colorMode = static_cast<ColorMode>(i); }
        else if (key == "renderMode") { s >> i; result.render.mode = static_cast<RenderMode>(i); }
        else if (key == "selectionStyle") { s >> i; result.render.selectionStyle = static_cast<SelectionStyle>(i); }
        else if (key == "beadWidth") { s >> d; result.render.beadWidthMm = static_cast<float>(d); }
        else if (key == "beadHeight") { s >> d; result.render.beadHeightMm = static_cast<float>(d); }
        else if (key == "backfaceCulling") { s >> i; result.render.backfaceCulling = (i != 0); }
        else if (key == "selectBackfacing") { s >> i; result.render.selectBackfacing = (i != 0); }
        else if (key == "showPrintPaths") { s >> i; result.render.showPrintPaths = (i != 0); }
        else if (key == "showTravels") { s >> i; result.render.showTravels = (i != 0); }
        else if (key == "showStartPoint") { s >> i; result.render.showStartPoint = (i != 0); }
        else if (key == "bedWidth") { s >> d; result.bed.widthMm = static_cast<float>(d); }
        else if (key == "bedDepth") { s >> d; result.bed.depthMm = static_cast<float>(d); }
        else if (key == "bedOriginX") { s >> d; result.bed.originXMm = static_cast<float>(d); }
        else if (key == "bedOriginY") { s >> d; result.bed.originYMm = static_cast<float>(d); }
        else if (key == "bedOriginZ") { s >> d; result.bed.originZMm = static_cast<float>(d); }
        else if (key == "bedGridSpacing") { s >> d; result.bed.gridSpacingMm = static_cast<float>(d); }
        else if (key == "bedShowGrid") { s >> i; result.bed.showGrid = (i != 0); }
        else if (key == "safePointMeasured") { s >> i; result.bed.safePointMeasured = (i != 0); }
        else if (key == "safePointX") { s >> d; result.bed.safePointXMm = static_cast<float>(d); }
        else if (key == "safePointY") { s >> d; result.bed.safePointYMm = static_cast<float>(d); }
        else if (key == "safePointZ") { s >> d; result.bed.safePointZMm = static_cast<float>(d); }
        else if (key == "heightmapVisible") { s >> i; result.heightmap.visible = (i != 0); }
        else if (key == "heightmapCols") { s >> i; result.heightmap.cols = i; }
        else if (key == "heightmapRows") { s >> i; result.heightmap.rows = i; }
        else if (key == "heightmapValue") {
            // Collected raw; sized against cols/rows after the whole file
            // is read, since the counts may appear in any order.
            if (s >> d) result.heightmap.elevationsMm.push_back(static_cast<float>(d));
        }
        else if (key == "light") {
            Light light;
            int enabled = 1;
            s >> light.direction.x >> light.direction.y >> light.direction.z
              >> light.color.r >> light.color.g >> light.color.b >> enabled;
            light.enabled = (enabled != 0);
            if (result.lighting.lights.size() == 1 && !sawAnyLight) {
                result.lighting.lights.clear(); // drop the default before adding saved ones
            }
            sawAnyLight = true;
            result.lighting.lights.push_back(light);
        }
        else if (key == "link") { int a = 0, b = 0; if (s >> a >> b) result.scene.objectLinks.insert({a, b}); }
        else if (key == "activeObjectId") { s >> i; result.scene.activeObjectId = i; }
    }

    // The heightmap's declared size wins: if the value count doesn't
    // match cols*rows the file is inconsistent, so fall back to a zeroed
    // grid of the declared size rather than indexing past the end later.
    size_t expected = static_cast<size_t>(std::max(result.heightmap.cols, 0)) *
                       static_cast<size_t>(std::max(result.heightmap.rows, 0));
    if (result.heightmap.cols < 2 || result.heightmap.rows < 2) {
        result.heightmap = BedHeightmap{};
    } else if (result.heightmap.elevationsMm.size() != expected) {
        result.heightmap.elevationsMm.assign(expected, 0.0f);
    }

    project = std::move(result);
    return true;
}
