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

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <cppcodec/base64_rfc4648.hpp>
#include <rapidjson/document.h>
#include <tsduck/tsduck.h>

#include "base.hh"
#include "jsonl_source.hh"
#include "logging.hh"
#include "packet_sink.hh"
#include "tsduck_helper.hh"

namespace {

struct ServiceEventCollectorOption final {
  uint16_t sid = 0;
};

class ServiceEventCollector final : public PacketSink,
                                    public JsonlSource,
                                    public ts::SectionHandlerInterface,
                                    public ts::TableHandlerInterface {
 public:
  explicit ServiceEventCollector(const ServiceEventCollectorOption& option)
      : option_(option), demux_(context_) {
    demux_.setTableHandler(this);
    demux_.setSectionHandler(this);
    demux_.addPID(ts::PID_PAT);
    demux_.addPID(ts::PID_EIT);
    demux_.addPID(ts::PID_TOT);
  }

  ~ServiceEventCollector() override = default;

  bool HandlePacket(const ts::TSPacket& packet) override {
    auto pid = packet.getPID();

    if (packet.hasPCR() && packet.getPCR() != ts::INVALID_PCR && pid == pcr_pid_) {
      auto pcr = static_cast<int64_t>(packet.getPCR());
      if (IsValidPcr(pcr)) {
        clock_.UpdatePcr(pcr);
        last_pcr_ = pcr;
        has_last_pcr_ = true;
      }
    }

    const auto kind_it = caption_pids_.find(pid);
    if (kind_it != caption_pids_.end()) {
      HandleCaptionPacket(packet, pid, kind_it->second);
    }

    demux_.feedPacket(packet);
    return true;
  }

  void End() override {
    for (auto& pair : pes_buffers_) {
      FlushPesBuffer(pair.first);
    }
  }

 private:
  enum class CaptionKind { kCaption, kSuperimposedText };

  struct PesBuffer {
    std::vector<uint8_t> data;
    CaptionKind kind = CaptionKind::kCaption;
  };

  static int64_t NowMillis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  }

  void handleTable(ts::SectionDemux&, const ts::BinaryTable& table) override {
    switch (table.tableId()) {
      case ts::TID_PAT:
        HandlePat(table);
        break;
      case ts::TID_PMT:
        HandlePmt(table);
        break;
      case ts::TID_TOT:
        HandleTot(table);
        break;
      default:
        break;
    }
  }

  void handleSection(ts::SectionDemux&, const ts::Section& section) override {
    if (!section.isValid()) {
      return;
    }
    const auto tid = section.tableId();
    if (tid < ts::TID_EIT_PF_ACT || tid > ts::TID_EIT_S_OTH_MAX) {
      return;
    }
    if (section.isNext()) {
      return;
    }
    if (section.payloadSize() < EitSection::EIT_PAYLOAD_FIXED_SIZE) {
      return;
    }
    EitSection eit(section);
    if (eit.sid != option_.sid) {
      return;
    }
    WriteEitEvent(eit);
  }

  void HandlePat(const ts::BinaryTable& table) {
    ts::PAT pat(context_, table);
    if (!pat.isValid()) {
      return;
    }
    auto it = pat.pmts.find(option_.sid);
    if (it == pat.pmts.end()) {
      return;
    }
    auto new_pmt_pid = it->second;
    if (pmt_pid_ != ts::PID_NULL && pmt_pid_ != new_pmt_pid) {
      demux_.removePID(pmt_pid_);
    }
    pmt_pid_ = new_pmt_pid;
    demux_.addPID(pmt_pid_);
  }

  void HandlePmt(const ts::BinaryTable& table) {
    ts::PMT pmt(context_, table);
    if (!pmt.isValid() || pmt.service_id != option_.sid) {
      return;
    }

    caption_pids_.clear();
    for (const auto& pair : pmt.streams) {
      ts::PID pid = pair.first;
      const auto& stream = pair.second;
      if (IsAribSubtitle(stream)) {
        caption_pids_[pid] = CaptionKind::kCaption;
      } else if (IsAribSuperimposedText(stream)) {
        caption_pids_[pid] = CaptionKind::kSuperimposedText;
      }
    }

    if (pmt.pcr_pid != ts::PID_NULL && pmt.pcr_pid != pcr_pid_) {
      pcr_pid_ = pmt.pcr_pid;
      clock_.SetPid(pcr_pid_);
      has_last_pcr_ = false;
    }
  }

  void HandleTot(const ts::BinaryTable& table) {
    ts::TOT tot(context_, table);
    if (!tot.isValid()) {
      return;
    }
    clock_.UpdateTime(tot.utc_time);  // JST in ARIB

    if (has_last_pcr_ && clock_.IsReady()) {
      WriteClockEvent(last_pcr_);
    }
  }

  void HandleCaptionPacket(const ts::TSPacket& packet, ts::PID pid, CaptionKind kind) {
    const uint8_t* payload = packet.getPayload();
    auto payload_size = packet.getPayloadSize();
    if (payload == nullptr || payload_size == 0) {
      return;
    }

    if (packet.getPUSI()) {
      FlushPesBuffer(pid);
      auto& buf = pes_buffers_[pid];
      buf.kind = kind;
      buf.data.assign(payload, payload + payload_size);
    } else {
      auto it = pes_buffers_.find(pid);
      if (it == pes_buffers_.end()) {
        return;  // no PES start seen yet, ignore
      }
      it->second.data.insert(it->second.data.end(), payload, payload + payload_size);
    }
  }

  void FlushPesBuffer(ts::PID pid) {
    auto it = pes_buffers_.find(pid);
    if (it == pes_buffers_.end()) {
      return;
    }
    const auto& buf = it->second;
    EmitPesEvent(pid, buf);
    pes_buffers_.erase(it);
  }

  void EmitPesEvent(ts::PID pid, const PesBuffer& buf) {
    const auto& data = buf.data;
    if (data.size() < 9) {
      return;
    }
    if (!(data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01)) {
      return;
    }
    uint8_t pts_dts_flags = (data[7] >> 6) & 0x03;
    uint8_t header_data_length = data[8];
    size_t header_end = 9 + static_cast<size_t>(header_data_length);
    if (header_end > data.size()) {
      return;
    }

    std::optional<int64_t> pts;
    std::optional<int64_t> dts;
    if ((pts_dts_flags & 0x02) && 9 + 5 <= data.size()) {
      pts = ParsePtsDts(&data[9]);
    }
    if (pts_dts_flags == 0x03 && 9 + 10 <= data.size()) {
      dts = ParsePtsDts(&data[14]);
    }

    const uint8_t* payload_ptr = data.data() + header_end;
    size_t payload_size = data.size() - header_end;
    std::string payload_b64 =
        cppcodec::base64_rfc4648::encode(payload_ptr, payload_size);

    rapidjson::Document doc(rapidjson::kObjectType);
    auto& alloc = doc.GetAllocator();
    const char* type_str = (buf.kind == CaptionKind::kCaption)
        ? "caption_pes"
        : "superimposed_text_pes";
    doc.AddMember("type", rapidjson::Value(type_str, alloc), alloc);
    doc.AddMember("sid", option_.sid, alloc);
    doc.AddMember("pid", static_cast<uint32_t>(pid), alloc);
    if (pts.has_value()) {
      doc.AddMember("pts", pts.value(), alloc);
    } else {
      doc.AddMember("pts", rapidjson::Value(rapidjson::kNullType), alloc);
    }
    if (dts.has_value()) {
      doc.AddMember("dts", dts.value(), alloc);
    } else {
      doc.AddMember("dts", rapidjson::Value(rapidjson::kNullType), alloc);
    }
    doc.AddMember("observedAt", NowMillis(), alloc);
    doc.AddMember(
        "payloadBase64",
        rapidjson::Value(payload_b64.c_str(), static_cast<rapidjson::SizeType>(payload_b64.size()),
            alloc),
        alloc);
    FeedDocument(doc);
  }

  static int64_t ParsePtsDts(const uint8_t* p) {
    int64_t v = 0;
    v |= static_cast<int64_t>((p[0] >> 1) & 0x07) << 30;
    v |= static_cast<int64_t>(p[1]) << 22;
    v |= static_cast<int64_t>((p[2] >> 1) & 0x7F) << 15;
    v |= static_cast<int64_t>(p[3]) << 7;
    v |= static_cast<int64_t>((p[4] >> 1) & 0x7F);
    return v;
  }

  void WriteEitEvent(const EitSection& eit) {
    auto inner = MakeJsonValue(eit);

    rapidjson::Document doc(rapidjson::kObjectType);
    auto& alloc = doc.GetAllocator();
    doc.AddMember("type", "eit", alloc);
    doc.AddMember("sid", option_.sid, alloc);
    doc.AddMember("tableId", static_cast<uint32_t>(eit.tid), alloc);
    doc.AddMember("observedAt", NowMillis(), alloc);
    rapidjson::Value data_val;
    data_val.CopyFrom(inner, alloc);
    doc.AddMember("data", data_val, alloc);
    FeedDocument(doc);
  }

  void WriteClockEvent(int64_t pcr) {
    rapidjson::Document doc(rapidjson::kObjectType);
    auto& alloc = doc.GetAllocator();
    doc.AddMember("type", "clock", alloc);
    doc.AddMember("sid", option_.sid, alloc);
    doc.AddMember("pid", static_cast<uint32_t>(pcr_pid_), alloc);
    doc.AddMember("pcr", pcr, alloc);
    if (clock_.IsReady()) {
      auto jst = clock_.PcrToTime(pcr);
      auto unix_ms = ConvertJstTimeToUnixTime(jst);
      doc.AddMember("time", static_cast<int64_t>(unix_ms), alloc);
    } else {
      doc.AddMember("time", rapidjson::Value(rapidjson::kNullType), alloc);
    }
    doc.AddMember("observedAt", NowMillis(), alloc);
    FeedDocument(doc);
  }

  const ServiceEventCollectorOption option_;
  ts::DuckContext context_;
  ts::SectionDemux demux_;
  ts::PID pmt_pid_ = ts::PID_NULL;
  ts::PID pcr_pid_ = ts::PID_NULL;
  std::map<ts::PID, CaptionKind> caption_pids_;
  std::map<ts::PID, PesBuffer> pes_buffers_;
  Clock clock_;
  int64_t last_pcr_ = 0;
  bool has_last_pcr_ = false;

  MIRAKC_ARIB_NON_COPYABLE(ServiceEventCollector);
};

}  // namespace
