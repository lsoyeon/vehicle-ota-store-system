from __future__ import annotations

import json
import shutil
import threading
import time
import urllib.error
import urllib.request
import zlib
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


@dataclass(frozen=True)
class OtaPackageResult:
    feature_id: str
    action_id: str
    action_type: str
    target: str
    downloaded: bool
    applied: bool
    updated: bool
    path: Path | None
    version: str | None
    release_tag: str | None
    release_url: str | None
    asset_name: str | None
    downloaded_at: str | None
    checked_at: str | None
    error: str | None = None


class OtaAction:
    action_type = "base"

    def run(
        self,
        manager: "OtaManager",
        catalog_item: dict[str, Any],
        action: dict[str, Any],
        *,
        current_version: str | None,
        force: bool,
    ) -> OtaPackageResult:
        raise NotImplementedError


class GithubReleaseFileAction(OtaAction):
    action_type = "github_release_file"

    def run(
        self,
        manager: "OtaManager",
        catalog_item: dict[str, Any],
        action: dict[str, Any],
        *,
        current_version: str | None,
        force: bool,
    ) -> OtaPackageResult:
        feature_id = str(catalog_item["id"])
        action_id = str(action.get("id", "python_package"))
        target_name = str(action.get("target", "rpi"))
        checked_at = utc_now()
        download_file = str(action.get("download_file") or catalog_item["download_file"])
        target = manager.resolve_target_path(action, download_file)
        manager.update_progress(
            feature_id,
            action_id=action_id,
            action_type=self.action_type,
            target=target_name,
            phase="checking",
            status="checking",
            percent=0,
            message="최신 릴리즈 확인 중",
            active=True,
        )

        release = manager.fetch_latest_release(catalog_item, action)
        release_tag = str(release.get("tag_name") or "")
        version = clean_version(release_tag) or str(catalog_item.get("latest_version", "1.0.0"))
        release_url = release.get("html_url")

        if not force and target.exists() and clean_version(current_version) == version:
            manager.update_progress(
                feature_id,
                action_id=action_id,
                action_type=self.action_type,
                target=target_name,
                phase="complete",
                status="current",
                percent=100,
                message="최신 버전입니다",
                active=False,
            )
            return OtaPackageResult(
                feature_id=feature_id,
                action_id=action_id,
                action_type=self.action_type,
                target=target_name,
                downloaded=True,
                applied=False,
                updated=False,
                path=target,
                version=version,
                release_tag=release_tag,
                release_url=str(release_url) if release_url else None,
                asset_name=None,
                downloaded_at=None,
                checked_at=checked_at,
            )

        manager.update_progress(
            feature_id,
            action_id=action_id,
            action_type=self.action_type,
            target=target_name,
            phase="download",
            status="downloading",
            percent=0,
            message=f"{download_file} 다운로드 중",
            active=True,
        )
        module_bytes, asset_name = manager.download_release_asset(
            release,
            download_file,
            progress={
                "feature_id": feature_id,
                "action_id": action_id,
                "action_type": self.action_type,
                "target": target_name,
                "message": f"{download_file} 다운로드 중",
            },
        )
        target.parent.mkdir(parents=True, exist_ok=True)
        temp_target = target.with_suffix(target.suffix + ".tmp")
        temp_target.write_bytes(module_bytes)
        temp_target.replace(target)
        manager.update_progress(
            feature_id,
            action_id=action_id,
            action_type=self.action_type,
            target=target_name,
            phase="complete",
            status="complete",
            percent=100,
            message=f"{download_file} 다운로드 완료",
            active=False,
            bytes_downloaded=len(module_bytes),
            total_bytes=len(module_bytes),
        )
        return OtaPackageResult(
            feature_id=feature_id,
            action_id=action_id,
            action_type=self.action_type,
            target=target_name,
            downloaded=True,
            applied=False,
            updated=True,
            path=target,
            version=version,
            release_tag=release_tag,
            release_url=str(release_url) if release_url else None,
            asset_name=asset_name,
            downloaded_at=checked_at,
            checked_at=checked_at,
        )


class LocalFileAction(OtaAction):
    action_type = "local_file"

    def run(
        self,
        manager: "OtaManager",
        catalog_item: dict[str, Any],
        action: dict[str, Any],
        *,
        current_version: str | None,
        force: bool,
    ) -> OtaPackageResult:
        feature_id = str(catalog_item["id"])
        action_id = str(action.get("id", "local_file"))
        target_name = str(action.get("target", "rpi"))
        checked_at = utc_now()
        source = manager.resolve_source_path(action)
        download_file = str(action.get("download_file") or source.name)
        target = manager.resolve_target_path(action, download_file)
        version = str(action.get("version") or catalog_item.get("latest_version", "1.0.0"))

        manager.update_progress(
            feature_id,
            action_id=action_id,
            action_type=self.action_type,
            target=target_name,
            phase="download",
            status="copying",
            percent=0,
            message=f"{download_file} 설치 중",
            active=True,
        )
        if not source.exists():
            raise FileNotFoundError(f"local OTA source not found: {source}")

        if not force and target.exists() and clean_version(current_version) == clean_version(version):
            manager.update_progress(
                feature_id,
                action_id=action_id,
                action_type=self.action_type,
                target=target_name,
                phase="complete",
                status="current",
                percent=100,
                message="최신 버전입니다",
                active=False,
            )
            return OtaPackageResult(
                feature_id=feature_id,
                action_id=action_id,
                action_type=self.action_type,
                target=target_name,
                downloaded=True,
                applied=False,
                updated=False,
                path=target,
                version=version,
                release_tag=None,
                release_url=None,
                asset_name=source.name,
                downloaded_at=None,
                checked_at=checked_at,
            )

        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        manager.update_progress(
            feature_id,
            action_id=action_id,
            action_type=self.action_type,
            target=target_name,
            phase="complete",
            status="complete",
            percent=100,
            message=f"{download_file} 설치 완료",
            active=False,
            bytes_downloaded=target.stat().st_size,
            total_bytes=target.stat().st_size,
        )
        return OtaPackageResult(
            feature_id=feature_id,
            action_id=action_id,
            action_type=self.action_type,
            target=target_name,
            downloaded=True,
            applied=False,
            updated=True,
            path=target,
            version=version,
            release_tag=None,
            release_url=None,
            asset_name=source.name,
            downloaded_at=checked_at,
            checked_at=checked_at,
        )


class DoipUdsFlashAction(OtaAction):
    action_type = "doip_uds_flash"

    def run(
        self,
        manager: "OtaManager",
        catalog_item: dict[str, Any],
        action: dict[str, Any],
        *,
        current_version: str | None,
        force: bool,
    ) -> OtaPackageResult:
        feature_id = str(catalog_item["id"])
        action_id = str(action.get("id", "zcu_flash"))
        target_name = str(action.get("target", "zcu"))
        checked_at = utc_now()
        manager.update_progress(
            feature_id,
            action_id=action_id,
            action_type=self.action_type,
            target=target_name,
            phase="checking",
            status="checking",
            percent=0,
            message="ZCU 펌웨어 릴리즈 확인 중",
            active=True,
        )

        release = manager.fetch_latest_release(catalog_item, action)
        release_tag = str(release.get("tag_name") or "")
        version = clean_firmware_version(release_tag) or str(catalog_item.get("latest_version", "1.0.0"))
        release_url = release.get("html_url")
        download_file = manager.release_bin_asset_name(release, action.get("asset_name"))
        bin_path = manager.resolve_target_path(
            {**action, "target_dir": str(action.get("target_dir", "firmware"))},
            download_file,
        )

        if not force and bin_path.exists() and clean_firmware_version(current_version) == version:
            manager.update_progress(
                feature_id,
                action_id=action_id,
                action_type=self.action_type,
                target=target_name,
                phase="complete",
                status="current",
                percent=100,
                message="ZCU 펌웨어가 최신 버전입니다",
                active=False,
            )
            return OtaPackageResult(
                feature_id=feature_id,
                action_id=action_id,
                action_type=self.action_type,
                target=target_name,
                downloaded=True,
                applied=True,
                updated=False,
                path=bin_path,
                version=version,
                release_tag=release_tag,
                release_url=str(release_url) if release_url else None,
                asset_name=download_file,
                downloaded_at=None,
                checked_at=checked_at,
            )

        manager.update_progress(
            feature_id,
            action_id=action_id,
            action_type=self.action_type,
            target=target_name,
            phase="download",
            status="downloading",
            percent=0,
            message="ZCU 펌웨어 다운로드 중",
            active=True,
        )
        firmware_bytes, asset_name = manager.download_release_bin_asset(
            release,
            download_file,
            progress={
                "feature_id": feature_id,
                "action_id": action_id,
                "action_type": self.action_type,
                "target": target_name,
                "message": "ZCU 펌웨어 다운로드 중",
                "percent_start": 0,
                "percent_end": 50,
            },
        )
        bin_path.parent.mkdir(parents=True, exist_ok=True)
        temp_target = bin_path.with_suffix(bin_path.suffix + ".tmp")
        temp_target.write_bytes(firmware_bytes)
        temp_target.replace(bin_path)

        manager.update_progress(
            feature_id,
            action_id=action_id,
            action_type=self.action_type,
            target=target_name,
            phase="install",
            status="flashing",
            percent=50,
            message="ZCU 펌웨어 플래싱 중",
            active=True,
            bytes_downloaded=len(firmware_bytes),
            total_bytes=len(firmware_bytes),
        )
        success = manager.flash_bin_via_doip(
            bin_path,
            feature_id,
            action,
            progress={
                "feature_id": feature_id,
                "action_id": action_id,
                "action_type": self.action_type,
                "target": target_name,
                "message": "ZCU 펌웨어 플래싱 중",
                "percent_start": 50,
                "percent_end": 100,
            },
        )
        manager.update_progress(
            feature_id,
            action_id=action_id,
            action_type=self.action_type,
            target=target_name,
            phase="complete" if success else "error",
            status="complete" if success else "failed",
            percent=100 if success else None,
            message="ZCU 펌웨어 플래싱 완료" if success else "ZCU 펌웨어 플래싱 실패",
            active=False,
            bytes_downloaded=len(firmware_bytes),
            total_bytes=len(firmware_bytes),
        )
        return OtaPackageResult(
            feature_id=feature_id,
            action_id=action_id,
            action_type=self.action_type,
            target=target_name,
            downloaded=True,
            applied=success,
            updated=True,
            path=bin_path,
            version=version,
            release_tag=release_tag,
            release_url=str(release_url) if release_url else None,
            asset_name=asset_name,
            downloaded_at=checked_at,
            checked_at=checked_at,
            error=None if success else "DoIP/UDS flashing failed",
        )


def clean_version(tag: str | None) -> str | None:
    if not tag:
        return None
    value = str(tag).strip()
    return value[1:] if value.lower().startswith("v") and len(value) > 1 else value


def clean_firmware_version(tag: str | None) -> str | None:
    value = clean_version(tag)
    if not value:
        return None
    parts: list[str] = []
    for part in value.replace("-", ".").replace("_", ".").split("."):
        digits = "".join(ch for ch in part if ch.isdigit())
        if digits == "":
            break
        parts.append(str(int(digits)))
        if len(parts) == 3:
            break
    if not parts:
        return None
    while len(parts) < 3:
        parts.append("0")
    return ".".join(parts)


def release_version_tuple(tag: str | None) -> tuple[int, int, int] | None:
    value = clean_version(tag)
    if not value:
        return None
    parts: list[int] = []
    for part in value.replace("-", ".").replace("_", ".").split("."):
        digits = "".join(ch for ch in part if ch.isdigit())
        if digits == "":
            break
        parts.append(int(digits))
        if len(parts) == 3:
            break
    if len(parts) < 3:
        return None
    return parts[0], parts[1], parts[2]


class OtaManager:
    """Middle layer for downloadable vehicle software."""

    def __init__(
        self,
        *,
        base_dir: Path,
        downloaded_features_dir: Path,
        firmware_dir: Path | None = None,
        timeout_seconds: float = 10.0,
        flash_state_callback: Callable[[bool], None] | None = None,
    ) -> None:
        self.base_dir = base_dir
        self.downloaded_features_dir = downloaded_features_dir
        self.firmware_dir = firmware_dir or (base_dir / "firmware")
        self.timeout_seconds = timeout_seconds
        self._flash_state_callback = flash_state_callback
        self._flash_state_lock = threading.Lock()
        self._flash_state_depth = 0
        self._progress_lock = threading.Lock()
        self._progress: dict[str, dict[str, Any]] = {}
        self._actions: dict[str, OtaAction] = {
            LocalFileAction.action_type: LocalFileAction(),
            GithubReleaseFileAction.action_type: GithubReleaseFileAction(),
            DoipUdsFlashAction.action_type: DoipUdsFlashAction(),
        }

    def _enter_flash(self) -> None:
        callback = None
        with self._flash_state_lock:
            self._flash_state_depth += 1
            if self._flash_state_depth == 1:
                callback = self._flash_state_callback
        if callback is not None:
            callback(True)

    def _leave_flash(self) -> None:
        callback = None
        with self._flash_state_lock:
            if self._flash_state_depth > 0:
                self._flash_state_depth -= 1
            if self._flash_state_depth == 0:
                callback = self._flash_state_callback
        if callback is not None:
            callback(False)

    def download_feature_package(
        self,
        catalog_item: dict[str, Any],
        *,
        current_version: str | None = None,
        force: bool = True,
    ) -> OtaPackageResult:
        feature_id = str(catalog_item["id"])
        checked_at = utc_now()
        if not catalog_item.get("downloadable"):
            return OtaPackageResult(
                feature_id=feature_id,
                action_id="none",
                action_type="none",
                target="none",
                downloaded=False,
                applied=False,
                updated=False,
                path=None,
                version=None,
                release_tag=None,
                release_url=None,
                asset_name=None,
                downloaded_at=None,
                checked_at=checked_at,
            )

        action = self.python_package_action(catalog_item)
        return self.run_action(
            catalog_item,
            action,
            current_version=current_version,
            force=force,
        )

    def run_action(
        self,
        catalog_item: dict[str, Any],
        action: dict[str, Any],
        *,
        current_version: str | None = None,
        force: bool = True,
    ) -> OtaPackageResult:
        action_type = str(action.get("type", ""))
        handler = self._actions.get(action_type)
        if handler is None:
            raise ValueError(f"unsupported OTA action type: {action_type}")
        try:
            return handler.run(
                self,
                catalog_item,
                action,
                current_version=current_version,
                force=force,
            )
        except Exception as exc:
            self.update_progress(
                str(catalog_item["id"]),
                action_id=str(action.get("id", "unknown")),
                action_type=action_type,
                target=str(action.get("target", "unknown")),
                phase="error",
                status="error",
                percent=None,
                message=str(exc),
                active=False,
            )
            raise

    def update_progress(
        self,
        feature_id: str,
        *,
        action_id: str,
        action_type: str,
        target: str,
        phase: str,
        status: str,
        percent: int | None,
        message: str,
        active: bool,
        bytes_downloaded: int | None = None,
        total_bytes: int | None = None,
    ) -> None:
        progress = {
            "feature_id": feature_id,
            "action_id": action_id,
            "action_type": action_type,
            "target": target,
            "phase": phase,
            "status": status,
            "percent": percent,
            "message": message,
            "active": active,
            "bytes_downloaded": bytes_downloaded,
            "total_bytes": total_bytes,
            "updated_at": utc_now(),
        }
        with self._progress_lock:
            self._progress[feature_id] = progress

    def progress_for(self, feature_id: str) -> dict[str, Any]:
        with self._progress_lock:
            progress = self._progress.get(feature_id)
            if progress is None:
                return {
                    "feature_id": feature_id,
                    "active": False,
                    "percent": None,
                    "status": "idle",
                    "phase": "idle",
                    "message": "",
                }
            return dict(progress)

    def clear_progress(self, feature_id: str | None = None) -> None:
        with self._progress_lock:
            if feature_id is None:
                self._progress.clear()
            else:
                self._progress.pop(feature_id, None)

    def actions_for(self, catalog_item: dict[str, Any]) -> list[dict[str, Any]]:
        actions = catalog_item.get("ota_actions")
        if isinstance(actions, list) and actions:
            return [action for action in actions if isinstance(action, dict)]

        if catalog_item.get("release_repo") and catalog_item.get("download_file"):
            return [
                {
                    "id": "python_package",
                    "type": "github_release_file",
                    "target": "rpi",
                    "release_repo": catalog_item["release_repo"],
                    "download_file": catalog_item["download_file"],
                    "target_dir": "features",
                }
            ]

        return []

    def python_package_action(self, catalog_item: dict[str, Any]) -> dict[str, Any]:
        for action in self.actions_for(catalog_item):
            if action.get("type") in ("github_release_file", "local_file"):
                return action
        raise ValueError(f"python package OTA action is not configured: {catalog_item['id']}")

    def resolve_target_path(self, action: dict[str, Any], download_file: str) -> Path:
        target_dir = str(action.get("target_dir", "features"))
        if Path(download_file).is_absolute():
            return Path(download_file)
        if target_dir == "features":
            return self.downloaded_features_dir / download_file
        if target_dir == "firmware":
            return self.firmware_dir / download_file
        return self.base_dir / target_dir / download_file

    def resolve_source_path(self, action: dict[str, Any]) -> Path:
        source_file = action.get("source_file")
        if not source_file:
            raise ValueError("local_file OTA action requires source_file")
        source = Path(str(source_file))
        if source.is_absolute():
            return source
        return self.base_dir / source

    def fetch_latest_release(self, catalog_item: dict[str, Any], action: dict[str, Any]) -> dict[str, Any]:
        repo = action.get("release_repo") or catalog_item.get("release_repo")
        if not repo:
            raise ValueError(f"release repo is not configured: {catalog_item['id']}")

        if action.get("release_patch_filter") is not None:
            expected_patch = int(action["release_patch_filter"])
            return self.fetch_highest_patch_release(
                catalog_item,
                action,
                expected_patch=expected_patch,
            )

        url = f"https://api.github.com/repos/{repo}/releases/latest"
        data = self._request_bytes(url, accept="application/vnd.github+json")
        payload = json.loads(data.decode("utf-8"))
        if not isinstance(payload, dict) or not payload.get("tag_name"):
            raise RuntimeError(f"latest release payload is invalid: {repo}")
        return payload

    def fetch_highest_patch_release(
        self,
        catalog_item: dict[str, Any],
        action: dict[str, Any],
        *,
        expected_patch: int,
    ) -> dict[str, Any]:
        repo = action.get("release_repo") or catalog_item.get("release_repo")
        if not repo:
            raise ValueError(f"release repo is not configured: {catalog_item['id']}")

        url = f"https://api.github.com/repos/{repo}/releases?per_page=100"
        data = self._request_bytes(url, accept="application/vnd.github+json")
        payload = json.loads(data.decode("utf-8"))
        if not isinstance(payload, list):
            raise RuntimeError(f"release list payload is invalid: {repo}")

        candidates: list[tuple[tuple[int, int, int], dict[str, Any]]] = []
        for release in payload:
            if not isinstance(release, dict):
                continue
            if release.get("draft") or release.get("prerelease"):
                continue
            version = release_version_tuple(str(release.get("tag_name") or ""))
            if version is None or version[2] != expected_patch:
                continue
            candidates.append((version, release))

        if not candidates:
            raise RuntimeError(f"no x.x.{expected_patch} release found: {repo}")
        candidates.sort(key=lambda item: item[0], reverse=True)
        return candidates[0][1]

    def download_release_asset(
        self,
        release: dict[str, Any],
        download_file: str,
        *,
        progress: dict[str, Any] | None = None,
    ) -> tuple[bytes, str]:
        assets = release.get("assets", [])
        if not isinstance(assets, list):
            assets = []

        for asset in assets:
            if not isinstance(asset, dict):
                continue
            if asset.get("name") != download_file:
                continue
            asset_url = asset.get("browser_download_url")
            if not asset_url:
                break
            return self._request_bytes(str(asset_url), progress=progress), download_file

        raise FileNotFoundError(f"release asset not found: {download_file}")

    def download_release_bin_asset(
        self,
        release: dict[str, Any],
        asset_name: Any = None,
        *,
        progress: dict[str, Any] | None = None,
    ) -> tuple[bytes, str]:
        name = self.release_bin_asset_name(release, asset_name)
        assets = release.get("assets", [])
        if not isinstance(assets, list):
            assets = []
        for asset in assets:
            if (
                isinstance(asset, dict)
                and asset.get("name") == name
                and asset.get("browser_download_url")
            ):
                return self._request_bytes(str(asset["browser_download_url"]), progress=progress), name

        raise FileNotFoundError(f"release asset not found: {name}")

    def release_bin_asset_name(self, release: dict[str, Any], asset_name: Any = None) -> str:
        assets = release.get("assets", [])
        if not isinstance(assets, list):
            assets = []

        bin_assets = [
            asset
            for asset in assets
            if isinstance(asset, dict)
            and asset.get("browser_download_url")
            and str(asset.get("name", "")).lower().endswith(".bin")
        ]
        if asset_name:
            expected = str(asset_name)
            for asset in bin_assets:
                if asset.get("name") == expected:
                    return expected
            raise FileNotFoundError(f"release asset not found: {expected}")

        if len(bin_assets) != 1:
            names = ", ".join(str(asset.get("name")) for asset in bin_assets) or "none"
            raise FileNotFoundError(f"expected exactly one .bin release asset, found: {names}")

        asset = bin_assets[0]
        return str(asset["name"])

    def _request_bytes(
        self,
        url: str,
        *,
        accept: str = "application/octet-stream",
        progress: dict[str, Any] | None = None,
    ) -> bytes:
        request = urllib.request.Request(
            url,
            headers={
                "Accept": accept,
                "User-Agent": "vehicle-computer-ota",
                "X-GitHub-Api-Version": "2022-11-28",
            },
        )
        try:
            with urllib.request.urlopen(request, timeout=self.timeout_seconds) as response:
                total_header = response.headers.get("Content-Length")
                total_bytes = int(total_header) if total_header and total_header.isdigit() else None
                if progress is None:
                    return response.read()

                chunks: list[bytes] = []
                downloaded = 0
                last_percent: int | None = None
                percent_start = int(progress.get("percent_start", 0))
                percent_end = int(progress.get("percent_end", 100))
                while True:
                    chunk = response.read(64 * 1024)
                    if not chunk:
                        break
                    chunks.append(chunk)
                    downloaded += len(chunk)
                    percent = (
                        min(100, int(downloaded * 100 / total_bytes))
                        if total_bytes
                        else None
                    )
                    if percent is not None:
                        percent = percent_start + int(percent * (percent_end - percent_start) / 100)
                    if percent != last_percent:
                        last_percent = percent
                        self.update_progress(
                            str(progress["feature_id"]),
                            action_id=str(progress["action_id"]),
                            action_type=str(progress["action_type"]),
                            target=str(progress["target"]),
                            phase="download",
                            status="downloading",
                            percent=percent,
                            message=str(progress["message"]),
                            active=True,
                            bytes_downloaded=downloaded,
                            total_bytes=total_bytes,
                        )

                data = b"".join(chunks)
                self.update_progress(
                    str(progress["feature_id"]),
                    action_id=str(progress["action_id"]),
                    action_type=str(progress["action_type"]),
                    target=str(progress["target"]),
                    phase="download",
                    status="downloading",
                    percent=percent_end,
                    message=str(progress["message"]),
                    active=True,
                    bytes_downloaded=len(data),
                    total_bytes=total_bytes or len(data),
                )
                return data
        except urllib.error.HTTPError as exc:
            raise RuntimeError(f"download failed ({exc.code}): {url}") from exc
        except urllib.error.URLError as exc:
            raise RuntimeError(f"download failed: {url}: {exc.reason}") from exc

    def download_zcu_payload(self, catalog_item: dict[str, Any]) -> OtaPackageResult:
        return self.run_action(catalog_item, self.zcu_flash_action(catalog_item), force=False)

    def flash_zcu_payload(self, catalog_item: dict[str, Any]) -> OtaPackageResult:
        return self.run_action(catalog_item, self.zcu_flash_action(catalog_item), force=True)

    def zcu_flash_action(self, catalog_item: dict[str, Any]) -> dict[str, Any]:
        for action in self.actions_for(catalog_item):
            if action.get("type") == "doip_uds_flash":
                return action
        return {
            "id": "zcu_flash",
            "type": "doip_uds_flash",
            "target": "zcu",
        }

    def flash_bin_via_doip(
        self,
        bin_path: Path,
        feature_id: str,
        action: dict[str, Any],
        *,
        progress: dict[str, Any] | None = None,
    ) -> bool:
        if not bin_path.exists():
            raise FileNotFoundError(f"bin file not found: {bin_path}")

        firmware = bin_path.read_bytes()
        total = len(firmware)
        ecu_ip = str(action.get("ecu_ip", "192.168.10.2"))
        doip_port = int(action.get("doip_port", 13400))
        tester_address = int(action.get("tester_address", 0x0E00))
        ecu_address = int(action.get("ecu_address", 0x0001))
        bank_start = int(action.get("bank_start", 0x80300000))
        request_timeout = float(action.get("timeout_seconds", 60.0))

        def fail(message: str, *, offset: int = 0) -> None:
            if progress:
                self.update_progress(
                    str(progress["feature_id"]),
                    action_id=str(progress["action_id"]),
                    action_type=str(progress["action_type"]),
                    target=str(progress["target"]),
                    phase="error",
                    status="failed",
                    percent=None,
                    message=message,
                    active=False,
                    bytes_downloaded=offset,
                    total_bytes=total,
                )
            raise RuntimeError(message)

        def set_progress(
            phase: str,
            status: str,
            percent: int | None,
            message: str,
            *,
            offset: int = 0,
            active: bool = True,
        ) -> None:
            if not progress:
                return
            self.update_progress(
                str(progress["feature_id"]),
                action_id=str(progress["action_id"]),
                action_type=str(progress["action_type"]),
                target=str(progress["target"]),
                phase=phase,
                status=status,
                percent=percent,
                message=message,
                active=active,
                bytes_downloaded=offset,
                total_bytes=total,
            )

        doip_connection = None
        stage = "importing DoIP/UDS libraries"
        try:
            self._enter_flash()
            import doipclient
            import udsoncan
            import udsoncan.services
            from doipclient.connectors import DoIPClientUDSConnector
            from udsoncan.client import Client

            stage = f"connecting DoIP {ecu_ip}:{doip_port}"
            doip_connection = doipclient.DoIPClient(
                ecu_ip,
                ecu_address,
                tcp_port=doip_port,
            )
            uds_connection = DoIPClientUDSConnector(doip_connection)
            client = Client(uds_connection, request_timeout=request_timeout)

            set_progress("install", "flashing", None, f"DoIP connected: {ecu_ip}:{doip_port}")
            with client:
                stage = "DiagnosticSessionControl extended"
                client.change_session(
                    udsoncan.services.DiagnosticSessionControl.Session.extendedDiagnosticSession
                )
                set_progress("install", "flashing", None, "Extended Session active")

                memory_location = udsoncan.MemoryLocation(
                    address=bank_start,
                    memorysize=total,
                    address_format=32,
                    memorysize_format=32,
                )
                stage = f"RequestDownload addr=0x{bank_start:08X} size={total}"
                response = client.request_download(memory_location)
                max_block = int(response.service_data.max_length)
                max_payload = max(1, max_block - 2)
                set_progress(
                    "install",
                    "flashing",
                    None,
                    f"RequestDownload complete: addr=0x{bank_start:08X} maxBlock={max_block}",
                )

                offset = 0
                sequence = 1
                percent_start = int((progress or {}).get("percent_start", 50))
                percent_end = int((progress or {}).get("percent_end", 100))
                last_percent: int | None = None

                while offset < total:
                    chunk = firmware[offset:offset + max_payload]
                    stage = f"TransferData seq={sequence} offset={offset}/{total}"
                    client.transfer_data(sequence, chunk)

                    offset += len(chunk)
                    sequence = (sequence + 1) & 0xFF
                    raw_percent = min(100, int(offset * 100 / total)) if total else 100
                    percent = percent_start + int(raw_percent * (percent_end - percent_start) / 100)
                    if percent != last_percent:
                        last_percent = percent
                        set_progress(
                            "install",
                            "flashing",
                            percent,
                            str((progress or {}).get("message", "ZCU firmware flashing")),
                            offset=offset,
                        )

                transfer_exit_delay = float(action.get("transfer_exit_delay_seconds", 0.5))
                if transfer_exit_delay > 0:
                    time.sleep(transfer_exit_delay)
                crc32 = zlib.crc32(firmware) & 0xFFFFFFFF
                stage = f"RequestTransferExit crc32=0x{crc32:08X}"
                try:
                    client.request_transfer_exit(crc32.to_bytes(4, byteorder="big"))
                except Exception as exc:
                    if bool(action.get("transfer_exit_timeout_success", True)) and "respond in time" in str(exc).lower():
                        set_progress(
                            "install",
                            "flashing",
                            percent_end,
                            f"TransferExit sent; ECU reset/no response treated as complete: crc32=0x{crc32:08X}",
                            offset=total,
                        )
                        return True
                    raise
                set_progress(
                    "install",
                    "flashing",
                    percent_end,
                    f"TransferExit complete: crc32=0x{crc32:08X}",
                    offset=total,
                )

            return True
        except Exception as exc:
            if isinstance(exc, RuntimeError):
                raise
            fail(f"DoIP/UDS flash failed during {stage}: {type(exc).__name__}: {exc}")
        finally:
            if doip_connection is not None:
                try:
                    doip_connection.close()
                except Exception:
                    pass
            self._leave_flash()

    def clear_downloaded_feature_packages(self) -> list[str]:
        removed: list[str] = []
        if not self.downloaded_features_dir.exists():
            return removed

        base = self.downloaded_features_dir.resolve()
        for path in self.downloaded_features_dir.iterdir():
            resolved = path.resolve()
            if resolved == base or base not in resolved.parents:
                raise RuntimeError(f"refusing to remove outside downloaded features dir: {path}")

            if path.is_dir():
                if path.name != "__pycache__":
                    continue
                shutil.rmtree(path)
            else:
                path.unlink()
            removed.append(str(path))

        return removed

    def clear_downloaded_firmware_packages(self) -> list[str]:
        removed: list[str] = []
        if not self.firmware_dir.exists():
            return removed

        base = self.firmware_dir.resolve()
        for path in self.firmware_dir.iterdir():
            resolved = path.resolve()
            if resolved == base or base not in resolved.parents:
                raise RuntimeError(f"refusing to remove outside firmware dir: {path}")
            if path.is_dir():
                shutil.rmtree(path)
            else:
                path.unlink()
            removed.append(str(path))
        return removed

    def status(self) -> dict[str, Any]:
        return {
            "actions": {
                "local_file": "implemented",
                "github_release_file": "implemented",
                "doip_uds_flash": "implemented",
            },
            "downloaded_features_dir": str(self.downloaded_features_dir),
            "firmware_dir": str(self.firmware_dir),
            "timeout_seconds": self.timeout_seconds,
        }
