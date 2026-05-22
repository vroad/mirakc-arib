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

#include <optional>
#include <unordered_set>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "service_filter.hh"

#include "test_helper.hh"

namespace {

class MockServiceFilterControl : public ServiceFilterControl {
 public:
  MOCK_METHOD(uint16_t, TargetSid, (), (const, override));
  MOCK_METHOD(std::optional<ts::Time>, TimeLimit, (), (const, override));

  MOCK_METHOD(void, SetPmtPid, (ts::PID), (override));
  MOCK_METHOD(void, SetOutputPat, (const ts::PAT&), (override));

  MOCK_METHOD(void, SetPsiFilter, (std::unordered_set<ts::PID>), (override));
  MOCK_METHOD(void, SetContentFilter, (std::unordered_set<ts::PID>), (override));
  MOCK_METHOD(void, SetEmmFilter, (std::unordered_set<ts::PID>), (override));

  MOCK_METHOD(void, SetDone, (), (override));
  MOCK_METHOD(void, SetError, (), (override));
};

TEST(DefaultServiceFilterTableHandlerTest, PatUpdatesPmtPidOutputPatAndPsiFilter) {
  testing::StrictMock<MockServiceFilterControl> control;
  ts::DuckContext context;
  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(control, TargetSid()).WillOnce(testing::Return(0x0001));
    EXPECT_CALL(control, SetPsiFilter(testing::_))
        .WillOnce([](const std::unordered_set<ts::PID>& pids) { EXPECT_TRUE(pids.empty()); });
    EXPECT_CALL(control, SetPmtPid(0x0101));
    EXPECT_CALL(control, SetOutputPat(testing::_)).WillOnce([](const ts::PAT& pat) {
      EXPECT_TRUE(pat.isValid());
      ASSERT_EQ(1U, pat.pmts.size());
      EXPECT_EQ(0x0101, pat.pmts.at(0x0001));
    });
    EXPECT_CALL(control, SetPsiFilter(testing::_))
        .WillOnce([](const std::unordered_set<ts::PID>& pids) {
          EXPECT_THAT(pids,
              testing::UnorderedElementsAre(ts::PID_PAT, ts::PID_CAT, ts::PID_NIT, ts::PID_SDT,
                  ts::PID_EIT, ts::PID_RST, ts::PID_TOT, ts::PID_BIT, ts::PID_CDT, 0x0101));
        });
  }

  DefaultServiceFilterTableHandler handler(control, context);
  handler.Handle(table);
}

TEST(DefaultServiceFilterTableHandlerTest, PatAbortsWhenTargetSidIsMissing) {
  testing::StrictMock<MockServiceFilterControl> control;
  ts::DuckContext context;
  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(control, TargetSid()).WillOnce(testing::Return(0x0001));
    EXPECT_CALL(control, SetError());
  }

  DefaultServiceFilterTableHandler handler(control, context);
  handler.Handle(table);
}

TEST(DefaultServiceFilterTableHandlerTest, PatSkipsWhenDeliveredWithWrongPid) {
  testing::StrictMock<MockServiceFilterControl> control;
  ts::DuckContext context;
  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0001">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
    </tsduck>
  )");

  DefaultServiceFilterTableHandler handler(control, context);
  handler.Handle(table);
}

TEST(DefaultServiceFilterTableHandlerTest, PatSkipsWhenTsIdIsZero) {
  testing::StrictMock<MockServiceFilterControl> control;
  ts::DuckContext context;
  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x0000"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
    </tsduck>
  )");

  DefaultServiceFilterTableHandler handler(control, context);
  handler.Handle(table);
}

TEST(DefaultServiceFilterTableHandlerTest, CatReplacesEmmFilterWithCaPids) {
  testing::StrictMock<MockServiceFilterControl> control;
  ts::DuckContext context;
  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <CAT version="1" current="true" test-pid="0x0001">
        <CA_descriptor CA_system_id="0x0005" CA_PID="0x0111" />
        <CA_descriptor CA_system_id="0x0006" CA_PID="0x0112" />
      </CAT>
    </tsduck>
  )");

  EXPECT_CALL(control, SetEmmFilter(testing::_))
      .WillOnce([](const std::unordered_set<ts::PID>& pids) {
        EXPECT_THAT(pids, testing::UnorderedElementsAre(0x0111, 0x0112));
      });

  DefaultServiceFilterTableHandler handler(control, context);
  handler.Handle(table);
}

TEST(DefaultServiceFilterTableHandlerTest, CatReplacesEmmFilterWithEmptySetWhenNoCaDescriptors) {
  testing::StrictMock<MockServiceFilterControl> control;
  ts::DuckContext context;
  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <CAT version="1" current="true" test-pid="0x0001">
      </CAT>
    </tsduck>
  )");

  EXPECT_CALL(control, SetEmmFilter(testing::_))
      .WillOnce([](const std::unordered_set<ts::PID>& pids) { EXPECT_TRUE(pids.empty()); });

  DefaultServiceFilterTableHandler handler(control, context);
  handler.Handle(table);
}

TEST(DefaultServiceFilterTableHandlerTest, PmtReplacesContentFilterWithPcrEcmAndPesPids) {
  testing::StrictMock<MockServiceFilterControl> control;
  ts::DuckContext context;
  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <CA_descriptor CA_system_id="0x0005" CA_PID="0x0251" />
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(control, TargetSid()).WillOnce(testing::Return(0x0001));
    EXPECT_CALL(control, SetContentFilter(testing::_))
        .WillOnce([](const std::unordered_set<ts::PID>& pids) {
          EXPECT_THAT(pids, testing::UnorderedElementsAre(0x0901, 0x0251, 0x0301, 0x0302));
        });
  }

  DefaultServiceFilterTableHandler handler(control, context);
  handler.Handle(table);
}

TEST(DefaultServiceFilterTableHandlerTest, PmtIgnoresUnmatchedSid) {
  testing::StrictMock<MockServiceFilterControl> control;
  ts::DuckContext context;
  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PMT version="1" current="true" service_id="0x0002" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
      </PMT>
    </tsduck>
  )");

  EXPECT_CALL(control, TargetSid()).WillOnce(testing::Return(0x0001));

  DefaultServiceFilterTableHandler handler(control, context);
  handler.Handle(table);
}

TEST(DefaultServiceFilterTableHandlerTest,
    PmtReplacesContentFilterWithPcrAndPesOnlyWhenNoCaDescriptor) {
  testing::StrictMock<MockServiceFilterControl> control;
  ts::DuckContext context;
  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(control, TargetSid()).WillOnce(testing::Return(0x0001));
    EXPECT_CALL(control, SetContentFilter(testing::_))
        .WillOnce([](const std::unordered_set<ts::PID>& pids) {
          EXPECT_THAT(pids, testing::UnorderedElementsAre(0x0901, 0x0301, 0x0302));
        });
  }

  DefaultServiceFilterTableHandler handler(control, context);
  handler.Handle(table);
}

TEST(DefaultServiceFilterTableHandlerTest, TotDoesNotStopWithoutTimeLimit) {
  testing::StrictMock<MockServiceFilterControl> control;
  ts::DuckContext context;
  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" />
    </tsduck>
  )");

  EXPECT_CALL(control, TimeLimit()).WillOnce(testing::Return(std::optional<ts::Time>{}));

  DefaultServiceFilterTableHandler handler(control, context);
  handler.Handle(table);
}

TEST(DefaultServiceFilterTableHandlerTest, TotDoesNotStopBeforeTimeLimit) {
  testing::StrictMock<MockServiceFilterControl> control;
  ts::DuckContext context;
  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <TOT UTC_time="2019-01-02 03:04:04" test-pid="0x0014" />
    </tsduck>
  )");

  EXPECT_CALL(control, TimeLimit())
      .WillOnce(testing::Return(std::make_optional(ts::Time(2019, 1, 2, 3, 4, 5))));

  DefaultServiceFilterTableHandler handler(control, context);
  handler.Handle(table);
}

TEST(DefaultServiceFilterTableHandlerTest, TotStopsAtTimeLimit) {
  testing::StrictMock<MockServiceFilterControl> control;
  ts::DuckContext context;
  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(control, TimeLimit())
        .WillOnce(testing::Return(std::make_optional(ts::Time(2019, 1, 2, 3, 4, 5))));
    EXPECT_CALL(control, SetDone());
  }

  DefaultServiceFilterTableHandler handler(control, context);
  handler.Handle(table);
}

TEST(DefaultServiceFilterTableHandlerTest, TotStopsAfterTimeLimit) {
  testing::StrictMock<MockServiceFilterControl> control;
  ts::DuckContext context;
  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <TOT UTC_time="2019-01-02 03:04:06" test-pid="0x0014" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(control, TimeLimit())
        .WillOnce(testing::Return(std::make_optional(ts::Time(2019, 1, 2, 3, 4, 5))));
    EXPECT_CALL(control, SetDone());
  }

  DefaultServiceFilterTableHandler handler(control, context);
  handler.Handle(table);
}

TEST(DefaultServiceFilterTableHandlerTest, SkipsUnknownTableId) {
  testing::StrictMock<MockServiceFilterControl> control;
  ts::DuckContext context;
  ts::BinaryTable table;

  DefaultServiceFilterTableHandler handler(control, context);
  handler.Handle(table);
}

}  // namespace
