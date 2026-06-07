#!/usr/bin/env python3
"""relay_ctl.py — CLI test tool for the ESP32-C6 relay controller."""

import argparse
import json
import socket
import sys

UDP_PORT = 12345
TIMEOUT  = 3


def _find_device(host, mac):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(TIMEOUT)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    msg = json.dumps({"id": 1, "method": "get_sysinfo", "params": {}})
    sock.sendto(msg.encode(), (host, UDP_PORT))

    while True:
        try:
            data, addr = sock.recvfrom(2048)
            resp = json.loads(data.decode())
            result = resp.get("result", {})
            if result.get("mac", "").lower() == mac.lower():
                sock.close()
                return addr[0], result
        except socket.timeout:
            break

    sock.close()
    return None, None


def _rpc(ip, method, params, req_id=1):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(TIMEOUT)

    msg = json.dumps({"id": req_id, "method": method, "params": params})
    sock.sendto(msg.encode(), (ip, UDP_PORT))

    try:
        data, _ = sock.recvfrom(2048)
        return json.loads(data.decode())
    except socket.timeout:
        return None
    finally:
        sock.close()


def _print_relays(children):
    for ch in children:
        state = "ON " if ch["on"] else "OFF"
        print(f"  relay {ch['id']}  [{state}]  {ch['alias']}")


def _resolve(host, mac):
    ip, info = _find_device(host, mac)
    if not ip:
        print(f"error: device {mac} not found on {host}", file=sys.stderr)
        sys.exit(1)
    return ip, info


def cmd_status(args):
    ip, info = _resolve(args.host, args.mac)
    print(f"mac={info['mac']}  ip={info['ip']}  type={info['type']}  ver={info['version']}")
    _print_relays(info.get("children", []))


def cmd_on(args):
    ip, _ = _resolve(args.host, args.mac)
    children = [{"id": rid, "on": 1} for rid in args.relay]
    resp = _rpc(ip, "set_relay_state", {"children": children})
    if not resp or "error" in resp:
        print(f"error: {resp}", file=sys.stderr)
        sys.exit(1)
    _print_relays(resp["result"].get("children", []))


def cmd_off(args):
    ip, _ = _resolve(args.host, args.mac)
    children = [{"id": rid, "on": 0} for rid in args.relay]
    resp = _rpc(ip, "set_relay_state", {"children": children})
    if not resp or "error" in resp:
        print(f"error: {resp}", file=sys.stderr)
        sys.exit(1)
    _print_relays(resp["result"].get("children", []))


def cmd_alias(args):
    ip, _ = _resolve(args.host, args.mac)
    resp = _rpc(ip, "set_alias", {"children": [{"id": args.relay, "alias": args.name}]})
    if not resp or "error" in resp:
        print(f"error: {resp}", file=sys.stderr)
        sys.exit(1)
    _print_relays(resp["result"].get("children", []))


def main():
    parser = argparse.ArgumentParser(
        description="test CLI for the ESP32-C6 relay controller"
    )
    parser.add_argument("--host", required=True,
                        help="broadcast or device IP (e.g. 192.168.1.255)")
    parser.add_argument("--mac",  required=True,
                        help="device MAC address (e.g. aa:bb:cc:dd:ee:ff)")

    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("status", help="show device info and relay states")

    p_on = sub.add_parser("on", help="turn one or more relays on")
    p_on.add_argument("relay", type=int, nargs="+", metavar="ID",
                      help="relay id(s): 0-3")

    p_off = sub.add_parser("off", help="turn one or more relays off")
    p_off.add_argument("relay", type=int, nargs="+", metavar="ID",
                       help="relay id(s): 0-3")

    p_alias = sub.add_parser("alias", help="set a relay's alias (persisted in NVS)")
    p_alias.add_argument("relay", type=int, metavar="ID", help="relay id: 0-3")
    p_alias.add_argument("name",  metavar="NAME", help="alias string")

    args = parser.parse_args()
    {"status": cmd_status, "on": cmd_on, "off": cmd_off, "alias": cmd_alias}[args.cmd](args)


if __name__ == "__main__":
    main()
