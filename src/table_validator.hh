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

// Structural/safety validation results for PSI/SI tables.
enum class TableValidateResult {
  kOk,
  kWrongPatPid,
  kBrokenTable,
  kZeroPatTsId,
  kZeroSdtTsId,
};

// ---- PAT ----

inline TableValidateResult ValidatePat(const ts::PAT& pat, const ts::BinaryTable& table) {
  if (table.sourcePID() != ts::PID_PAT)
    return TableValidateResult::kWrongPatPid;
  if (!pat.isValid())
    return TableValidateResult::kBrokenTable;
  if (pat.ts_id == 0)
    return TableValidateResult::kZeroPatTsId;
  return TableValidateResult::kOk;
}

// ---- CAT ----

inline TableValidateResult ValidateCat(const ts::CAT& cat) {
  if (!cat.isValid())
    return TableValidateResult::kBrokenTable;
  return TableValidateResult::kOk;
}

// ---- PMT ----

inline TableValidateResult ValidatePmt(const ts::PMT& pmt) {
  if (!pmt.isValid())
    return TableValidateResult::kBrokenTable;
  return TableValidateResult::kOk;
}

// ---- Other tables ----

inline TableValidateResult ValidateTot(const ts::TOT& tot) {
  return tot.isValid() ? TableValidateResult::kOk : TableValidateResult::kBrokenTable;
}

inline TableValidateResult ValidateEit(const ts::EIT& eit) {
  return eit.isValid() ? TableValidateResult::kOk : TableValidateResult::kBrokenTable;
}

inline TableValidateResult ValidateNit(const ts::NIT& nit) {
  return nit.isValid() ? TableValidateResult::kOk : TableValidateResult::kBrokenTable;
}

inline TableValidateResult ValidateSdt(const ts::SDT& sdt) {
  if (!sdt.isValid())
    return TableValidateResult::kBrokenTable;
  if (sdt.ts_id == 0)
    return TableValidateResult::kZeroSdtTsId;
  return TableValidateResult::kOk;
}

// ---- Utilities ----

inline const char* TableName(uint8_t table_id) {
  switch (table_id) {
    case ts::TID_PAT:
      return "PAT";
    case ts::TID_CAT:
      return "CAT";
    case ts::TID_PMT:
      return "PMT";
    case ts::TID_NIT_ACT:
      return "NIT";
    case ts::TID_SDT_ACT:
      return "SDT";
    case ts::TID_EIT_PF_ACT:
      return "EIT[p/f]";
    case ts::TID_TOT:
      return "TOT";
    default:
      return "table";
  }
}

inline void LogValidateError(
    const char* tag, TableValidateResult result, const ts::BinaryTable& table) {
  switch (result) {
    case TableValidateResult::kWrongPatPid:
      MIRAKC_ARIB_WARN("{}: PAT delivered with PID#{:04X}, skip", tag, table.sourcePID());
      break;
    case TableValidateResult::kBrokenTable:
      MIRAKC_ARIB_WARN("{}: Broken {}, skip", tag, TableName(table.tableId()));
      break;
    case TableValidateResult::kZeroPatTsId:
      MIRAKC_ARIB_WARN("{}: PAT for TSID#0000, skip", tag);
      break;
    case TableValidateResult::kZeroSdtTsId:
      MIRAKC_ARIB_WARN("{}: SDT for TSID#0000, skip", tag);
      break;
    case TableValidateResult::kOk:
      break;
  }
}

}  // namespace
