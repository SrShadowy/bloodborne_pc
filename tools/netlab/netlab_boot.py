# SPDX-License-Identifier: GPL-2.0-or-later
"""The netlab boot run: drive a GameSession through the whole shadNet entry
sequence and return a structured report. Orchestration lives here so that
netlab_session stays a driver of primitives only (<= ~300 lines)."""
import time


def _step(steps, name, result, t0):
    if isinstance(result, dict) and "ok" in result:
        ok = result.get("ok", True)
        detail = result.get("text", result.get("detail", ""))
    else:
        ok = result is not False
        detail = "" if result is None else str(result)
    steps.append({"step": name, "ok": bool(ok), "detail": str(detail),
                  "ms": int((time.monotonic() - t0) * 1000)})


def run_login(sess):
    """Match the launcher's 'Проверить вход' one-login verdict."""
    sess.connect()
    r = sess.login()
    # best-effort clean close (non-critical)
    try:
        sess.terminate()
    except Exception:
        pass
    sess.close()
    return r


def run_boot(sess):
    """Drive the game-entry flow and return a report dict (steps/ok/...)."""
    steps = []
    total_start = time.monotonic()
    effective_ok = True

    # 1 connecting + ServerInfo
    t = time.monotonic()
    try:
        si = sess.connect()
        steps.append({"step": "ServerInfo handshake", "ok": True,
                      "detail": f"protocol version {si.get('version', '?')}",
                      "ms": int((time.monotonic() - t) * 1000)})
    except Exception as e:
        steps.append({"step": "ServerInfo handshake", "ok": False,
                      "detail": str(e), "ms": int((time.monotonic() - t) * 1000)})
        sess.close()
        return _wrap(sess, steps, total_start, False)

    # 2 Login — fatal on refusal (matches game behaviour)
    t = time.monotonic()
    try:
        r = sess.login()
    except Exception as e:
        steps.append({"step": "Login", "ok": False, "detail": str(e),
                      "ms": int((time.monotonic() - t) * 1000)})
        sess.close()
        return _wrap(sess, steps, total_start, False)
    steps.append({"step": "Login",
                  "ok": bool(r.get("ok", False)),
                  "detail": _login_detail(r),
                  "ms": int((time.monotonic() - t) * 1000)})
    if r.get("friends"):
        online = [n for n, on in r["friends"] if on]
        steps.append({"step": "  friends online", "ok": True,
                      "detail": (", ".join(online) if online else "none") +
                                f" ({len(online)}/{len(r['friends'])})",
                      "ms": 0})
    if not r.get("ok"):
        sess.close()
        return _wrap(sess, steps, total_start, False)

    # 3 GetToken (WebAPI bearer — token length only, never the token)
    t = time.monotonic()
    try:
        rtok = sess.get_token()
    except Exception as e:
        steps.append({"step": "GetToken (WebAPI bearer)", "ok": False,
                      "detail": str(e),
                      "ms": int((time.monotonic() - t) * 1000)})
        rtok = None
    else:
        steps.append({"step": "GetToken (WebAPI bearer)",
                      "ok": bool(rtok.get("ok", False)),
                      "detail": rtok.get("text", ""),
                      "ms": int((time.monotonic() - t) * 1000)})
    if rtok is None or not rtok.get("ok"):
        # non-fatal; WebAPI stays unauthenticated but matching can proceed
        pass

    # 4 GetServerFeatures
    t = time.monotonic()
    try:
        rf = sess.get_features()
    except Exception as e:
        steps.append({"step": "GetServerFeatures", "ok": False, "detail": str(e),
                      "ms": int((time.monotonic() - t) * 1000)})
        rf = {"ok": False, "matching2_enabled": False}
    else:
        steps.append({"step": "GetServerFeatures",
                      "ok": bool(rf.get("ok", False)),
                      "detail": rf.get("text", ""),
                      "ms": int((time.monotonic() - t) * 1000)})
    matching_enabled = bool(rf.get("matching2_enabled", False))

    # SetClientVersion: server only registers the account after a version string
    t = time.monotonic()
    try:
        rc = sess.set_client_version()
    except Exception as e:
        steps.append({"step": "SetClientVersion", "ok": False, "detail": str(e),
                      "ms": int((time.monotonic() - t) * 1000)})
    else:
        steps.append({"step": "SetClientVersion",
                      "ok": bool(rc.get("ok", False)),
                      "detail": rc.get("text", ""),
                      "ms": int((time.monotonic() - t) * 1000)})

    # 5 matching half — only when matching2_enabled
    if not matching_enabled:
        steps.append({"step": "Matching2 (disabled/failed)", "ok": True,
                      "detail": "skipping ContextStart/SearchRoom",
                      "ms": 0})
    else:
        # ContextStart
        t = time.monotonic()
        try:
            rc = sess.context_start()
        except Exception as e:
            steps.append({"step": "ContextStart", "ok": False, "detail": str(e),
                          "ms": int((time.monotonic() - t) * 1000)})
        else:
            steps.append({"step": "ContextStart",
                          "ok": bool(rc.get("ok", False)),
                          "detail": rc.get("text", ""),
                          "ms": int((time.monotonic() - t) * 1000)})

        # GetWorldInfoList
        t = time.monotonic()
        try:
            rw = sess.world_list()
        except Exception as e:
            steps.append({"step": "GetWorldInfoList", "ok": False, "detail": str(e),
                          "ms": int((time.monotonic() - t) * 1000)})
            rw = None
        else:
            worlds = rw.get("worlds", []) if isinstance(rw, dict) else []
            steps.append({"step": "GetWorldInfoList",
                          "ok": (isinstance(rw, dict) and rw.get("ok", False)),
                          "detail": (rw.get("text", "") if isinstance(rw, dict) else str(rw)),
                          "ms": int((time.monotonic() - t) * 1000)})
            sess.worlds = worlds
            # SearchRoom per world, page through
            for w in worlds:
                wid = w.get("world_id", 1)
                t = time.monotonic()
                try:
                    rs = sess.search_rooms(wid)
                except Exception as e:
                    steps.append({"step": f"SearchRoom (world {wid})", "ok": False,
                                 "detail": str(e),
                                 "ms": int((time.monotonic() - t) * 1000)})
                    continue
                steps.append({"step": f"SearchRoom (world {wid})",
                              "ok": bool(rs.get("ok", False)),
                              "detail": rs.get("text", ""),
                              "ms": int((time.monotonic() - t) * 1000)})
                for rm in (rs.get("rooms", []) if isinstance(rs, dict) else []):
                    rid = rm.get("room_id", 0)
                    if not rid:
                        continue
                    if int(rm.get("cur_members", 0)) <= 0:
                        continue
                    t = time.monotonic()
                    try:
                        rr = sess.room_members(rid)
                    except Exception as e:
                        steps.append({"step": f"Room members (room {rid})", "ok": False,
                                      "detail": str(e),
                                      "ms": int((time.monotonic() - t) * 1000)})
                        continue
                    steps.append({"step": f"Room members (room {rid})",
                                  "ok": bool(rr.get("ok", False)),
                                  "detail": rr.get("text", ""),
                                  "ms": int((time.monotonic() - t) * 1000)})

        # ContextStop
        t = time.monotonic()
        try:
            rcx = sess.context_stop()
        except Exception as e:
            steps.append({"step": "ContextStop", "ok": False, "detail": str(e),
                          "ms": int((time.monotonic() - t) * 1000)})
        else:
            steps.append({"step": "ContextStop",
                          "ok": bool(rcx.get("ok", False)),
                          "detail": rcx.get("text", ""),
                          "ms": int((time.monotonic() - t) * 1000)})

    # clean disconnect
    t = time.monotonic()
    try:
        tr = sess.terminate()
    except Exception as e:
        tr = {"ok": False, "text": str(e)}
    steps.append({"step": "Terminate (clean disconnect)",
                  "ok": bool(tr.get("ok", False)),
                  "detail": tr.get("text", ""),
                  "ms": int((time.monotonic() - t) * 1000)})
    sess.close()

    overall_ok = all(s.get("ok") for s in steps) and True
    return _wrap(sess, steps, total_start, overall_ok)


def _login_detail(r):
    bits = [r.get("text", "")]
    if r.get("friends"):
        n_on = sum(1 for _n, o in r["friends"] if o)
        bits.append(f"friends {len(r['friends'])} ({n_on} online)")
    return " ".join(b for b in bits if b)


def _wrap(sess, steps, total_start, ok):
    return {"server": sess.server, "npid": sess.npid,
            "version": sess.version, "user_id": sess.user_id,
            "broadcast_online": [], "friends": sess.friends,
            "worlds": sess.worlds, "rooms": sess.all_rooms,
            "steps": steps, "ok": bool(ok),
            "seconds": time.monotonic() - total_start}