# Make a creature (and a pack)

A creature is a file. You don't need any code. None of the game's animals are built into the code: all 19 are files
in the hunting pack, `Godot/packs/hunting/creatures/`, and they're the best examples to copy.

The easiest way is in the game: on foot, B to build, Q, then **+ Make something new**, **Creature**. Pick one to start
from, change its shape and what it does while you watch it, and Save it. It's written as a file like these, in
`<Saved>/packs/workshop/creatures/`, and the game maker offers it straight away.

## Packs

A game in Geodrive is a pack: a folder of files. The game's own packs are in `Godot/packs/`. Yours go in the game's
Saved folder:

```
<Saved>/packs/<your pack>/
    pack.json            its name, what it is, its sources: {"name": "...", "about": "...", "sources": ["..."]}
    creatures/*.json     its creatures (this page)
    weapons/*.json       its weapons (below)
    reserves/*.json      its game farms: outline, gates, water, and stock
```

`<Saved>` is the game's own folder for your files: `%APPDATA%\Godot\app_userdata\Geodrive` in a packaged game, `Saved`
beside the project when you run it from the editor. Every file is read when the game starts, before any hunt: the
game's packs first, then yours, and each pack's files in name order.

A file holds one creature, or a list of them `[ {...}, {...} ]`. If a creature uses the id of one that already exists
(even one of the hunting pack's, like `warthog`), it replaces that one. Only the values you give change. `like` can
name any creature from an earlier file or pack.

## The smallest one

Start from an animal that's close, and give only what's different:

```json
{ "id": "nyala", "name": "Nyala", "like": "kudu", "mass": 85, "shoulder": 1.05, "length": 1.6, "colour": "#6b5040" }
```

## Everything a creature can have

Use real figures for an adult: the average of male and female.

| key | what | example |
|---|---|---|
| `id` | its name in files, lower case | `"nyala"` |
| `name` | what the game calls it | `"Nyala"` |
| `like` | another creature's id to start from | `"kudu"` |
| `mass` | kg | `85` |
| `shoulder` | height at the shoulder, m | `1.05` |
| `length` | head and body, m | `1.6` |
| `walk`, `run` | m/s: its walk, and the flat-out run it can keep up | `1.2`, `14` |
| `herd` | the fewest and the most in a herd | `[2, 8]` |
| `diet` | `grazer`, `browser`, `mixed` or `carnivore` | `"browser"` |
| `colour` | its coat, as a picture's colour | `"#6b5040"` |
| `horns` | whether it has any | `true` |
| `protected` | never to be hunted | `false` |
| `season` | open season, months from and to (`[0, 0]`: closed) | `[6, 7]` |
| `bag` | how many can be shot in a day | `1` |
| `shape` | how its body is built (below) | |

`shape`:

| key | what | default |
|---|---|---|
| `head_up` | how high it carries its head, times its shoulder height | `1.08` |
| `torso_depth` | its body's half depth, times its shoulder height (`-1`: set by its mass) | `-1` |
| `back_slope` | how much lower its rump is than its shoulders (a giraffe has `0.12`) | `0` |
| `ears` | ear size | `1` |
| `horns` | `none`, `pair` or `nose` (a rhino's) | `pair` if it has horns |
| `horn_length` | m (`-1`: set by its mass) | `-1` |
| `bend` | each horn in two lengths: back from upright, out to the side, then back and out again (radians) | `[0.35, 0.2, 0.5, 0.1]` |
| `mane` | `none`, `lion` or `crest` | `none` |
| `long_tail` | a tail with a tuft at the end | `false` |
| `coat` | `plain`, `countershade`, `stripes`, `patches`, `mask` or `pinstripes` | `countershade` |
| `frame` | `four_legs` (an animal) or `upright` (a person's build: two legs, two arms; `shoulder` is its shoulder height, and its whole height is about 1.2 times that) | `four_legs` |
| `arms_forward` | upright: its arms held out in front instead of hanging | `false` |

An upright body's hit zones: the head is the brain, the chest the heart and lungs, the hips the gut, and the arms and
legs are limbs.

## How it behaves

`behaviour` is a set of building blocks, each with its own numbers. A creature behaves according to its `diet` unless
you say otherwise:
- a grazer eats grass;
- a browser eats from bushes;
- a mixed feeder does both;
- a carnivore rests between hunts, hunts, and isn't prey.

Give only the blocks you want to change. Times are in seconds and distances in metres; `[a, b]` means a random time
between a and b each time.

| key | what | default |
|---|---|---|
| `browsing` | how many of its meals are at a bush rather than grass, 0 to 1 | by diet: 0, 1, or 0.4 |
| `rests` | lies at rest instead of grazing (a lion) | carnivores |
| `meal` | how long it eats at one patch | `[60, 240]` |
| `thirsty_after` | how long until it walks to water again (`[0, 0]`: never drinks) | `[400, 800]` |
| `hears` | how far off it hears a shot | `400` |
| `wary` | how long it stays on guard after a shot, head up, looking that way | `[15, 40]` |
| `flees_for` | how long its herd runs from a shot | `[8, 15]` |
| `prey` | whether hunters go after it | not carnivores |
| `hunts` | `false`, `true`, or the hunt's numbers (below) | carnivores |
| `flees` | runs from a shot (its herd with it) | `true` |
| `vital` | the zones a wound kills from (any of `brain`, `spine`, `heart_lung`, `gut`, `neck`); a wound anywhere else only hurts | all five |
| `wounds_to_drop` | this many wounds that don't kill it drop it anyway (a zombie's 4); 0: never | `0` |
| `goes_for_people` | `false`, `true`, or its numbers (below): it comes after people on foot while they're playing | `false` |

Cars: every creature is pushed out of a car's way and hit by it. Slowly, it's only bumped, and runs off if it `flees`.
Fast, it's killed: from about 18 km/h plus 0.036 km/h for every kg it weighs (a duiker about 20 km/h, an eland about
40, a rhino about 90). From half that speed it's wounded. The car loses speed by the creature's share of their
combined weight. One that `flees` runs from a car coming at it within 25 m, and looks up at one coming within 60 m.

`goes_for_people`. It goes for the nearest person within reach, straight at them and round buildings, and catches
them at arm's length. Someone caught is down for 5 seconds, then gets up by their car.

| key | what | default |
|---|---|---|
| `within` | how far off it notices someone | `40` |
| `speed` | how fast it comes, times its `run` | `1` |
| `reach` | how close it has to get to catch them | `1` |
| `gives_up` | how far off they can get before it gives up | `120` |

`hunts`. It hunts the nearest prey in reach, never one of its own herd. It closes in at a walk, then rushes. The prey
bolts when the rush starts. If it catches it, its herd feeds there.

| key | what | default (a lion's) |
|---|---|---|
| `every` | how long between hunts | `[480, 900]` |
| `within` | how far off it looks for prey | `800` |
| `prey_up_to` | the heaviest prey it takes, kg | `700` |
| `rush_from` | how close it walks before it rushes | `40` |
| `rush_for` | how long it can keep the rush up (the first 8 s fastest) | `20` |
| `rush_speed` | its top rush speed, times its `run` | `1.25` |
| `feeds_for` | how long its herd feeds on a kill | `[500, 700]` |

A zebra that hunts small antelope, never drinks, and ignores gunshots:

```json
{ "id": "odd_zebra", "like": "zebra", "behaviour": { "hunts": { "every": [60, 90], "prey_up_to": 80 }, "thirsty_after": [0, 0], "hears": 0 } }
```

To see any creature written out in full, built-in ones included, call `Creatures.save("kudu", "<path>.json")`. Change
that file and put it in a pack.

If a file has a mistake (run slower than walk, an unknown word, a herd of 0, a missing comma), the game's log says
what's wrong and in which file (with the line number, if the JSON itself is broken), and leaves that creature out.

To put your creature on a game farm, add it to the farm's `stock` in a pack's `reserves/*.json`, as
`["<id>", how many]`.

## Areas

An area (a pack's `reserves/*.json`) is where its creatures live: a game farm, or a whole town. The zombie pack's
George zone (`Godot/packs/zombies/reserves/george.json`) is the example of a town:

| key | what |
|---|---|
| `name`, `about` | what it's called, and what it is |
| `outline` | its edge, `[[lat, lon], ...]`: its creatures never leave it |
| `gates` | `[{ "name", "lat", "lon", "tickets": true }]`: a gate with `tickets` has the ticket booth |
| `water` | `[{ "lat", "lon" }]`: where its creatures drink |
| `stock` | `[["<id>", how many], ...]` |
| `fence` | a game fence round it, with its gates in it (`true`, a game farm's); `false`: no fence, and each booth is just where its gate's `lat` and `lon` put it |
| `avoid_buildings` | its creatures keep out of the map's buildings (a town) |
| `creatures_out` | `always` (a game farm's: seen from the road), or `playing` (only once you've its ticket and are in it) |
| `spawn` | `{"around_people": true, "nearest": 60, "farthest": 150, "leave_past": 250, "dead_for": 30}`: its creatures a pool kept round the players instead of spread over it. One farther than `leave_past` m from everyone, or dead for `dead_for` s, comes back `nearest`-`farthest` m from one of them, on open ground (George's 100 zombies are always round you) |
| `texts` | the booth's and the game's words: `sign` (on the booth), `booth` (the big sign, `%s` its name), `title` (the ticket window, `%s` its name), `law` (`false`: no hunting law shown), `note`, `hint` (near the booth), `ticket` (once bought: `%s` the area, `%s` the weapon), `start` (once playing: `%d` how many creatures), `bag` (what the score's called at the end) |

The game is on while you're inside the area with its ticket, and over once you leave it. Your creatures come out when
you're within the area's size of its middle, plus a kilometre.

## Weapons

A weapon is a file in a pack's `weapons/` folder: a rifle's load or a bow, with its published figures:

```json
{ "id": "308win", "name": ".308 Winchester, 150 gr", "grains": 150, "bc": 0.387, "muzzle": 860, "zero": 100, "sight_height": 0.045, "magazine": 4, "arrow": false }
```

| key | what |
|---|---|
| `grains` | the bullet's (or arrow's) weight |
| `bc` | a bullet's G1 ballistic coefficient (an arrow: 0, since its drag is worked out separately) |
| `muzzle` | m/s as it leaves the barrel (or bow) |
| `zero` | m: where the sights meet the bullet's path |
| `sight_height` | m: how far the sight is above the bore |
| `magazine` | rounds in a full magazine (an arrow at a time: 1) |
| `arrow` | a bow's arrow, not a bullet |
| `action` | `bolt` (a round a pull, then the bolt's worked), `semi` (a round a pull), `auto` (rounds while the trigger's held) |
| `rate` | an automatic's rounds a minute (60 to 2,000) |
| `rounds` | rounds carried, the magazine's included (default 20) |
| `reload_time` | s to put a full magazine in (default 3) |
| `sight` | `scope` (~9x) or `iron` (plain sights, a little closer) |
| `kick` | how far the muzzle jumps a shot: 1 is a .308 hunting rifle's |
| `model` | what's in your hands: `rifle` or `smg` |

Automatic fire isn't legal for hunting game, so a full-auto shot at the game farm counts as illegal. A game area whose
`texts` say `"law": false` (the zombie zone) has no hunting law.

## The workshop

You can also make weapons in the game: building (B), the build menu (Q), the **Workshop** page. Start from a weapon there
is, or a cartridge (9 mm, .22 LR, 5.56 mm, 7.62x39 mm, .243, .308, .375 H&H, a 12-gauge slug: each one's real bullet and
muzzle speed). Then choose the action and rate, the magazine, the rounds carried, the barrel (the muzzle speed), the
sights and the model. As you go, it shows the energy at the muzzle and 100 m out and the drop there; the kick is worked
out from the bullet's momentum. **Save it** writes it into `<Saved>/packs/workshop/weapons/`, and it's at every ticket
booth from then on. With a ticket, **Take it now** puts it straight into your hands. The workshop will grow a page for
each kind of thing you can make.

**How it looks** is the shape editor (the same one will make props, cars and clothing). Add a box, a wedge, a
cylinder, a cone, a sphere or a tube; pick one (in the list, or click it in the view; left drag turns the view, the
wheel zooms) and move, size and turn it, colour it (the palette, or any colour) and give it a finish (matte, plastic,
metal, wood). A **cut** takes its shape out of everything else: a window in a box, a notch in a sight, a hollow
barrel. **Aim over this** makes a part the sight: aiming with plain sights, the gun comes up to your eye and you look
along it over that part. In the file it's the weapon's `look`: `{"parts": [{"shape", "at", "size", "turn", "colour",
"finish", "cut"}], "sight_at", "eye_relief"}` (metres; x right, y up, z towards you; turns in degrees).

The ticket kiosk offers every weapon there is. The law's limits for bows (the least arrow weight and energy for an
animal's size) are the Western Cape's guideline, and those are in the code.

In multiplayer, everyone needs the same packs for now. A joiner whose creatures differ from the host's sees their own
animals, not the host's.

## Props

A prop is anything you put on the map: a bench, a sign, a gate, a crate. Make one in the build menu's **Workshop**,
**Props**: the same shape editor, at a prop's scale (a 5 cm grid). **Save it**, or **Place it** to put it straight away.
In build mode, the build menu's **Props** page lists every prop there is; choose one, look where it goes (it's green
where it can, red where it can't: it needs something near level under it), **R** turns it, a click puts it. Its solid
parts are solid. It's kept where you put it (`user://props.json`) and is back whenever that part of the world is built.
Right click a prop twice to take it away.

A prop is a file in a pack's `props/` folder: `{"id": "...", "name": "...", "look": {"parts": [...]}}`. The workshop's
are in `<Saved>/packs/workshop/props/`; a game pack can ship its own.

## Games

Make a game in the build menu's **Workshop**, **Games**: name it, then **Mark it on the map**. The M map opens: click
each corner of its area (right click takes the last back, the wheel zooms), Enter when it's done. Put its booth **where
you're standing**. Choose what's in it (how many of each creature: any pack's), which weapons its booth offers (none
chosen: all), and how it goes: its creatures spread over the area or a pool round the players, out always or only while
it's played, a game fence or none, the hunting law or none. **Save it**: it's an area in your pack
(`<Saved>/packs/workshop/reserves/`), its booth's on the map at once, and F1 there (or E at the booth) plays it.

### Rules

A game's **rules** say what happens when. In the game maker, set **N** and press a quick rule (+N a kill, score N
wins, kill N to win, N minutes then lost, survive N minutes, caught N times: lost); each rule is listed in words,
with **x** to take it out. While it's played, the game's line shows its score (of what wins) and its time left; a win
or a loss is shown big and the game's over. In the area's file:

```json
"rules": [
  {"when": "start", "then": [{"do": "timer", "seconds": 300}]},
  {"when": "kill", "of": "zombie", "then": [{"do": "score", "by": 1}]},
  {"when": "score", "reaches": 25, "then": [{"do": "win", "text": "25 zombies: you've won"}]},
  {"when": "time_up", "then": [{"do": "lose", "text": "Time's up"}]}
]
```

| when | what it needs |
|---|---|
| `start` | |
| `kill` | `of`: a creature's id (none: any) |
| `caught` | |
| `score` | `reaches`: N (once) |
| `kills` | `count`: N, `of` (once) |
| `caught_times` | `count`: N (once) |
| `after` | `seconds`: N from the start (once) |
| `every` | `seconds`: N |
| `time_up` | the timer's run out (once) |

| do | what it needs |
|---|---|
| `score` | `by`: N (taken off if it's less than 0) |
| `win`, `lose` | `text`: what's said; the game's over |
| `say` | `text` |
| `timer` | `seconds`: a countdown from there |
| `time` | `seconds`: added to the timer |

The rules run in Core (`Core/GeoGame`), as the animals do: a server can run a game on its own.

### Races

A game can be a race. In the game maker, **A race**: **Mark checkpoints on the map** (click each in order, the start
first; Enter when done) or **Add one where I am** as you drive or walk the way. **laps** 0 is point to point (the last
checkpoint's the finish); 1 or more goes round a circuit, back to the start each lap. Its weapons: **None (a race)**,
so its booth only says **Take part**. With no area marked, its area is round the course (150 m out). The quick rules
**Race: finish wins** and **Finish in N s** go with it. While it's raced, the next checkpoint's a numbered column, the
game's line shows which and how far, the time and the lap; a checkpoint reached out of order doesn't count. At the
finish, your time and your best (kept in the game's user folder, `race_times.json`). In the area's file:

```json
"checkpoints": [{"lat": -33.9631, "lon": 22.4617, "radius": 8}, {"lat": -33.9650, "lon": 22.4640, "radius": 8}],
"laps": 0,
"rules": [{"when": "finish", "then": [{"do": "win", "text": "Finished"}]}]
```

| when | what it needs |
|---|---|
| `checkpoint` | `of`: its number from 0, the start (none: each) |
| `lap` | each lap done |
| `finish` | (once) |

## Playing together

In multiplayer, what the host's made comes to everyone who joins: every pack in the host's Saved folder (its weapons,
creatures, props and games; the game's own packs everyone has already), and again whenever the host saves something new.
The host's games are there with their booths (yours too, alongside); the props on the host's map are what's on yours.
Put a prop or take one away and the host does it, so everyone's map stays the same (each prop's height is kept above
the ground, as each player's ground is their own). Nothing that comes is kept: leave, and your own props and games are
back as they were. Each file is checked as it comes (Core turns away a creature or weapon that can't be, and says why).
