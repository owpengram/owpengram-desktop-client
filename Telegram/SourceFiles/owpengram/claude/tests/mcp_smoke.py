#!/usr/bin/env python3
"""Smoke test for the OwpenGram local MCP server (stdlib only).

Baseline checks always run. Access-level checks depend on how the running
client is configured, so pick the phase that matches it:

  --phase forbidden    every account is Forbidden, no chat overrides:
                       list_chats must return [].
  --phase read         an account is at Read, and --hidden-chat <id> is
                       explicitly Forbidden there: list_chats must be
                       non-empty and must not contain that chat.

Usage: python3 -I mcp_smoke.py --port 47800 --token <token> [--phase ...]
"""
import argparse
import http.client
import json
import sys

TOOLS = {
    "list_accounts", "current_chat", "list_chats", "chat_info",
    "chat_members", "read_history", "search_messages", "send_message",
    "reply", "react", "forward",
}


class Failure(Exception):
    pass


def check(cond, what):
    if not cond:
        raise Failure(what)
    print("ok   " + what)


def post(port, token, body, extra_headers=None, path="/mcp"):
    """Returns (status, headers, body-bytes)."""
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=130)
    headers = {"Content-Type": "application/json"}
    if token is not None:
        headers["Authorization"] = "Bearer " + token
    headers.update(extra_headers or {})
    if not isinstance(body, bytes):
        body = json.dumps(body).encode()
    try:
        conn.request("POST", path, body=body, headers=headers)
        resp = conn.getresponse()
        return resp.status, dict(resp.getheaders()), resp.read()
    finally:
        conn.close()


def rpc(port, token, method, params=None, rid=1, session=None):
    msg = {"jsonrpc": "2.0", "id": rid, "method": method}
    if params is not None:
        msg["params"] = params
    extra = {"Mcp-Session-Id": session} if session else None
    status, headers, raw = post(port, token, msg, extra)
    check(status == 200, "%s -> HTTP 200 (got %d)" % (method, status))
    return json.loads(raw), headers


def call_tool(port, token, session, name, arguments=None):
    reply, _ = rpc(port, token, "tools/call",
                   {"name": name, "arguments": arguments or {}},
                   session=session)
    result = reply["result"]
    text = result["content"][0]["text"]
    return result["isError"], text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--token", required=True)
    ap.add_argument("--phase", choices=["forbidden", "read"])
    ap.add_argument("--hidden-chat", default="")
    args = ap.parse_args()
    port, token = args.port, args.token

    # Authorization.
    status, _, _ = post(port, None, {"jsonrpc": "2.0", "id": 1, "method": "ping"})
    check(status == 401, "no token -> 401")
    status, _, _ = post(port, token + "x",
                        {"jsonrpc": "2.0", "id": 1, "method": "ping"})
    check(status == 401, "wrong token -> 401")

    # Routing.
    status, _, _ = post(port, token, {}, path="/other")
    check(status == 404, "unknown path -> 404")
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    conn.request("GET", "/mcp", headers={"Authorization": "Bearer " + token})
    status = conn.getresponse().status
    conn.close()
    check(status == 405, "GET /mcp -> 405")

    # initialize.
    reply, headers = rpc(port, token, "initialize", {
        "protocolVersion": "2025-03-26",
        "capabilities": {},
        "clientInfo": {"name": "mcp_smoke", "version": "1"},
    })
    result = reply["result"]
    check(result["serverInfo"]["name"] == "owpengram", "serverInfo.name")
    check(result["protocolVersion"] == "2025-03-26", "supported version echoed")
    check("tools" in result["capabilities"], "capabilities.tools")
    session = {k.lower(): v for k, v in headers.items()}.get("mcp-session-id")
    reply, _ = rpc(port, token, "initialize",
                   {"protocolVersion": "1999-01-01", "capabilities": {},
                    "clientInfo": {"name": "mcp_smoke"}}, rid=2)
    check(reply["result"]["protocolVersion"] == "2025-06-18",
          "unsupported version -> 2025-06-18")

    status, _, raw = post(port, token, {
        "jsonrpc": "2.0", "method": "notifications/initialized"},
        {"Mcp-Session-Id": session} if session else None)
    check(status == 202 and raw == b"", "notifications/initialized -> 202, empty")

    reply, _ = rpc(port, token, "ping", rid=3, session=session)
    check(reply["result"] == {}, "ping")

    # Protocol errors.
    status, _, raw = post(port, token, b"{not json")
    check(status == 200 and json.loads(raw)["error"]["code"] == -32700,
          "bad JSON -> -32700")
    status, _, raw = post(port, token, {"jsonrpc": "2.0", "id": 1})
    check(json.loads(raw)["error"]["code"] == -32600, "no method -> -32600")
    reply, _ = rpc(port, token, "nope/nope", rid=4, session=session)
    check(reply["error"]["code"] == -32601, "unknown method -> -32601")
    reply, _ = rpc(port, token, "tools/call", {"arguments": {}}, rid=5,
                   session=session)
    check(reply["error"]["code"] == -32602, "tools/call w/o name -> -32602")
    status, _, raw = post(port, token, [{"jsonrpc": "2.0", "id": 1,
                                          "method": "ping"}])
    check(json.loads(raw)["error"]["code"] == -32600, "batch -> -32600")

    # Tools.
    reply, _ = rpc(port, token, "tools/list", rid=6, session=session)
    names = {t["name"] for t in reply["result"]["tools"]}
    check(names == TOOLS, "tools/list has the 11 tools (got %s)" % sorted(names))
    is_error, _ = call_tool(port, token, session, "no_such_tool")
    check(is_error, "unknown tool -> isError")

    if args.phase:
        is_error, text = call_tool(port, token, session, "list_chats")
        check(not is_error, "list_chats succeeds")
        chats = json.loads(text)
        if args.phase == "forbidden":
            check(chats == [], "Forbidden account: list_chats == []")
        else:
            check(len(chats) > 0, "Read account: list_chats is not empty")
            check(args.hidden_chat != "", "--hidden-chat given")
            check(all(c["chat"] != args.hidden_chat for c in chats),
                  "explicitly Forbidden chat is absent from list_chats")
    else:
        print("skip access-level checks (pass --phase forbidden|read)")

    # Too large: 2 MB body.
    try:
        status, _, _ = post(port, token, b"x" * (2 * 1024 * 1024))
        check(status == 413, "2 MB body -> 413")
    except (ConnectionError, BrokenPipeError):
        # The server answered 413 and closed while we were still sending.
        check(True, "2 MB body -> rejected (connection closed by server)")

    print("ALL OK")


if __name__ == "__main__":
    try:
        main()
    except Failure as e:
        print("FAIL " + str(e))
        sys.exit(1)
    except (OSError, ValueError, KeyError, IndexError) as e:
        print("FAIL %s: %s" % (type(e).__name__, e))
        sys.exit(1)
