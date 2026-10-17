// Geodrive Godot client - GPL (see Core/README.md)

#pragma once

/** The memory this game itself uses (bytes; 0 where it can't be told) */
double ProcessMemoryBytes();

/** This game's share of the whole processor since the last call (percent, 0-100; read every half second or so) */
double ProcessCpuPercent();
