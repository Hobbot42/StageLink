#pragma once

// FxQ
// Brand-wide product identity values, shared by every FxQ board (TxQ,
// RxQ, and future nodes). This is the single source of truth for
// version/hardware/link identity - bump PRODUCT_VERSION here only.
// Per-product headers (TxQInfo.h / RxQInfo.h) include this file and add
// just their own PRODUCT_NAME alongside it.
// Belongs to: StageLink_Common (shared by TX and RX).

namespace FxQ
{
    constexpr const char *PROJECT_BRAND = "FxQ";

    // Bump this for every release. Nothing else needs to change.
    constexpr const char *PRODUCT_VERSION = "v0.9.0";

    // For future hardware variations - not user-configurable yet.
    constexpr const char *HARDWARE_REVISION = "A";

    // Communication compatibility identifier, shown to users as
    // "FxQ Link" rather than "Protocol". Independent of PRODUCT_VERSION -
    // only needs to change when the wire format itself changes.
    constexpr const char *LINK_VERSION = "v1";
}
