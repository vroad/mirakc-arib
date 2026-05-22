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

#include <cstdlib>
#include <memory>
#include <unordered_set>

#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "service_filter.hh"

#include "test_helper.hh"

namespace {

const ServiceFilterOption kOption{0x0001};

class MockServiceFilterTableHandler : public ServiceFilterTableHandler {
 public:
  MOCK_METHOD(void, Handle, (const ts::BinaryTable&), (override));
};

}  // namespace

TEST(ServiceFilterUnitTest, NoPacket) {
  MockSource src;
  auto mock_handler = std::make_unique<MockServiceFilterTableHandler>();
  auto* handler_ptr = mock_handler.get();
  auto filter = std::make_unique<ServiceFilter>(kOption, std::move(mock_handler));
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false));  // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*handler_ptr, Handle).Times(0);
  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(ServiceFilterUnitTest, NullPacket) {
  MockSource src;
  auto mock_handler = std::make_unique<MockServiceFilterTableHandler>();
  auto* handler_ptr = mock_handler.get();
  auto filter = std::make_unique<ServiceFilter>(kOption, std::move(mock_handler));
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(src, GetNextPacket).WillOnce([](ts::TSPacket* packet) {
      *packet = ts::NullPacket;
      return true;
    });
    EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false));  // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*handler_ptr, Handle).Times(0);
  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(ServiceFilterUnitTest, HandlePacketDropsUnknownPidBeforeAnyFilter) {
  MockSource src;
  auto mock_handler = std::make_unique<MockServiceFilterTableHandler>();
  auto* handler_ptr = mock_handler.get();
  auto filter = std::make_unique<ServiceFilter>(kOption, std::move(mock_handler));
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(src, GetNextPacket).WillOnce([](ts::TSPacket* packet) {
      *packet = ts::NullPacket;
      packet->setPID(0x0301);
      return true;
    });
    EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false));  // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*handler_ptr, Handle).Times(0);
  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(ServiceFilterUnitTest, DemuxesPatAndCallsHandle) {
  TableSource src;
  auto mock_handler = std::make_unique<MockServiceFilterTableHandler>();
  auto* handler_ptr = mock_handler.get();
  auto filter = std::make_unique<ServiceFilter>(kOption, std::move(mock_handler));
  auto sink = std::make_unique<MockSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*handler_ptr, Handle).WillOnce([](const ts::BinaryTable& table) {
      EXPECT_EQ(ts::TID_PAT, table.tableId());
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceFilterUnitTest, DemuxesCat) {
  TableSource src;
  auto mock_handler = std::make_unique<MockServiceFilterTableHandler>();
  auto* handler_ptr = mock_handler.get();
  auto filter = std::make_unique<ServiceFilter>(kOption, std::move(mock_handler));
  auto sink = std::make_unique<MockSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <CAT version="1" current="true" test-pid="0x0001">
        <CA_descriptor CA_system_id="0x0005" CA_PID="0x0111" />
      </CAT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*handler_ptr, Handle).WillOnce([](const ts::BinaryTable& table) {
      EXPECT_EQ(ts::TID_CAT, table.tableId());
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceFilterUnitTest, TotNotDemuxedWithoutTimeLimit) {
  TableSource src;
  auto mock_handler = std::make_unique<MockServiceFilterTableHandler>();
  auto* handler_ptr = mock_handler.get();
  auto filter = std::make_unique<ServiceFilter>(kOption, std::move(mock_handler));
  auto sink = std::make_unique<MockSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*handler_ptr, Handle).Times(0);
  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceFilterUnitTest, TotDemuxedWithTimeLimit) {
  auto option = kOption;
  option.time_limit = ts::Time(2019, 1, 2, 3, 4, 5);

  TableSource src;
  auto mock_handler = std::make_unique<MockServiceFilterTableHandler>();
  auto* handler_ptr = mock_handler.get();
  auto filter = std::make_unique<ServiceFilter>(option, std::move(mock_handler));
  auto sink = std::make_unique<MockSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*handler_ptr, Handle).WillOnce([](const ts::BinaryTable& table) {
      EXPECT_EQ(ts::TID_TOT, table.tableId());
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceFilterUnitTest, ForwardsPatAfterPsiFilterIncludesPatPid) {
  TableSource src;
  auto mock_handler = std::make_unique<MockServiceFilterTableHandler>();
  auto* handler_ptr = mock_handler.get();
  auto filter = std::make_unique<ServiceFilter>(kOption, std::move(mock_handler));
  ServiceFilterControl& control = *filter;
  auto sink = std::make_unique<MockSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*handler_ptr, Handle).WillOnce([&control](const ts::BinaryTable& table) {
      ts::DuckContext ctx;
      ts::PAT pat(ctx, table);
      control.SetOutputPat(pat);
      control.SetPsiFilter({ts::PID_PAT});
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      TableValidator<ts::PAT> validator(ts::PID_PAT);
      EXPECT_CALL(validator, Validate).WillOnce([](const ts::PAT& pat) {
        EXPECT_TRUE(pat.isValid());
        ASSERT_EQ(1U, pat.pmts.size());
        EXPECT_EQ(0x0101, pat.pmts.at(0x0001));
      });
      validator.FeedPacket(packet);
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceFilterUnitTest, ForwardsContentPidsListedInContentFilter) {
  TableSource src;
  auto mock_handler = std::make_unique<MockServiceFilterTableHandler>();
  auto* handler_ptr = mock_handler.get();
  auto filter = std::make_unique<ServiceFilter>(kOption, std::move(mock_handler));
  ServiceFilterControl& control = *filter;
  auto sink = std::make_unique<MockSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <generic_short_table table_id="0xFF" test-pid="0x0301" />
      <generic_short_table table_id="0xFF" test-pid="0x0302" />
      <generic_short_table table_id="0xFF" test-pid="0x0303" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*handler_ptr, Handle).WillOnce([&control](const ts::BinaryTable&) {
      control.SetContentFilter({0x0301, 0x0302});
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0301, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0302, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceFilterUnitTest, ForwardsEmmPidsListedInEmmFilter) {
  TableSource src;
  auto mock_handler = std::make_unique<MockServiceFilterTableHandler>();
  auto* handler_ptr = mock_handler.get();
  auto filter = std::make_unique<ServiceFilter>(kOption, std::move(mock_handler));
  ServiceFilterControl& control = *filter;
  auto sink = std::make_unique<MockSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <CAT version="1" current="true" test-pid="0x0001">
        <CA_descriptor CA_system_id="0x0005" CA_PID="0x0111" />
      </CAT>
      <generic_short_table table_id="0xFF" test-pid="0x0111" />
      <generic_short_table table_id="0xFF" test-pid="0x0112" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*handler_ptr, Handle).WillOnce([&control](const ts::BinaryTable&) {
      control.SetEmmFilter({0x0111});
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0111, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceFilterUnitTest, SetDoneStopsForwardingAndReturnsFalse) {
  TableSource src;
  auto mock_handler = std::make_unique<MockServiceFilterTableHandler>();
  auto* handler_ptr = mock_handler.get();
  auto filter = std::make_unique<ServiceFilter>(kOption, std::move(mock_handler));
  ServiceFilterControl& control = *filter;
  auto sink = std::make_unique<MockSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <generic_short_table table_id="0xFF" test-pid="0x0301" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*handler_ptr, Handle).WillOnce([&control](const ts::BinaryTable&) {
      control.SetDone();
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_EQ(1U, src.GetNumberOfRemainingPackets());
}

TEST(ServiceFilterUnitTest, SetErrorPropagatesToExitCode) {
  TableSource src;
  auto mock_handler = std::make_unique<MockServiceFilterTableHandler>();
  auto* handler_ptr = mock_handler.get();
  auto filter = std::make_unique<ServiceFilter>(kOption, std::move(mock_handler));
  ServiceFilterControl& control = *filter;
  auto sink = std::make_unique<MockSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*handler_ptr, Handle).WillOnce([&control](const ts::BinaryTable&) {
      control.SetError();
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_FAILURE, src.FeedPackets());
}

TEST(ServiceFilterUnitTest, SetPmtPidAddsPidToDemux) {
  TableSource src;
  auto mock_handler = std::make_unique<MockServiceFilterTableHandler>();
  auto* handler_ptr = mock_handler.get();
  auto filter = std::make_unique<ServiceFilter>(kOption, std::move(mock_handler));
  ServiceFilterControl& control = *filter;
  auto sink = std::make_unique<MockSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
      </PMT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*handler_ptr, Handle).WillOnce([&control](const ts::BinaryTable& table) {
      EXPECT_EQ(ts::TID_PAT, table.tableId());
      control.SetPmtPid(0x0101);
    });
    EXPECT_CALL(*handler_ptr, Handle).WillOnce([](const ts::BinaryTable& table) {
      EXPECT_EQ(ts::TID_PMT, table.tableId());
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}
