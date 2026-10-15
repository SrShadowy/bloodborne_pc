# netlab — network connection tester (Bloodborne/mp)

Standalone utility that does not touch the launcher. Emulates the game's shadNet
boot as far as its "enter online" point (ServerInfo ... Login ... matching
world list / rooms / members, then a clean Terminate), reads the public
Hunter's Dream map API (hunters abroad now, and messages on the maps), and
reports timings per step.

Usage (from the repo root):

    python3 tools/netlab/netlab.py boot          # full shadNet entry
    python3 tools/netlab/netlab.py boot --json   # machine readable
    python3 tools/netlab/netlab.py login         # single login verdict
    python3 tools/netlab/netlab.py map           # players + messages on maps
    python3 tools/netlab/netlab.py map --events  # include deaths/kill/boss counts

Credentials are read from ~/.config/bbport-launcher/settings.json (NPID,
password, server, community, WebAPI). CLI flags and env vars (BB_SHADNET_*) override.

Notes:
  - Nothing here creates rooms, P2P or STUN sessions. Matching calls are
    GetWorldInfoList/SearchRoom/GetRoomMemberDataExternalList only.
  - ContextStart and the whole matching half are skipped if the server reports
    matching2 disabled.
  - The bearer token from GetToken is used only to count its length; it is never
    stored or printed.
  - Only public site endpoints (/api/map/*) are contacted on The Hunter's Dream;
    no account/auth endpoints are touched.
  - Clean disconnect (Terminate) at the end avoids the temporary
    "LoginError (5)" busy state the server enters after an abrupt socket close.