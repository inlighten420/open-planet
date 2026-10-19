// Geodrive tile tool - AGPL-3.0 (see LICENSE)
//
// How far a long job has got, said as it goes ("  37% (reading the map)": one line a percent at most) so the game's loading
// bar can follow it: chunks done out of the chunks there are, not a guess.

#pragma once

#include <cstdio>
#include <mutex>

namespace geotile
{
	/** The stretch of the whole job's progress (0 to 1) that a loop fills as its chunks finish */
	struct ProgressSpan
	{
		double From = 0.0;
		double To = 0.0;
		const char* What = "";
	};

	/** Says the progress when its whole percent has gone up (bReset: starts again from nothing) */
	inline void ReportProgress(double Fraction, const char* What, bool bReset = false)
	{
		static std::mutex Lock;
		static int Last = -1;
		std::lock_guard<std::mutex> Guard(Lock);
		if (bReset)
		{
			Last = -1;
			return;
		}
		const int Percent = Fraction < 0.0 ? 0 : (Fraction > 1.0 ? 100 : int(Fraction * 100.0));
		if (Percent > Last)
		{
			Last = Percent;
			std::printf("  %d%% (%s)\n", Percent, What);
			std::fflush(stdout);
		}
	}

	/** A chunk of a loop with Count chunks has finished (Finished so far, this one included) */
	inline void ReportChunk(const ProgressSpan* Span, size_t Finished, size_t Count)
	{
		if (Span && Count > 0)
		{
			ReportProgress(Span->From + (Span->To - Span->From) * double(Finished) / double(Count), Span->What);
		}
	}
}
