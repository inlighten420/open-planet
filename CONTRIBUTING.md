# Help make Open-Planet

Open-Planet is the real world, built from open map data, to drive, walk, fly and build in. The map is drawn by
millions of people, mostly by hand, so it's a guideline more than the law: some roads come out wrong. You can help put
them right **without writing a line of code**, from inside the game. Thank you for being here.

## Without coding

### Fix a road (the most useful thing you can do)

1. Go to the road that's wrong, in the game.
2. Press **F2**: the road editor. Hold the right mouse button to look round; WASD, E and Q to fly.
3. Press **1 2 3 4** to hide the ground, buildings, trees and water, so you see just the roads (and again to bring
   them back, to see how they fit).
4. Drag the road's points to where the road really is: sideways with the left mouse button, up and down with the mouse
   wheel. **-** and **=** make it narrower or wider. Double-click a road to add a point, **L** to make your line flow
   like a real road, **G** to colour the roads by how steep they are. **H** shows every key.
5. **Ctrl+C**, then **F6**: your browser opens a form on our GitHub page with your edit filled in. Say **what's wrong,
   and what to look for**, and Submit.

Your drawing doesn't need to be perfect: it's a guideline. We don't copy it into the game; we use it to make the
game's rules build roads like it right, **everywhere**. Every edit you send becomes a test the game has to pass, so one
fix in your street can fix a thousand streets like it.

Good things to send: a ramp that bends into the road it leaves, a junction that doesn't join, a tunnel too low for a
bus, a road in the wrong place, a road far too wide or too narrow, a roundabout that's too small.

### Show us what's wrong

- **A picture**: **F12** (it remembers exactly where it was taken), then **F6**.
- **A recording**: **F4** to start, do what goes wrong, **F4** to stop, then **F6**. It writes down your keys and what
  the game did with each, so we can see what happened.
- **A bug report**: Esc > Settings > Game > Something wrong?: write a few words and press Report a bug.

**F6** always sends the last thing you made. For pictures and recordings, the folder with the file opens beside the form:
drag the file into it. You need a free [GitHub account](https://github.com/signup) to post.

### Fix OpenStreetMap itself

If the map itself is wrong (a road that isn't there, a tunnel on the wrong side), fixing
[OpenStreetMap](https://www.openstreetmap.org) helps everyone who uses it, not just us. Please follow its rules:

- **Only use what you know or what OSM allows**: your own knowledge of the place, the aerial photos inside the OSM
  editor, or free street photos (Mapillary, Panoramax). **Never copy from Google Maps, Google Earth or Street View**:
  it's against OSM's rules (copyright), and the edit will be removed.
- Edit with your own OSM account, and say in the changeset what you fixed and how you know.
- Tunnels are hard: nobody can see them from the air. Only change one if you've been through it or have street photos
  from inside.

Fixes reach the game the next time that area's map is packed (usually within days).

### Other ways to help

- **Play somewhere new** and tell us what's wrong there: new places find new problems.
- **Try it on your PC**: a different graphics card, a gamepad, a wheel, a keyboard in another language. Tell us what
  doesn't work (F4 records it).
- **Tell people about it**, and share your pictures (F12).

## With code

The game is free software (AGPL-3.0): C++ for the world (Core/), Godot for the game (Godot/), tools in Tools/.
Start with [docs/HOW_IT_WORKS.md](docs/HOW_IT_WORKS.md) (the short tour) and
[docs/DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md) (where things live and the rules the code follows). The one rule
above all: **the map is a guideline, drivability is the law**; fix the rules that build every road, never one place by
hand.
