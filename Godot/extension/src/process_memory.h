// Geodrive Godot client - AGPL-3.0 (see LICENSE)

#pragma once

/** The memory this game itself uses (bytes; 0 where it can't be told) */
double ProcessMemoryBytes();

/** This game's share of the whole processor since the last call (percent, 0-100; read every half second or so) */
double ProcessCpuPercent();
