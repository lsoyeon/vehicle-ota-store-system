from __future__ import annotations

from collections import deque
from typing import Any


class LKASFeature:
    def __init__(
        self,
        *,
        max_angle: float,
        angle_deadzone: float,
        smooth_frames: int,
    ) -> None:
        self.max_angle = max_angle
        self.angle_deadzone = angle_deadzone
        self._angle_history = deque(maxlen=smooth_frames)
        self._last_valid_angle = 0.0

    def update(self, *, joystick: Any, enabled: bool, context: dict[str, Any]) -> dict:
        if not enabled:
            self._angle_history.clear()
            self._last_valid_angle = 0.0
            return {
                "enabled": False,
                "mode": "manual",
                "value": {
                    "steer_byte": joystick.steer_byte,
                    "steer_angle": round(joystick.axis_steer * self.max_angle, 3),
                },
            }

        if joystick.gear != context["gear_d"]:
            return self._state("parked", context["steer_center_byte"], 0.0)

        if context["is_manual_steering"](joystick):
            return {
                "enabled": False,
                "mode": "manual override",
                "value": {
                    "disable_requested": True,
                    "steer_byte": joystick.steer_byte,
                    "steer_angle": round(joystick.axis_steer * self.max_angle, 3),
                },
            }

        self._angle_history.append(self._last_valid_angle)
        smooth_angle = (
            float(sum(self._angle_history) / len(self._angle_history))
            if self._angle_history
            else 0.0
        )
        if abs(smooth_angle) < self.angle_deadzone:
            smooth_angle = 0.0

        steer_byte = context["angle_to_byte"](smooth_angle, self.max_angle)
        return self._state("waiting for lane", steer_byte, smooth_angle)

    def _state(self, mode: str, steer_byte: int, steer_angle: float) -> dict:
        return {
            "enabled": True,
            "mode": mode,
            "value": {
                "steer_byte": steer_byte,
                "steer_angle": round(steer_angle, 3),
            },
        }
