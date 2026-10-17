# Playing together

There are no Open-Planet servers yet: one of you hosts the game on their own PC and the others join it. Up to 16 players.

## The basics

1. **Everyone needs the same version** of Open-Planet (the same release zip, or the same code).
2. **Download the place first.** Everyone should download the same region before playing (Play somewhere new, click
   it on the world map), so their game can build the same world.
3. **The host:** Play with friends (or the phone's Friends page, Tab), then Host. The host's area is where everyone
   plays; when the host goes somewhere new, everyone goes with them.
4. **Friends:** Play with friends, then type the host's address and Join. You arrive on the road beside the host.

What the host has made (guns, props, games, races) comes to everyone who joins, while they're joined. Traffic and the
robot driver are only in single player.

The game talks on **UDP port 7777**. The first time you host, Windows may ask whether to let Open-Planet through the
firewall: allow it (Private networks is enough for the first two ways below).

## Which address? Three ways to connect

### 1. In the same house (the same Wi-Fi or router)

The easiest. The host finds their PC's address: press Windows + R, type `cmd`, Enter, then type `ipconfig` and look for
**IPv4 Address** (something like `192.168.1.23`). Friends join with that.

### 2. Over the internet with Tailscale (recommended)

[Tailscale](https://tailscale.com) is free for personal use and joins your PCs into one private network, as if you were
in the same house. Nothing to set up on your router.

1. Everyone installs Tailscale and signs in.
2. The host shares their PC with their friends: in the Tailscale admin page, **Share** the host's machine and send the
   invite link (or add friends as users of the same network).
3. The host's Tailscale address is shown in the Tailscale app (it starts with `100.`). Friends join with that.

Other tools that do the same job: ZeroTier, Radmin VPN.

### 3. Over the internet by opening a port (for people who know their router)

1. On the host's router, forward **UDP port 7777** to the host PC's address (from `ipconfig`, as in 1).
2. Allow Open-Planet through the Windows firewall on **Public** networks too.
3. The host finds their public address (search "what is my IP" in a browser). Friends join with that.

Only do this if you're comfortable with it: an open port is reachable by anyone on the internet. Close it again when
you're done.

## When it won't connect

- **Different versions:** check everyone has the same release.
- **The firewall:** Windows Security, Firewall and network protection, Allow an app through firewall: tick Open-Planet.
- **"Couldn't host (port 7777 in use?)":** another copy of the game is already running on that PC. Close it.
- **Tailscale:** both PCs show as connected in the Tailscale app, and the friend can see the host's machine.
- **Hosting someday:** proper always-on servers, where anyone can host a region for others, are planned. If you'd like
  to help build that, see Contributing in the [README](../README.md).
