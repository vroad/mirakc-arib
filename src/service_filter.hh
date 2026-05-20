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

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include <tsduck/tsduck.h>

#include "base.hh"
#include "logging.hh"
#include "packet_sink.hh"
#include "packet_source.hh"
#include "tsduck_helper.hh"

#define MIRAKC_ARIB_SERVICE_FILTER_TRACE(...) MIRAKC_ARIB_TRACE("service-filter: " __VA_ARGS__)
#define MIRAKC_ARIB_SERVICE_FILTER_DEBUG(...) MIRAKC_ARIB_DEBUG("service-filter: " __VA_ARGS__)
#define MIRAKC_ARIB_SERVICE_FILTER_INFO(...) MIRAKC_ARIB_INFO("service-filter: " __VA_ARGS__)
#define MIRAKC_ARIB_SERVICE_FILTER_WARN(...) MIRAKC_ARIB_WARN("service-filter: " __VA_ARGS__)
#define MIRAKC_ARIB_SERVICE_FILTER_ERROR(...) MIRAKC_ARIB_ERROR("service-filter: " __VA_ARGS__)

namespace {

struct ServiceFilterOption final {
  uint16_t sid = 0;
  std::optional<ts::Time> time_limit = std::nullopt;  // JST
};

class ServiceFilterControl {
 public:
  virtual ~ServiceFilterControl() = default;
  virtual uint16_t TargetSid() const = 0;
  virtual std::optional<ts::Time> TimeLimit() const = 0;
  virtual ts::PID CurrentPmtPid() const = 0;
  virtual void ChangePmtPid(ts::PID new_pmt_pid) = 0;
  virtual void UpdateOutputPat(const ts::PAT& pat) = 0;
  virtual void ReplacePsiFilter(std::unordered_set<ts::PID> pids) = 0;
  virtual void ReplaceContentFilter(std::unordered_set<ts::PID> pids) = 0;
  virtual void ReplaceEmmFilter(std::unordered_set<ts::PID> pids) = 0;
  virtual void StopStreaming() = 0;
  virtual void AbortWithError() = 0;
};

class PatHandler final {
 public:
  void Handle(ServiceFilterControl& control, ts::DuckContext& duck, const ts::BinaryTable& table) {
    if (table.sourcePID() != ts::PID_PAT) {
      MIRAKC_ARIB_SERVICE_FILTER_WARN("PAT delivered with PID#{:04X}, skip", table.sourcePID());
      return;
    }
    ts::PAT pat(duck, table);
    if (!pat.isValid()) {
      MIRAKC_ARIB_SERVICE_FILTER_WARN("Broken PAT, skip");
      return;
    }
    if (pat.ts_id == 0) {
      MIRAKC_ARIB_SERVICE_FILTER_WARN("PAT for TSID#0000, skip");
      return;
    }
    if (pat.pmts.find(control.TargetSid()) == pat.pmts.end()) {
      MIRAKC_ARIB_SERVICE_FILTER_ERROR("SID#{:04X} not found in PAT", control.TargetSid());
      control.AbortWithError();
      return;
    }
    auto new_pmt_pid = pat.pmts[control.TargetSid()];
    control.ChangePmtPid(new_pmt_pid);
    for (auto it = pat.pmts.begin(); it != pat.pmts.end();) {
      if (it->first != control.TargetSid()) {
        it = pat.pmts.erase(it);
      } else {
        ++it;
      }
    }
    MIRAKC_ARIB_ASSERT(pat.pmts.size() == 1);
    MIRAKC_ARIB_ASSERT(pat.pmts.find(control.TargetSid()) != pat.pmts.end());
    control.UpdateOutputPat(pat);
    std::unordered_set<ts::PID> psi_filter = {ts::PID_PAT, ts::PID_CAT, ts::PID_NIT, ts::PID_SDT,
                                              ts::PID_EIT, ts::PID_RST, ts::PID_TOT, ts::PID_BIT,
                                              ts::PID_CDT, control.CurrentPmtPid()};
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG(
        "PSI/SI filter += PAT CAT NIT SDT EIT RST TOT BIT CDT PMT#{:04X}", control.CurrentPmtPid());
    control.ReplacePsiFilter(std::move(psi_filter));
  }
};

class CatHandler final {
 public:
  void Handle(ServiceFilterControl&, ts::DuckContext& duck, const ts::BinaryTable& table) {
    ts::CAT cat(duck, table);
    if (!cat.isValid()) {
      MIRAKC_ARIB_SERVICE_FILTER_WARN("Broken CAT, skip");
      return;
    }
    std::unordered_set<ts::PID> emm_filter;
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Clear EMM filter");
    auto i = cat.descs.search(ts::DID_CA);
    while (i < cat.descs.size()) {
      ts::CADescriptor desc(duck, *cat.descs[i]);
      emm_filter.insert(desc.ca_pid);
      MIRAKC_ARIB_SERVICE_FILTER_DEBUG("EMM filter += EMM#{:04X}", desc.ca_pid);
      i = cat.descs.search(ts::DID_CA, i + 1);
    }
    control.ReplaceEmmFilter(std::move(emm_filter));
  }
};

class PmtHandler final {
 public:
  void Handle(ServiceFilterControl& control, ts::DuckContext& duck, const ts::BinaryTable& table) {
    ts::PMT pmt(duck, table);
    if (!pmt.isValid()) {
      MIRAKC_ARIB_SERVICE_FILTER_WARN("Broken PMT, skip");
      return;
    }
    if (pmt.service_id != control.TargetSid()) {
      MIRAKC_ARIB_SERVICE_FILTER_WARN("PMT.SID#{} unmatched, skip", pmt.service_id);
      return;
    }
    std::unordered_set<ts::PID> content_filter;
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Clear content filter");
    content_filter.insert(pmt.pcr_pid);
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Content filter += PCR#{:04X}", pmt.pcr_pid);
    auto i = pmt.descs.search(ts::DID_CA);
    while (i < pmt.descs.size()) {
      ts::CADescriptor desc(duck, *pmt.descs[i]);
      content_filter.insert(desc.ca_pid);
      MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Content filter += ECM#{:04X}", desc.ca_pid);
      i = pmt.descs.search(ts::DID_CA, i + 1);
    }
    for (auto it = pmt.streams.begin(); it != pmt.streams.end(); ++it) {
      ts::PID pid = it->first;
      content_filter.insert(pid);
      const auto& stream = it->second;
      if (stream.isVideo()) {
        MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Content filter += PES/Video#{:04X}", pid);
      } else if (stream.isAudio()) {
        MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Content filter += PES/Audio#{:04X}", pid);
      } else if (stream.isSubtitles()) {
        MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Content filter += PES/Subtitle#{:04X}", pid);
      } else if (IsAribSubtitle(stream)) {
        MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Content filter += PES/ARIB-Subtitle#{:04X}", pid);
      } else if (IsAribSuperimposedText(stream)) {
        MIRAKC_ARIB_SERVICE_FILTER_DEBUG(
            "Content filter += PES/ARIB-SuperimposedText#{:04X}", pid);
      } else {
        MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Content filter += Other#{:04X}", pid);
      }
    }
    control.ReplaceContentFilter(std::move(content_filter));
  }
};

class TotHandler final {
 public:
  void Handle(ServiceFilterControl& control, ts::DuckContext& duck, const ts::BinaryTable& table) {
    ts::TOT tot(duck, table);
    if (!tot.isValid()) {
      MIRAKC_ARIB_SERVICE_FILTER_WARN("Broken TOT, skip");
      return;
    }
    auto time_limit = control.TimeLimit();
    if (time_limit.has_value() && tot.utc_time >= time_limit.value()) {
      control.StopStreaming();
      MIRAKC_ARIB_SERVICE_FILTER_INFO("Over the time limit, stop streaming");
    }
  }
};

class ServiceFilter final : public PacketSink,
                            public ts::TableHandlerInterface,
                            public ServiceFilterControl {
 public:
  explicit ServiceFilter(const ServiceFilterOption& option)
      : option_(option),
        demux_(context_),
        pat_packetizer_(ts::PID_PAT, ts::CyclingPacketizer::ALWAYS) {
    demux_.setTableHandler(this);
    demux_.addPID(ts::PID_PAT);
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Demux PAT");
    demux_.addPID(ts::PID_CAT);
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Demux CAT for detecting EMM PIDs");
    if (option_.time_limit.has_value()) {
      demux_.addPID(ts::PID_TOT);
      MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Demux TOT for checking the time limit");
    }
  }

  virtual ~ServiceFilter() override {}

  void Connect(std::unique_ptr<PacketSink>&& sink) {
    sink_ = std::move(sink);
  }

  bool Start() override {
    MIRAKC_ARIB_ASSERT(sink_ != nullptr);
    return sink_->Start();
  }

  void End() override {
    MIRAKC_ARIB_ASSERT(sink_ != nullptr);
    sink_->End();
  }

  int GetExitCode() const override {
    MIRAKC_ARIB_ASSERT(sink_ != nullptr);
    auto exit_code = sink_->GetExitCode();
    if (exit_code == EXIT_SUCCESS) {
      if (error_) {
        exit_code = EXIT_FAILURE;
      }
    }
    return exit_code;
  }

  bool HandlePacket(const ts::TSPacket& packet) override {
    if (!sink_) {
      MIRAKC_ARIB_SERVICE_FILTER_ERROR("No sink connected");
      return false;
    }

    demux_.feedPacket(packet);

    if (done_) {
      return false;
    }

    auto pid = packet.getPID();

    if (CheckFilterForDrop(pid)) {
      return true;
    }

    if (pid == ts::PID_PAT) {
      // Feed a modified PAT packet
      ts::TSPacket pat_packet;
      pat_packetizer_.getNextPacket(pat_packet);
      MIRAKC_ARIB_ASSERT(pat_packet.getPID() == ts::PID_PAT);
      return sink_->HandlePacket(pat_packet);
    }

    MIRAKC_ARIB_ASSERT(pid != ts::PID_NULL);
    return sink_->HandlePacket(packet);
  }

 private:
  bool CheckFilterForDrop(ts::PID pid) const {
    if (content_filter_.find(pid) != content_filter_.end()) {
      return false;
    }
    if (psi_filter_.find(pid) != psi_filter_.end()) {
      return false;
    }
    if (emm_filter_.find(pid) != emm_filter_.end()) {
      return false;
    }
    return true;
  }

  void handleTable(ts::SectionDemux&, const ts::BinaryTable& table) override {
    switch (table.tableId()) {
      case ts::TID_PAT:
        pat_handler_.Handle(*this, context_, table);
        break;
      case ts::TID_CAT:
        cat_handler_.Handle(*this, context_, table);
        break;
      case ts::TID_PMT:
        pmt_handler_.Handle(*this, context_, table);
        break;
      case ts::TID_TOT:
        tot_handler_.Handle(*this, context_, table);
        break;
      default:
        break;
    }
  }

  uint16_t TargetSid() const override { return option_.sid; }
  std::optional<ts::Time> TimeLimit() const override { return option_.time_limit; }
  ts::PID CurrentPmtPid() const override { return pmt_pid_; }
  void ChangePmtPid(ts::PID new_pmt_pid) override {
    psi_filter_.clear();
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Clear PSI/SI filter");
    if (pmt_pid_ != ts::PID_NULL) {
      MIRAKC_ARIB_SERVICE_FILTER_INFO(
          "PID of PMT has been changed: {:04X} -> {:04X}", pmt_pid_, new_pmt_pid);
      demux_.removePID(pmt_pid_);
      MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Stop to demux PMT#{:04X}", pmt_pid_);
      pmt_pid_ = ts::PID_NULL;
    }
    pmt_pid_ = new_pmt_pid;
    demux_.addPID(pmt_pid_);
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Demux PMT#{:04X}", pmt_pid_);
  }
  void UpdateOutputPat(const ts::PAT& pat) override {
    pat_packetizer_.removeAll();
    pat_packetizer_.addTable(context_, pat);
  }
  void ReplacePsiFilter(std::unordered_set<ts::PID> pids) override { psi_filter_ = std::move(pids); }
  void ReplaceContentFilter(std::unordered_set<ts::PID> pids) override {
    content_filter_ = std::move(pids);
  }
  void ReplaceEmmFilter(std::unordered_set<ts::PID> pids) override { emm_filter_ = std::move(pids); }
  void StopStreaming() override { done_ = true; }
  void AbortWithError() override {
    done_ = true;
    error_ = true;
  }

  const ServiceFilterOption option_;
  ts::DuckContext context_;
  ts::SectionDemux demux_;
  ts::CyclingPacketizer pat_packetizer_;
  std::unique_ptr<PacketSink> sink_;
  std::unordered_set<ts::PID> psi_filter_;
  std::unordered_set<ts::PID> content_filter_;
  std::unordered_set<ts::PID> emm_filter_;
  ts::PID pmt_pid_ = ts::PID_NULL;
  bool done_ = false;
  bool error_ = false;
  PatHandler pat_handler_;
  CatHandler cat_handler_;
  PmtHandler pmt_handler_;
  TotHandler tot_handler_;

  MIRAKC_ARIB_NON_COPYABLE(ServiceFilter);
};

}  // namespace
