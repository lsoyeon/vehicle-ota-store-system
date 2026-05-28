from __future__ import annotations

import base64
import json
import socket
import struct
from dataclasses import dataclass
from typing import Any, Literal


Protocol = Literal["tcp", "udp"]
PayloadFormat = Literal["text", "hex", "base64", "json", "empty"]

SOMEIP_PROTOCOL_VERSION = 0x01
SOMEIP_INTERFACE_VERSION = 0x01
SOMEIP_MSG_TYPE_REQUEST = 0x00
SOMEIP_RETURN_CODE_OK = 0x00


@dataclass(frozen=True)
class EthernetResponse:
    protocol: Protocol
    remote_host: str
    remote_port: int
    bytes_sent: int
    response_bytes: int
    response_hex: str | None
    response_text: str | None
    response_base64: str | None
    response_from: str | None


@dataclass(frozen=True)
class SomeIpMessage:
    service_id: int
    method_id: int
    client_id: int
    session_id: int
    protocol_version: int
    interface_version: int
    message_type: int
    return_code: int
    payload: bytes


def parse_payload(payload: Any = "", payload_format: PayloadFormat = "text") -> bytes:
    if payload_format == "empty":
        return b""

    if payload_format == "json":
        return json.dumps(payload, separators=(",", ":")).encode("utf-8")

    if isinstance(payload, bytes):
        return payload

    if payload is None:
        text = ""
    else:
        text = str(payload)

    if payload_format == "text":
        return text.encode("utf-8")

    if payload_format == "hex":
        compact = (
            text.replace("0x", "")
            .replace("0X", "")
            .replace(" ", "")
            .replace("-", "")
            .replace("_", "")
            .replace(":", "")
        )
        if len(compact) % 2 != 0:
            compact = "0" + compact
        return bytes.fromhex(compact)

    if payload_format == "base64":
        return base64.b64decode(text, validate=True)

    raise ValueError(f"unsupported payload_format: {payload_format}")


def format_response(
    protocol: Protocol,
    remote_host: str,
    remote_port: int,
    bytes_sent: int,
    response: bytes | None,
    response_from: tuple[str, int] | None = None,
) -> EthernetResponse:
    source = None
    if response_from:
        source = f"{response_from[0]}:{response_from[1]}"

    if not response:
        return EthernetResponse(
            protocol=protocol,
            remote_host=remote_host,
            remote_port=remote_port,
            bytes_sent=bytes_sent,
            response_bytes=0,
            response_hex=None,
            response_text=None,
            response_base64=None,
            response_from=source,
        )

    return EthernetResponse(
        protocol=protocol,
        remote_host=remote_host,
        remote_port=remote_port,
        bytes_sent=bytes_sent,
        response_bytes=len(response),
        response_hex=response.hex(" "),
        response_text=response.decode("utf-8", errors="replace"),
        response_base64=base64.b64encode(response).decode("ascii"),
        response_from=source,
    )


def validate_port(port: int) -> None:
    if port < 1 or port > 65535:
        raise ValueError("port must be between 1 and 65535")


def build_someip_packet(
    payload: bytes,
    *,
    service_id: int,
    method_id: int,
    client_id: int,
    session_id: int,
    protocol_version: int = SOMEIP_PROTOCOL_VERSION,
    interface_version: int = SOMEIP_INTERFACE_VERSION,
    message_type: int = SOMEIP_MSG_TYPE_REQUEST,
    return_code: int = SOMEIP_RETURN_CODE_OK,
) -> bytes:
    service_id = validate_uint16(service_id, "service_id")
    method_id = validate_uint16(method_id, "method_id")
    client_id = validate_uint16(client_id, "client_id")
    session_id = validate_uint16(session_id, "session_id")
    protocol_version = validate_uint8(protocol_version, "protocol_version")
    interface_version = validate_uint8(interface_version, "interface_version")
    message_type = validate_uint8(message_type, "message_type")
    return_code = validate_uint8(return_code, "return_code")

    message_id = (service_id << 16) | method_id
    request_id = (client_id << 16) | session_id
    length = 8 + len(payload)
    header = struct.pack(
        "!IIIBBBB",
        message_id,
        length,
        request_id,
        protocol_version,
        interface_version,
        message_type,
        return_code,
    )
    return header + payload


def parse_someip_packet(data: bytes) -> SomeIpMessage | None:
    if len(data) < 16:
        return None

    (
        message_id,
        length,
        request_id,
        protocol_version,
        interface_version,
        message_type,
        return_code,
    ) = struct.unpack("!IIIBBBB", data[:16])

    payload_length = max(0, length - 8)
    payload = data[16:16 + payload_length]
    return SomeIpMessage(
        service_id=(message_id >> 16) & 0xFFFF,
        method_id=message_id & 0xFFFF,
        client_id=(request_id >> 16) & 0xFFFF,
        session_id=request_id & 0xFFFF,
        protocol_version=protocol_version,
        interface_version=interface_version,
        message_type=message_type,
        return_code=return_code,
        payload=payload,
    )


def validate_uint16(value: int, name: str) -> int:
    value = int(value)
    if value < 0 or value > 0xFFFF:
        raise ValueError(f"{name} must be between 0x0000 and 0xffff")
    return value


def validate_uint8(value: int, name: str) -> int:
    value = int(value)
    if value < 0 or value > 0xFF:
        raise ValueError(f"{name} must be between 0x00 and 0xff")
    return value


def send_ethernet_message(
    protocol: Protocol,
    host: str,
    port: int,
    payload: bytes,
    *,
    timeout_seconds: float = 2.0,
    expect_response: bool = False,
    receive_bytes: int = 4096,
    local_host: str | None = None,
    local_port: int | None = None,
) -> EthernetResponse:
    protocol = protocol.lower()
    if protocol not in ("tcp", "udp"):
        raise ValueError("protocol must be 'tcp' or 'udp'")
    validate_port(port)
    if local_port is not None:
        validate_port(local_port)
    if timeout_seconds <= 0:
        raise ValueError("timeout_seconds must be greater than 0")
    if receive_bytes <= 0:
        raise ValueError("receive_bytes must be greater than 0")

    if protocol == "udp":
        return _send_udp(
            host,
            port,
            payload,
            timeout_seconds=timeout_seconds,
            expect_response=expect_response,
            receive_bytes=receive_bytes,
            local_host=local_host,
            local_port=local_port,
        )

    return _send_tcp(
        host,
        port,
        payload,
        timeout_seconds=timeout_seconds,
        expect_response=expect_response,
        receive_bytes=receive_bytes,
        local_host=local_host,
        local_port=local_port,
    )


def _send_udp(
    host: str,
    port: int,
    payload: bytes,
    *,
    timeout_seconds: float,
    expect_response: bool,
    receive_bytes: int,
    local_host: str | None,
    local_port: int | None,
) -> EthernetResponse:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout_seconds)
        if local_host or local_port:
            sock.bind((local_host or "0.0.0.0", local_port or 0))

        bytes_sent = sock.sendto(payload, (host, port))
        response = None
        response_from = None
        if expect_response:
            response, response_from = sock.recvfrom(receive_bytes)

    return format_response("udp", host, port, bytes_sent, response, response_from)


def _send_tcp(
    host: str,
    port: int,
    payload: bytes,
    *,
    timeout_seconds: float,
    expect_response: bool,
    receive_bytes: int,
    local_host: str | None,
    local_port: int | None,
) -> EthernetResponse:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.settimeout(timeout_seconds)
        if local_host or local_port:
            sock.bind((local_host or "0.0.0.0", local_port or 0))
        sock.connect((host, port))
        sock.sendall(payload)

        response = None
        if expect_response:
            response = sock.recv(receive_bytes)

        return format_response("tcp", host, port, len(payload), response)
    finally:
        sock.close()
