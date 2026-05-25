// SPDX-License-Identifier: GPL-2.0-or-later

// mirakc-arib
// Copyright (C) 2019 masnagam
//
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
// the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program; if
// not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301, USA.

#pragma once

#include <tsduck/tsduck.h>

#include "logging.hh"

namespace {

// ---- PAT ----

inline bool ValidatePat(const char* tag, const ts::PAT& pat, const ts::BinaryTable& table) {
  // Ignore a strange PAT delivered with PID#0012 around midnight at least on
  // BS-NTV and BS11 channels.
  //
  // This PAT has no NIT PID and its ts_id is 0, for example:
  //
  //   * PAT, TID 0 (0x00), PID 18 (0x0012)
  //     TS id:       0 (0x0000)
  //     Program: 19796 (0x4D54)  PID: 2672 (0x0A70)
  //     Program: 28192 (0x6E20)  PID: 6205 (0x183D)
  //     ...
  //
  if (table.sourcePID() != ts::PID_PAT) {
    MIRAKC_ARIB_WARN("{}: PAT delivered with PID#{:04X}, skip", tag, table.sourcePID());
    return false;
  }
  if (!pat.isValid()) {
    MIRAKC_ARIB_WARN("{}: Broken PAT, skip", tag);
    return false;
  }
  if (pat.ts_id == 0) {
    MIRAKC_ARIB_WARN("{}: PAT for TSID#0000, skip", tag);
    return false;
  }
  return true;
}

// ---- CAT ----

inline bool ValidateCat(const char* tag, const ts::CAT& cat) {
  if (!cat.isValid()) {
    MIRAKC_ARIB_WARN("{}: Broken CAT, skip", tag);
    return false;
  }
  return true;
}

// ---- PMT ----

inline bool ValidatePmt(const char* tag, const ts::PMT& pmt) {
  if (!pmt.isValid()) {
    MIRAKC_ARIB_WARN("{}: Broken PMT, skip", tag);
    return false;
  }
  return true;
}

// ---- Other tables ----

inline bool ValidateTot(const char* tag, const ts::TOT& tot) {
  if (!tot.isValid()) {
    MIRAKC_ARIB_WARN("{}: Broken TOT, skip", tag);
    return false;
  }
  return true;
}

inline bool ValidateEit(const char* tag, const ts::EIT& eit) {
  if (!eit.isValid()) {
    MIRAKC_ARIB_WARN("{}: Broken EIT, skip", tag);
    return false;
  }
  return true;
}

inline bool ValidateNit(const char* tag, const ts::NIT& nit) {
  if (!nit.isValid()) {
    MIRAKC_ARIB_WARN("{}: Broken NIT, skip", tag);
    return false;
  }
  return true;
}

inline bool ValidateSdt(const char* tag, const ts::SDT& sdt) {
  if (!sdt.isValid()) {
    MIRAKC_ARIB_WARN("{}: Broken SDT, skip", tag);
    return false;
  }
  if (sdt.ts_id == 0) {
    MIRAKC_ARIB_WARN("{}: SDT for TSID#0000, skip", tag);
    return false;
  }
  return true;
}

}  // namespace
