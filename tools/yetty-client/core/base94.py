"""Base94 encoding/decoding for yetty OSC payloads.

Uses printable ASCII from '!' (33) to '~' (126) - 94 characters.
Each byte is encoded as 2 characters.
"""


def encode(data: bytes) -> str:
    """Encode bytes to base94 string (2 chars per byte)."""
    result = []
    for byte in data:
        result.append(chr(33 + (byte // 94)))
        result.append(chr(33 + (byte % 94)))
    return ''.join(result)


def decode(encoded: str) -> bytes:
    """Decode base94 string back to bytes."""
    result = []
    i = 0
    while i + 1 < len(encoded):
        c1 = ord(encoded[i]) - 33
        c2 = ord(encoded[i + 1]) - 33
        result.append(c1 * 94 + c2)
        i += 2
    return bytes(result)


def encode_string(text: str) -> str:
    """Encode a UTF-8 string to base94."""
    return encode(text.encode('utf-8'))


def decode_string(encoded: str) -> str:
    """Decode base94 to UTF-8 string."""
    return decode(encoded).decode('utf-8')
