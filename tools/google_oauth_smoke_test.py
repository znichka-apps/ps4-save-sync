"""Memory-only Google device OAuth / Drive smoke test (standard library).

Reference: https://developers.google.com/identity/protocols/oauth2/limited-input-device
Usage: python tools/google_oauth_smoke_test.py PATH_TO_EXTERNAL_CREDENTIALS_JSON
"""

import argparse
import json
import math
from pathlib import Path
import ssl
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


SCOPE = "https://www.googleapis.com/auth/drive.file"
KNOWN_ERRORS = frozenset({
    "authorization_pending", "slow_down", "expired_token", "access_denied",
    "invalid_client", "invalid_grant", "invalid_scope", "unsupported_grant_type",
    "admin_policy_enforced", "org_internal", "rate_limit_exceeded",
    "unauthorized_client", "invalid_request", "disallowed_useragent",
})


class SmokeError(Exception):
    """Only locally composed, sanitized messages may be raised here."""


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # Never forward a secret-bearing request to a redirected endpoint.
        return None


def request_json(opener, url, form=None, token=None, timeout=30):
    headers = {"Accept": "application/json"}
    data = None
    if form is not None:
        data = urllib.parse.urlencode(form).encode("ascii")
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    if token is not None:
        headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request(url, data=data, headers=headers)
    try:
        response = opener.open(req, timeout=timeout)
    except urllib.error.HTTPError as exc:
        response = exc
    except (urllib.error.URLError, OSError, ValueError):
        raise SmokeError("HTTPS connection failed; certificate verification remains enabled.") from None
    with response:
        status = response.code
        raw = response.read(1048577)
    if len(raw) > 1048576:
        raise SmokeError("Server response exceeded the size limit.")
    try:
        payload = json.loads(raw)
    except (ValueError, UnicodeError):
        raise SmokeError(f"Invalid JSON response (HTTP {status}).") from None
    if not isinstance(payload, dict):
        raise SmokeError(f"Unexpected response format (HTTP {status}).")
    return status, payload


def fail_response(stage, status, payload):
    code = payload.get("error", payload.get("error_code"))
    safe_code = code if isinstance(code, str) and code in KNOWN_ERRORS else "unrecognized_error"
    raise SmokeError(f"{stage} failed (HTTP {status}; {safe_code}).")


def positive_seconds(payload, key):
    value = payload.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise SmokeError(f"Device response has an invalid {key}.")
    return float(value)


def required_string(payload, key):
    value = payload.get(key)
    if not isinstance(value, str) or not value:
        raise SmokeError(f"Response is missing {key}.")
    return value


def run(credentials_path):
    path = Path(credentials_path).resolve()
    repo = Path(__file__).resolve().parents[1]
    if path.is_relative_to(repo):
        raise SmokeError("Credentials must remain outside the repository.")
    try:
        credentials = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        raise SmokeError("Could not read the external credentials JSON.") from None
    if not isinstance(credentials, dict):
        raise SmokeError("Invalid credentials JSON structure.")
    client = credentials.get("installed", credentials.get("web", credentials))
    if not isinstance(client, dict):
        raise SmokeError("Invalid OAuth client configuration.")
    client_id = required_string(client, "client_id")
    client_secret = required_string(client, "client_secret")
    opener = urllib.request.build_opener(
        urllib.request.HTTPSHandler(context=ssl.create_default_context()), NoRedirect()
    )
    # Google recommends retrieving the device endpoint from its discovery document.
    status, discovery = request_json(opener, "https://accounts.google.com/.well-known/openid-configuration")
    if status != 200:
        fail_response("Discovery", status, discovery)
    device_endpoint = discovery.get("device_authorization_endpoint")
    if device_endpoint != "https://oauth2.googleapis.com/device/code":
        raise SmokeError("Discovery returned an unexpected device endpoint.")
    status, device = request_json(opener, device_endpoint, {"client_id": client_id, "scope": SCOPE})
    if status != 200 or "error" in device or "error_code" in device:
        fail_response("Device authorization", status, device)
    deadline = time.monotonic() + positive_seconds(device, "expires_in")
    interval = positive_seconds(device, "interval")
    device_code = required_string(device, "device_code")
    url = required_string(device, "verification_url")
    user_code = required_string(device, "user_code")
    if not all(32 <= ord(char) <= 126 for char in url + user_code):
        raise SmokeError("Device response contains invalid display characters.")
    print(f"Verification URL: {url}", flush=True)
    print(f"User code: {user_code}", flush=True)
    print("Waiting for authorization on your phone...", flush=True)
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= interval:
            raise SmokeError("Device authorization expired; run the test again.")
        time.sleep(interval)
        status, tokens = request_json(opener, "https://oauth2.googleapis.com/token", {
            "client_id": client_id, "client_secret": client_secret,
            "device_code": device_code,
            "grant_type": "urn:ietf:params:oauth:grant-type:device_code",
        }, timeout=min(30, max(1, deadline - time.monotonic())))
        error = tokens.get("error")
        if error == "authorization_pending":
            continue
        if error == "slow_down":
            interval += 5
            continue
        if error == "expired_token":
            raise SmokeError("Device authorization expired; run the test again.")
        if error == "access_denied":
            raise SmokeError("Authorization was denied.")
        if status != 200 or error is not None:
            fail_response("Token authorization", status, tokens)
        required_string(tokens, "refresh_token")
        access_token = required_string(tokens, "access_token")
        if tokens.get("token_type", "").lower() != "bearer":
            raise SmokeError("Unexpected token type.")
        if SCOPE not in required_string(tokens, "scope").split():
            raise SmokeError("Required Drive scope was not granted.")
        print("Success: refresh token returned (kept in memory).", flush=True)
        break
    query = urllib.parse.urlencode({"pageSize": 1, "fields": "files(id)"})
    status, files = request_json(opener, "https://www.googleapis.com/drive/v3/files?" + query, token=access_token)
    if status != 200 or "error" in files:
        fail_response("Drive files.list", status, files)
    if not isinstance(files.get("files"), list):
        raise SmokeError("Unexpected Drive files.list response.")
    print("Success: Drive v3 files.list access verified. No tokens saved.", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("credentials_path", help="OAuth credentials JSON outside this repository")
    args = parser.parse_args()
    try:
        run(args.credentials_path)
        return 0
    except SmokeError as exc:
        print(f"Error: {exc}", file=sys.stderr, flush=True)
    except KeyboardInterrupt:
        print("Error: Test canceled.", file=sys.stderr, flush=True)
    except Exception:
        # Never emit exception details/tracebacks that could expose credentials.
        print("Error: Test failed unexpectedly; sensitive details suppressed.", file=sys.stderr, flush=True)
    return 1


if __name__ == "__main__":
    sys.exit(main())
