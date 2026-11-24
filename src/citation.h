// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#pragma once

#include "document.h"

#include <QPointF>
#include <optional>

// For citations that aren't links: reads the citation under `point`
// ("[12]", "[3, 4]", "Ref. 12", "Smith et al. (2019)") and finds the
// matching entry in the document's reference list.
std::optional<LinkTarget> findCitationTarget(const Document& doc, int page, QPointF point);
