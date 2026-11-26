#pragma once

#include "model/workspace.h"

namespace phos::io {

// Sample -> BRR for a SNES instrument (PLAN.md §7.4): mono mix, resample to the source rate,
// loop start snapped to a 16-sample block; with AUTO-ALIGN the sample is resampled so the loop
// length is a multiple of 16 (the BRR's rate is adjusted so the pitch stays right). Uses the
// instrument's sampler loop (if on) and snes.source_rate / snes.auto_align.
model::BrrRef encode_for_snes(const model::SampleData& sample, const model::SamplerParams& sampler,
                              const model::SnesParams& snes);

// ARAM use (PLAN.md §7.4 budget meter): BRR bytes of every SNES instrument + echo buffer + driver.
std::size_t aram_bytes(const model::Document& doc);

} // namespace phos::io
