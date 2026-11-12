#pragma once

#include <I18n.h>

#include <iterator>

#include "CrossPointSettings.h"

// Labels and multipliers follow the persisted LINE_COMPRESSION order.
inline constexpr StrId LINE_SPACING_IDS[] = {
    StrId::STR_SPACING_0_PERCENT,       StrId::STR_SPACING_PLUS_10_PERCENT, StrId::STR_SPACING_PLUS_20_PERCENT_DEFAULT,
    StrId::STR_SPACING_PLUS_30_PERCENT, StrId::STR_SPACING_PLUS_45_PERCENT, StrId::STR_SPACING_PLUS_60_PERCENT,
    StrId::STR_SPACING_PLUS_75_PERCENT};
inline constexpr float LINE_SPACING_MULTIPLIERS[] = {1.0f, 1.1f, 1.2f, 1.3f, 1.45f, 1.6f, 1.75f};
static_assert(std::size(LINE_SPACING_IDS) == CrossPointSettings::LINE_COMPRESSION_COUNT);
static_assert(std::size(LINE_SPACING_MULTIPLIERS) == CrossPointSettings::LINE_COMPRESSION_COUNT);

// Labels and multipliers follow the persisted SCRIPT_LINE_SPACING order.
inline constexpr StrId SCRIPT_LINE_SPACING_IDS[] = {
    StrId::STR_SPACING_MINUS_15_PERCENT, StrId::STR_SPACING_MINUS_10_PERCENT, StrId::STR_SPACING_MINUS_5_PERCENT,
    StrId::STR_SPACING_0_PERCENT,        StrId::STR_SPACING_PLUS_5_PERCENT,   StrId::STR_SPACING_PLUS_10_PERCENT,
    StrId::STR_SPACING_PLUS_15_PERCENT,  StrId::STR_SPACING_PLUS_20_PERCENT,  StrId::STR_SPACING_PLUS_30_PERCENT};
inline constexpr float SCRIPT_LINE_SPACING_MULTIPLIERS[] = {0.85f, 0.9f, 0.95f, 1.0f, 1.05f, 1.1f, 1.15f, 1.2f, 1.3f};
static_assert(std::size(SCRIPT_LINE_SPACING_IDS) == CrossPointSettings::SCRIPT_LINE_SPACING_COUNT);
static_assert(std::size(SCRIPT_LINE_SPACING_MULTIPLIERS) == CrossPointSettings::SCRIPT_LINE_SPACING_COUNT);
