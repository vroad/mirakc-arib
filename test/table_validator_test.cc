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

#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "table_validator.hh"
#include "test_helper.hh"

ts::BinaryTable MakeIncompleteLongSectionTable(ts::TID table_id, ts::PID source_pid) {
  const uint8_t payload[] = {0x00};
  const ts::SectionPtr section(new ts::Section(
      table_id, false, 0x0001, 1, true, 0, 1, payload, sizeof(payload), source_pid));

  ts::BinaryTable table;
  table.addSection(section);
  return table;
}

TEST(TableValidatorTest, ValidPat) {
  ts::DuckContext context;

  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234" test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101"/>
      </PAT>
    </tsduck>
  )");
  ts::PAT pat(context, table);

  EXPECT_EQ(TableValidateResult::kOk, ValidatePat(pat, table));
}

TEST(TableValidatorTest, PatWithWrongPid) {
  ts::DuckContext context;

  // PAT must be carried on PID_PAT (0x0000); this table uses the wrong PID (0x0100).
  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234" test-pid="0x0100">
        <service service_id="0x0001" program_map_PID="0x0101"/>
      </PAT>
    </tsduck>
  )");
  ts::PAT pat(context, table);

  EXPECT_EQ(TableValidateResult::kWrongPatPid, ValidatePat(pat, table));
}

TEST(TableValidatorTest, PatWithZeroTsId) {
  ts::DuckContext context;

  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x0000" test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101"/>
      </PAT>
    </tsduck>
  )");
  ts::PAT pat(context, table);

  EXPECT_EQ(TableValidateResult::kZeroPatTsId, ValidatePat(pat, table));
}

TEST(TableValidatorTest, ValidCat) {
  ts::DuckContext context;

  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <CAT version="1" current="true" test-pid="0x0001">
        <CA_descriptor CA_system_id="0x0005" CA_PID="0x0901"/>
      </CAT>
    </tsduck>
  )");
  ts::CAT cat(context, table);

  EXPECT_EQ(TableValidateResult::kOk, ValidateCat(cat));
}

TEST(TableValidatorTest, ValidCatWithoutCaDescriptors) {
  ts::DuckContext context;

  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <CAT version="1" current="true" test-pid="0x0001"/>
    </tsduck>
  )");
  ts::CAT cat(context, table);

  EXPECT_EQ(TableValidateResult::kOk, ValidateCat(cat));
}

TEST(TableValidatorTest, ValidPmt) {
  ts::DuckContext context;

  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0100" test-pid="0x0101">
        <component elementary_PID="0x0100" stream_type="0x02"/>
      </PMT>
    </tsduck>
  )");
  ts::PMT pmt(context, table);

  EXPECT_EQ(TableValidateResult::kOk, ValidatePmt(pmt));
}

TEST(TableValidatorTest, ValidTot) {
  ts::DuckContext context;

  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <TOT UTC_time="2021-01-01 00:00:00" test-pid="0x0014"/>
    </tsduck>
  )");
  ts::TOT tot(context, table);

  EXPECT_EQ(TableValidateResult::kOk, ValidateTot(tot));
}

TEST(TableValidatorTest, ValidEit) {
  ts::DuckContext context;

  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="true" />
      </EIT>
    </tsduck>
  )");
  ts::EIT eit(context, table);

  EXPECT_EQ(TableValidateResult::kOk, ValidateEit(eit));
}

TEST(TableValidatorTest, ValidNit) {
  ts::DuckContext context;

  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <NIT version="1" current="true" actual="true" network_id="0x0001"
           test-pid="0x0010">
        <transport_stream transport_stream_id="0x1234" original_network_id="0x0002" />
      </NIT>
    </tsduck>
  )");
  ts::NIT nit(context, table);

  EXPECT_EQ(TableValidateResult::kOk, ValidateNit(nit));
}

TEST(TableValidatorTest, ValidSdt) {
  ts::DuckContext context;

  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <SDT version="1" current="true" actual="true" transport_stream_id="0x0003"
           original_network_id="0x0002" test-pid="0x0011">
        <service service_id="0x0001" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-1" />
        </service>
      </SDT>
    </tsduck>
  )");
  ts::SDT sdt(context, table);

  EXPECT_EQ(TableValidateResult::kOk, ValidateSdt(sdt));
}

TEST(TableValidatorTest, SdtWithZeroTsId) {
  ts::DuckContext context;

  auto table = MakeTable(context, R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <SDT version="1" current="true" actual="true" transport_stream_id="0x0000"
           original_network_id="0x0002" test-pid="0x0011">
        <service service_id="0x0001" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-1" />
        </service>
      </SDT>
    </tsduck>
  )");
  ts::SDT sdt(context, table);

  EXPECT_EQ(TableValidateResult::kZeroSdtTsId, ValidateSdt(sdt));
}

TEST(TableValidatorTest, BrokenTables) {
  ts::DuckContext context;

  struct TestCase final {
    const char* name;
    ts::TID table_id;
    ts::PID source_pid;
  };

  const TestCase test_cases[] = {
      {"PAT", ts::TID_PAT, ts::PID_PAT},
      {"CAT", ts::TID_CAT, ts::PID_CAT},
      {"PMT", ts::TID_PMT, 0x0101},
      {"EIT", ts::TID_EIT_PF_ACT, ts::PID_EIT},
      {"TOT", ts::TID_TOT, ts::PID_TOT},
      {"NIT", ts::TID_NIT_ACT, ts::PID_NIT},
      {"SDT", ts::TID_SDT_ACT, ts::PID_SDT},
  };

  for (const auto& tc : test_cases) {
    SCOPED_TRACE(tc.name);

    auto table = MakeIncompleteLongSectionTable(tc.table_id, tc.source_pid);
    ASSERT_FALSE(table.isValid());

    TableValidateResult result;
    switch (tc.table_id) {
      case ts::TID_PAT: {
        ts::PAT pat(context, table);
        result = ValidatePat(pat, table);
        break;
      }
      case ts::TID_CAT: {
        ts::CAT cat(context, table);
        result = ValidateCat(cat);
        break;
      }
      case ts::TID_PMT: {
        ts::PMT pmt(context, table);
        result = ValidatePmt(pmt);
        break;
      }
      case ts::TID_EIT_PF_ACT: {
        ts::EIT eit(context, table);
        result = ValidateEit(eit);
        break;
      }
      case ts::TID_TOT: {
        ts::TOT tot(context, table);
        result = ValidateTot(tot);
        break;
      }
      case ts::TID_NIT_ACT: {
        ts::NIT nit(context, table);
        result = ValidateNit(nit);
        break;
      }
      case ts::TID_SDT_ACT: {
        ts::SDT sdt(context, table);
        result = ValidateSdt(sdt);
        break;
      }
      default:
        result = TableValidateResult::kOk;
        FAIL() << "unhandled table_id in test";
    }

    EXPECT_EQ(TableValidateResult::kBrokenTable, result);
  }
}
