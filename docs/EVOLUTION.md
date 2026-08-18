# Evolution & long-term vision

Written by the project owner — the history of how GCode Editor's design
decisions came about, and where the broader ecosystem (GCode Editor +
BedForge) is heading.

**Scope note:** sections 18–23 (BedForge, digital bed twin, flow/printability
prediction, material-behavior-compensated paths) describe a *separate*,
related project and long-term research direction. They are not part of
GcodeForge and nothing here should turn into a GcodeForge task on their own.
They're kept for context — GcodeForge's data model (path metadata, object
structure) should stay compatible with eventually feeding a tool like that,
but building it is out of scope. Sections 1–17 describe the core editor and
are the direct basis for `PLAN.md`. See the "architecture" note at the
bottom for the one diagram that matters most for this repo.

---

## 1. The Beginning — Turning Robot Code Into Geometry

The first major breakthrough was parsing the SRC program and reconstructing its movements visually.

Instead of seeing:

```text
$VEL.CP = 0.040
LIN {X 1240,Y 630,Z 82,...}
LIN {X 1265,Y 641,Z 82,...}
LIN {X 1291,Y 654,Z 82,...}
```

the operator sees the actual path inside a 3D viewport. This fundamentally changes how the program can be understood. A manufacturing technician no longer needs to mentally translate coordinates into shapes. The software does it.

The SRC becomes: robot code → structured data → visible geometry

## 2. Understanding the Program, Not Just Drawing Lines

Every path needed to remain connected to its manufacturing information. A visible segment represents much more than two points:

```text
PATH 1842

Layer        27
Type         Print
Motion       LIN
Start XYZ    ...
End XYZ      ...
Speed        0.041 m/s
Object       Chair_01
SRC Line     8237
```

GCode Editor isn't trying to become a generic 3D modeling program. It is creating a visual representation of the robot program itself. The relationship between geometry and source code remains intact.

## 3. Visual Speed Editing

Instead of searching through thousands of lines for speed commands, an operator can select the physical paths and modify their speed visually:

```text
Select these paths
        ↓
Speed = 0.032 m/s
        ↓
Viewport updates
        ↓
SRC compiler understands the override
```

KUKA continuous-path velocities such as those used by `LIN` movements are conventionally represented in meters per second through `$VEL.CP` or related BAS calls. GCode Editor understands speed as manufacturing information associated with geometry rather than merely another line of text.

Central idea: edit the manufacturing process by interacting with its geometry.

## 4. Selection Became a Production Tool

The software evolved from simple clicking toward a more complete selection system: individual path selection, rectangular selection, additive selection, subtractive selection, layer-based selection, path-range selection, isolation, touch-oriented selection, experimental lasso selection.

The purpose isn't to imitate Blender or 3ds Max. The purpose is to allow questions like "Select the problematic paths on this side of the print" or "Select everything in these layers and slow it down." Selection becomes a way of querying the manufacturing program.

## 5. Object-Level Editing

Groups of paths became actual printable objects. An imported SRC object could be moved (X/Y/Z ±50mm) and rotated around the print bed. Instead of modifying hundreds or thousands of coordinates manually, the operator moves the object and the system handles the underlying geometry.

## 6. Multiple Objects Changed Everything

The editor represents Object A, B, C, D inside one production environment, each retaining its own geometry, paths, layers, transforms, speeds, travels, and metadata — organizing an entire robotic production job rather than one isolated SRC file.

## 7. The Link System

How does the robot move from the end of one object to the beginning of another? GCode Editor introduced procedural links: the software identifies Object A's last printing point and generates a connection to Object B's first printing point, inspectable visually. The connection initially remains procedural — move Object B and the connection updates. Powerful, but introduced a performance problem.

## 8. Bake Links to Travels

Procedural connections became expensive with many linked objects. "Bake Links to Travels": once satisfied, the operator converts a dynamic link into actual travel geometry. After baking, the software no longer continuously recalculates the relationship — improving performance and predictability.

Philosophy: keep things procedural while they are being designed; bake them when they become production decisions.

## 9. Travel Editing Became Its Own System

A travel isn't simply an invisible movement. A poor travel can create dripping material, strings, collisions, unnecessary cycle time, movement across finished geometry, or awkward robot transitions. Print and travel paths are treated as fundamentally different classes of motion — opening the door to future automatic travel optimization.

## 10. Animation — Watching the Program Execute

An animation system lets the operator watch the program advance path by path. **Next Path by N**: instead of stepping 1,2,3,4,5,6..., the operator requests "Next Path By: 50" and inspects 1, 51, 101, 151, 201... — making very large programs much faster to review.

## 11. Saving the Entire Editing State

An SRC file alone isn't enough to represent an editing session. A GCode Editor project preserves original SRC files, object positions, speed overrides, links, baked travels, editor state, and production modifications. The SRC remains the robot program; the project becomes the editable production workspace.

## 12. The Performance Problem

Drawing every path at full quality continuously made navigation increasingly slow on large programs. Reducing everything permanently would destroy useful information. Instead, GCode Editor adopted an idea from real-time game engines: **Progressive Viewport Detail** — far away, a simplified object representation; medium distance, major path structure; close, detailed paths; very close, full production detail. Paths progressively appear as the operator approaches them.

## 13. Game-Engine Thinking Entered Manufacturing Software

A production editor doesn't have to process everything at maximum fidelity all the time. It can use level of detail, screen-space simplification, view culling, cached geometry, batched rendering, progressive loading, and selective high-detail rendering — just like a game engine. The manufacturing data remains precise; only its temporary visual representation changes.

## 14. iPad Changed the Interface

The Cloudflare-hosted version runs as a PWA installable on an iPad, carrying the editor onto the production floor. But touch introduced ambiguity: a finger gesture can mean navigation or selection. The interface evolved explicit modes — **Hand Mode** (manipulates the viewport) and **Selection Mode** (manipulates the toolpath) — making the same environment usable with mouse or touchscreen.

## 15. Web Edition and Studio Edition

**GCode Editor Web**: Cloudflare → Browser/PWA → iPad. Immediate access, touch interface, no installation, automatic deployment, production-floor portability.

**GCode Editor Studio**: standalone PC package for very large SRC files, offline operation, heavier editing, debugging, development, maximum viewport performance.

Both share the same fundamental editing engine — not two unrelated applications, but one core with two front ends (Web/PWA/iPad and Studio/PC).

## 16. The Export Problem Led to a Better Safety System

Originally, verification treated numerical differences too aggressively — a floating-point difference like `0.039999999` vs `0.040000000` could block export, even though it isn't equivalent to structural corruption. Validation evolved into severity levels:

**Hard failures** (block export): missing coordinates, invalid XYZ, broken program structure, unexpected path counts, NaN/Infinity, corrupted motion commands.

**Warnings** (reported, not blocking): speed rounding, formatting differences, small numerical tolerance differences.

Principle: validation should be strict about dangerous changes and tolerant about mathematically insignificant ones.

## 17. GCode Editor Is Becoming a Compiler

Architecturally, GCode Editor is gradually becoming a visual compiler for robotic manufacturing:

```text
                 KUKA SRC
                    |
                    v
                 PARSER
                    |
                    v
          STRUCTURED PATH MODEL
                    |
        +-----------+-----------+
        v           v           v
     Geometry     Speed       Metadata
        |           |           |
        +-----------+-----------+
                    v
             VISUAL EDITOR
                    |
        +-----------+-----------+
        v           v           v
     Transform     Link       Modify
        |           |           |
        +-----------+-----------+
                    v
               VALIDATOR
                    |
                    v
               SRC COMPILER
                    |
                    v
             ROBOT PROGRAM
```

That is substantially more powerful than editing text.

## 18–23. Beyond GcodeForge (context only, not in scope here)

BedForge imports robot toolpaths into Blender (converting mm → m: `1mm → 0.001m`) and combines them with measured bed elevation to compute nozzle gap (`TOOLPATH_Z - BED_Z`) per point. Longer term: predicting bead width from gap/speed/material against a target width, and eventually a Flow/Printability system predicting how molten material actually behaves after leaving the nozzle (gravity, cooling, orientation) versus the programmed path — potentially compensating the programmed path for expected sag. The emerging platform pipeline: Digital Design → BedForge (Bed Twin / Flow Model / Simulation) → Optimized Paths → GCode Editor (Layout / Travels / Speeds) → Validation → KUKA SRC → KUKA Robot → Physical Object.

GCode Editor handles production intent; BedForge handles physical prediction and optimization; the robot executes the result. A path eventually knows the bed underneath it, the predicted bead it will create, the material behavior around it, and whether the resulting movement is likely to manufacture the intended geometry — representing the complete robotic manufacturing process as an interactive, editable, and eventually predictive digital system.

---

## Architecture takeaway for GcodeForge

Section 17's pipeline is not just narrative — it's the module boundary
`src/` should follow:

- `parser/` — text → structured path model only, no rendering/editing knowledge
- `model/` — pure data (paths, objects, transforms, links), no knowledge of parsing or rendering
- `editor/` — mutates the model: transform, link, modify (milestones 3, 7, 10)
- `validator/` — checks the model before export, two-tier severity (milestone 9)
- `compiler/` — serializes the model back to SRC
- `render/` + `ui/` — camera, line renderer, LOD, ImGui panels — observe the model, don't own it

Kept close to the validator code later: hard failures are missing/invalid
coordinates, broken structure, unexpected path counts, NaN/Infinity, and
corrupted motion commands; warnings are speed rounding, formatting, and
small numeric tolerance differences.
