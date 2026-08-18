# Product vision

Written by the project owner, kept verbatim as the source of truth for what
GcodeForge is actually for. Everything in `PLAN.md` should trace back to a
sentence in here.

---

**GCode Editor** is a browser-based and desktop-capable production tool developed for editing, visualizing, validating, and optimizing robotic additive-manufacturing toolpaths, with a particular focus on KUKA `.SRC` programs used in large-format extrusion workflows.

The software was created around a practical problem: traditional robot programs are difficult to understand and modify directly as text. A `.SRC` file may contain thousands of movements, speed commands, print paths, travel moves, layer transitions, and robot-specific parameters. GCode Editor converts that information into an interactive visual environment where the operator can see the actual geometry of the print, select individual paths, modify them, reorganize objects, create connections, inspect layers, and export the edited program back into robot-readable SRC code.

At the center of GCode Editor is an interactive viewport. Imported SRC movements are reconstructed as visible paths in 3D space, allowing the user to orbit, pan, zoom, inspect the print from different angles, isolate regions, and understand how the robot will move before the program reaches the machine. Print paths and travel paths are treated differently so that production motion can be analyzed clearly.

The viewport has been designed to work on both desktop computers and iPad. On touch devices, dedicated navigation and selection modes separate camera manipulation from geometry manipulation. Navigation mode supports touch-based orbit, pan, and zoom, while selection mode allows the user to interact directly with paths without the camera stealing the gesture. Desktop operation continues to support mouse and keyboard interaction.

A major part of the project is viewport performance. Large robotic print programs may contain tens or hundreds of thousands of path segments, so rendering every element at full detail continuously would make navigation increasingly slow. GCode Editor therefore uses an adaptive, game-style rendering system. Distant geometry is simplified, extremely small screen-space segments can be temporarily omitted, off-screen paths are culled, and detail progressively returns as the user approaches the object. Selected paths, active geometry, and important travel information can remain at higher detail. Rendering operations are also batched to reduce thousands of individual Canvas drawing operations.

This allows heavy SRC programs to remain responsive while still exposing full detail when the operator needs to inspect a specific area.

GCode Editor also treats the SRC program as structured production data rather than merely converting it into generic geometry. Individual paths retain information such as their path number, layer, motion type, print or travel classification, coordinates, effective speed, object association, and relationship to the original SRC program. This makes it possible to perform editing operations while maintaining a connection between what is visible in the viewport and what will ultimately be exported to the robot.

One of the main production features is **path speed editing**. Operators can select one or multiple print paths and modify their effective printing speeds visually instead of manually locating the corresponding commands inside a large SRC file. Speed changes are stored as overrides associated with the affected paths and are written back into the generated SRC during export.

The software includes multiple ways to select geometry. Individual paths can be clicked, groups can be selected, layer ranges can be isolated, and selection operations support replacement, addition, and subtraction. Touch-oriented selection tools have also been introduced for iPad operation, including dedicated selection controls and ongoing development of true freeform lasso selection.

Objects imported into the editor can be repositioned without manually rewriting robot coordinates. Transform controls allow translation in X, Y, and Z and rotation around Z, and quick production-oriented nudge controls allow movements such as ±50 mm along each axis. This is particularly useful when organizing multiple printed objects on a large robotic print bed.

GCode Editor supports multiple SRC objects in a single project. Objects can be imported independently, moved, arranged, inspected, and eventually connected into a larger production sequence.

A particularly important feature is the **object linking system**. When multiple print objects need to be produced in sequence, GCode Editor can generate a safe connection from the end of one object to the beginning of another. The operator can inspect and adjust this connection visually instead of manually creating travel commands.

Generated links are procedural during editing so their geometry can respond to object movement. Because early versions recalculated this geometry too frequently, link processing was optimized and cached to prevent procedural connections from slowing down the viewport.

Once a link has been approved, the user can use **Bake Links to Travels**. Baking converts the temporary procedural connection into actual travel geometry. The resulting travel becomes part of the editable production data rather than remaining a preview. This allows the operator to finalize connections before SRC export and prevents unnecessary procedural calculations during continued editing.

Travel editing is treated as an important production operation rather than a secondary visualization feature. The system distinguishes travel motion from extrusion motion and is designed around real problems such as long travel moves, unwanted crossings, dripping material, inefficient object sequencing, and dangerous or unnecessary robot movement.

The software also includes an animation system for reviewing the robot program sequentially. Paths can be played in order to visualize how the program progresses through the print. Operators can step through paths manually and use a configurable **Next Path by N** control to jump forward by a specified number of path segments during review. This is useful for navigating extremely long programs without having to inspect every movement individually.

Projects can be saved and reopened so that editing work does not have to be completed in one session. A GCode Editor project can preserve imported objects, transformations, speed modifications, links, baked travels, selection-related state, and other editing information. The dedicated project format is designed to let an operator return to a production program later without starting again from the original SRC.

SRC export is intentionally treated as a validation stage rather than a simple file download.

The editor performs checks before and during compilation to detect potentially corrupted output. Structural validation can detect problems such as malformed programs, incorrect motion counts, invalid coordinates, missing required SRC structure, and other conditions that should prevent a robot program from being trusted.

Speed verification is handled differently. Small numerical differences caused by floating-point representation or formatting were previously capable of blocking the entire export even when the intended and exported speeds were effectively identical. The current system therefore treats recognized speed-verification mismatches as **warnings rather than hard failures**. The SRC is still exported, the operator retains the speed edits, and a warning comment can be inserted into the generated program to preserve an audit trail.

Structural and coordinate corruption remain conditions that should be treated much more strictly.

This distinction reflects one of the core principles behind GCode Editor:

**production safety should stop genuinely dangerous or structurally invalid output without preventing useful work because of insignificant numerical differences.**

The project has also evolved into two deployment formats that share the same underlying editor.

**GCode Editor Web** runs through Cloudflare Pages and can be installed as a Progressive Web App on devices such as the iPad. This version is useful on the production floor because it requires no traditional software installation, can receive updates through deployment, and provides a touch-oriented interface.

**GCode Editor Studio** is the downloadable PC version. It packages the same runtime functionality—including viewport optimization and export-verification improvements—into a local bundle that can run from a Windows computer without relying on the Cloudflare service worker. This version is better suited for large programs, offline work, debugging, and heavier editing sessions.

Both versions are developed through GitHub with a controlled versioning workflow. New features and performance changes are developed on dedicated branches, tested independently, and then merged into the stable `main` branch after verification. Production builds can therefore remain usable while experimental features are developed separately.

Longer term, GCode Editor is intended to become the production-editing component of a larger robotic additive-manufacturing software ecosystem.

Its role is primarily:

**visualize → edit → organize → validate → export robot toolpaths.**

Other specialized tools can build around it. For example, BedForge is being developed as a Blender-based digital-twin and process-analysis environment that can combine measured print-bed geometry with SRC paths, analyze first-layer nozzle gaps, predict deposition behavior, and eventually optimize manufacturing parameters. BedForge can perform engineering analysis while GCode Editor remains the final production editing and validation environment.

Future GCode Editor development can include stronger coordinate-integrity verification, improved freeform lasso selection, more advanced travel editing, path-level profiling, bed-data integration, automated object sequencing, collision and clearance checks, richer path metadata, process presets, production statistics, and tighter integration with simulation tools.

The broader objective is not simply to make SRC files easier to edit.

The objective is to create a visual production environment where complex robotic additive-manufacturing programs can be understood, modified, optimized, and validated without forcing the operator to work directly through thousands of lines of robot code.

In that sense, GCode Editor turns the robot program from a text file into an interactive representation of the manufacturing process itself.
