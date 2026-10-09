# Editable workouts

Each `.json` file here defines one workout. The files are the source of truth for both the C runner and the tablet’s workout picker. No changes to HTML, command handlers, or timing code are needed to add, rename, reorder, or replace workouts.

## Example

```json
{
  "id": "gentle-climb",
  "name": "Gentle Climb",
  "description": "An easy opening, a longer climb, and a cool-down.",
  "default_duration_minutes": 12,
  "default_intensity_percent": 100,
  "segments": [
    { "weight": 1, "speed_mps": 1, "grade_percent": 0 },
    { "weight": 2, "speed_mps": 2, "grade_percent": 3 },
    { "weight": 1, "speed_mps": 1, "grade_percent": 0 }
  ]
}
```

At 12 minutes, weights `1, 2, 1` produce segments lasting 3, 6, and 3 minutes. Changing duration scales the whole workout. Intensity scales both speed and incline; 75% means multiplying each target by 0.75. The tablet displays speeds in the selected km/h or mph unit; definitions use m/s. Warm-up and cool-down are ordinary segments explicitly included by the author.

## Add or replace a workout

1. Copy a JSON file and edit it. IDs must be unique lowercase slugs, up to 24 characters, starting with a letter; `manual` is reserved. Filenames use letters, digits, hyphens, and underscores followed by `.json`.
2. Set the name, description, defaults, and segments. Files sort by filename, so numeric prefixes control button order. The ID is independent of filename/order.
3. From the repository root run `node tools/generate-workouts.cjs` (Node built-ins only).
4. Rebuild the firmware and flash it, or recompile and restart the [local preview](../../preview/README.md). Refresh the tablet page.
5. Commit JSON changes together with `firmware/components/test_session/workouts.generated.h` and `workouts.check.cmake`.

Delete a file to remove a workout. An empty directory leaves Manual available. Generated files should never be edited by hand. The firmware build checks the JSON file list and content hashes; CI also checks that generated data exactly matches the definitions.

## Limits and behavior

- Up to 32 workouts, each with 1–64 segments.
- Default duration: 1–120 whole minutes. Default intensity: 50–150 percent.
- Each weight: 1–1000, whole numbers; total weight at most 10000.
- Names: 1–60 characters; descriptions: up to 240 characters.
- Finite, nonnegative speed and grade. All targets must fit the current **software-only** 5 m/s and 10% limits even at 150% intensity (base speed at most 5/1.5 m/s and grade at most 10/1.5%). The C runner also validates the entire selected workout against its configured limits before starting.
- Unknown fields are rejected to catch misspellings.

The page fetches `/api/workouts` from the controller. That JSON response and the controller’s C tables come from the same generated catalog. Files are not downloaded from GitHub during a workout, so the controller works offline. The JSON format is data only: it cannot change Stop, fault handling, ownership, or connection timeout behavior.

Manual adjustments last until the next segment. Stop cancels a workout, loss of tablet heartbeats faults it, completion clears requests, and none of these events automatically resumes it. Physical outputs remain disabled; these profiles do not define acceleration limits, lift handling, or commissioning values for the eventual machine.

## Breckenridge trail-inspired examples

The Gold Digger, Beaver Meadows, and Heaven’s Gate examples use names and published distances from the [Breckenridge Nordic Center trail map](https://www.breckenridgenordic.com/wp-content/uploads/2023/07/2022-Nordic-Center-Map-02-1-Edited.pdf), linked by the center’s [trail reports page](https://www.breckenridgenordic.com/trail-reports-in-breckenridge/). The map lists 1 km, 4 km, and 5 km respectively. These are the map’s trail distances, **not the distance the timed workout will cover**. The trail report page lists some different lengths; no attempt has been made to reconcile route variants.

The speed targets, incline sequences, relative segment lengths, and default durations (10, 30, and 40 minutes) are authored examples for local UX exploration, not measurements or recommendations from the Nordic Center. No measured elevation profile was obtained. These sessions are explicitly labeled trail-inspired with estimated inclines in their JSON descriptions. They do not reproduce downhill grades, route geometry, altitude, or actual elevation gain. Replace the estimated segments when suitable route elevation data is available. Original maps and photos are not bundled with these definitions.
