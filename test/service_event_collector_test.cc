// SPDX-License-Identifier: GPL-2.0-or-later

// mirakc-arib
// Copyright (C) 2019 masnagam
//
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "service_event_collector.hh"

#include "test_helper.hh"

namespace {
const ServiceEventCollectorOption kOption{};
}

TEST(ServiceEventCollectorTest, NoPacket) {
  MockSource src;

  auto collector = std::make_unique<ServiceEventCollector>(kOption);
  auto sink = std::make_unique<MockJsonlSink>();

  EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false));  // EOF
  EXPECT_CALL(*sink, HandleDocument).Times(0);

  collector->Connect(std::move(sink));
  src.Connect(std::move(collector));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(ServiceEventCollectorTest, EmitsEitEvent) {
  TableSource src;
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
      </EIT>
    </tsduck>
  )");

  ServiceEventCollectorOption option(kOption);
  option.sid = 3;

  auto collector = std::make_unique<ServiceEventCollector>(option);
  auto sink = std::make_unique<MockJsonlSink>();

  EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
    EXPECT_STREQ("eit", doc["type"].GetString());
    EXPECT_EQ(3, doc["sid"]);
    EXPECT_EQ(78, doc["tableId"]);
    EXPECT_TRUE(doc.HasMember("observedAt"));
    EXPECT_TRUE(doc.HasMember("data"));
    EXPECT_EQ(3, doc["data"]["serviceId"]);
    EXPECT_EQ(1, doc["data"]["events"].Size());
    EXPECT_EQ(4, doc["data"]["events"][0]["eventId"]);
    return true;
  });

  collector->Connect(std::move(sink));
  src.Connect(std::move(collector));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(ServiceEventCollectorTest, IgnoresOtherSid) {
  TableSource src;
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
      </EIT>
    </tsduck>
  )");

  ServiceEventCollectorOption option(kOption);
  option.sid = 999;  // Mismatch.

  auto collector = std::make_unique<ServiceEventCollector>(option);
  auto sink = std::make_unique<MockJsonlSink>();

  EXPECT_CALL(*sink, HandleDocument).Times(0);

  collector->Connect(std::move(sink));
  src.Connect(std::move(collector));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}
