import json
import math
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


WGS84_SEMI_MAJOR_AXIS_M = 6378137.0
WGS84_ECCENTRICITY_SQUARED = 6.69437999014e-3


def local_offset_m(target, point):
    latitude_rad = math.radians(target["latitude_deg"])
    sin_latitude = math.sin(latitude_rad)
    curvature = math.sqrt(1.0 - WGS84_ECCENTRICITY_SQUARED * sin_latitude * sin_latitude)
    prime_vertical_radius = WGS84_SEMI_MAJOR_AXIS_M / curvature
    meridian_radius = (
        WGS84_SEMI_MAJOR_AXIS_M
        * (1.0 - WGS84_ECCENTRICITY_SQUARED)
        / (curvature * curvature * curvature)
    )

    north_m = math.radians(point["latitude_deg"] - target["latitude_deg"]) * meridian_radius
    east_m = (
        math.radians(point["longitude_deg"] - target["longitude_deg"])
        * prime_vertical_radius
        * math.cos(latitude_rad)
    )
    return east_m, north_m


def load_mission_plan(json_path):
    with json_path.open("r", encoding="utf-8") as input_file:
        mission_plan = json.load(input_file)

    target = mission_plan["target"]
    waypoints = mission_plan["waypoints"]
    return target, waypoints


def default_output_path():
    platform_root = Path(__file__).resolve().parents[2]
    return platform_root / "results" / "mission_plan.png"


def plot_mission(target, waypoints, output_path):
    waypoint_offsets = [local_offset_m(target, waypoint) for waypoint in waypoints]
    east_values = [offset[0] for offset in waypoint_offsets]
    north_values = [offset[1] for offset in waypoint_offsets]

    output_path.parent.mkdir(parents=True, exist_ok=True)

    fig, ax = plt.subplots(figsize=(7, 7))
    ax.scatter([0.0], [0.0], marker="x", s=120, color="red", label="Smoke target", zorder=3)
    ax.scatter(east_values, north_values, s=70, color="tab:blue", label="Recheck waypoint", zorder=3)
    ax.plot(east_values, north_values, color="tab:blue", linewidth=1.6, zorder=2)

    for index, (east_m, north_m) in enumerate(waypoint_offsets, start=1):
        ax.annotate(
            f"WP{index}",
            (east_m, north_m),
            textcoords="offset points",
            xytext=(6, 6),
            fontsize=10,
        )

    ax.set_title("UAV Smoke Recheck Mission Plan")
    ax.set_xlabel("East offset (m)")
    ax.set_ylabel("North offset (m)")
    ax.set_aspect("equal", adjustable="box")
    ax.grid(True, linestyle="--", alpha=0.45)
    ax.legend()
    fig.tight_layout()
    fig.savefig(output_path, dpi=160)
    plt.close(fig)


def main():
    if len(sys.argv) != 2:
        print("Usage: python visualize_mission.py <mission_plan.json>", file=sys.stderr)
        return 1

    json_path = Path(sys.argv[1]).expanduser().resolve()
    if not json_path.is_file():
        print(f"Input JSON not found: {json_path}", file=sys.stderr)
        return 1

    target, waypoints = load_mission_plan(json_path)
    output_path = default_output_path()
    plot_mission(target, waypoints, output_path)

    print(f"Input JSON: {json_path}")
    print(f"Waypoint count: {len(waypoints)}")
    print(f"Output image: {output_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
