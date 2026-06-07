#!/usr/bin/env python3
"""relay_ctl.py — CLI test tool for the ESP32-C6 relay controller."""

import argparse
import json
import socket
import sys

UDP_PORT = 12345
TIMEOUT  = 3


def _find_device(host, mac, verbose=False):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('', 0))
    sock.settimeout(TIMEOUT)

    msg = json.dumps({"id": 1, "method": "get_sysinfo", "params": {}})
    sock.sendto(msg.encode(), (host, UDP_PORT))

    while True:
        try:
            data, addr = sock.recvfrom(2048)
            if verbose:
                print(f"[debug] rx from {addr}: {data.decode(errors='replace')}",
                      file=sys.stderr)
            resp = json.loads(data.decode())
            result = resp.get("result", {})
            if result.get("mac", "").lower() == mac.lower():
                sock.close()
                return addr[0], result
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
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


def _resolve(args):
    ip, info = _find_device(args.host, args.mac, verbose=args.verbose)
    if not ip:
        print(f"error: device {args.mac} not found on {args.host}", file=sys.stderr)
        sys.exit(1)
    return ip, info


def cmd_status(args):
    ip, info = _resolve(args)
    print(f"mac={info['mac']}  ip={info['ip']}  type={info['type']}  ver={info['version']}")
    _print_relays(info.get("children", []))


def cmd_on(args):
    ip, _ = _resolve(args)
    children = [{"id": rid, "on": 1} for rid in args.relay]
    resp = _rpc(ip, "set_relay_state", {"children": children})
    if not resp or "error" in resp:
        print(f"error: {resp}", file=sys.stderr)
        sys.exit(1)
    _print_relays(resp["result"].get("children", []))


def cmd_off(args):
    ip, _ = _resolve(args)
    children = [{"id": rid, "on": 0} for rid in args.relay]
    resp = _rpc(ip, "set_relay_state", {"children": children})
    if not resp or "error" in resp:
        print(f"error: {resp}", file=sys.stderr)
        sys.exit(1)
    _print_relays(resp["result"].get("children", []))


def cmd_alias(args):
    ip, _ = _resolve(args)
    resp = _rpc(ip, "set_alias", {"children": [{"id": args.relay, "alias": args.name}]})
    if not resp or "error" in resp:
        print(f"error: {resp}", file=sys.stderr)
        sys.exit(1)
    _print_relays(resp["result"].get("children", []))


def main():
    verbose_p = argparse.ArgumentParser(add_help=False)
    verbose_p.add_argument("-v", "--verbose", action="store_true",
                           help="print all received UDP packets for debugging")

    parser = argparse.ArgumentParser(
        description="test CLI for the ESP32-C6 relay controller",
        parents=[verbose_p],
    )
    parser.add_argument("--host", required=True,
                        help="broadcast or device IP (e.g. 192.168.1.255)")
    parser.add_argument("--mac",  required=True,
                        help="device MAC address (e.g. aa:bb:cc:dd:ee:ff)")

    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("status", parents=[verbose_p],
                   help="show device info and relay states")

    p_on = sub.add_parser("on", parents=[verbose_p],
                          help="turn one or more relays on")
    p_on.add_argument("relay", type=int, nargs="+", metavar="ID",
                      help="relay id(s): 0-3")

    p_off = sub.add_parser("off", parents=[verbose_p],
                           help="turn one or more relays off")
    p_off.add_argument("relay", type=int, nargs="+", metavar="ID",
                       help="relay id(s): 0-3")

    p_alias = sub.add_parser("alias", parents=[verbose_p],
                             help="set a relay's alias (persisted in NVS)")
    p_alias.add_argument("relay", type=int, metavar="ID", help="relay id: 0-3")
    p_alias.add_argument("name",  metavar="NAME", help="alias string")

    args = parser.parse_args()
    {"status": cmd_status, "on": cmd_on, "off": cmd_off, "alias": cmd_alias}[args.cmd](args)


if __name__ == "__main__":
    main()
