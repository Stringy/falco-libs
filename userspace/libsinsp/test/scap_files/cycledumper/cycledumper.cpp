// SPDX-License-Identifier: Apache-2.0
/*
Copyright (C) 2024 The Falco Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#include <libsinsp/sinsp.h>
#include <libsinsp/sinsp_cycledumper.h>
#include <helpers/scap_file_helpers.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

TEST(scap_file, filter) {
	std::filesystem::path tmp_scap_file_path =
	        std::filesystem::temp_directory_path() / "tmp.XYZXXZZZZ.scap";
	std::string tmp_scap_file_name = tmp_scap_file_path.string();

	// Dump a filtered scap-file
	{
		sinsp inspector;
		inspector.set_filter("proc.name=ifplugd");
		inspector.open_savefile(LIBSINSP_TEST_SCAP_FILES_DIR "/sample.scap");

		auto dumper = std::make_unique<sinsp_cycledumper>(&inspector,
		                                                  tmp_scap_file_name,
		                                                  0,
		                                                  0,
		                                                  0,
		                                                  0,
		                                                  true);

		int32_t res;
		sinsp_evt* evt;
		do {
			res = inspector.next(&evt);
			EXPECT_NE(res, SCAP_FAILURE);
			if(res != SCAP_EOF) {
				dumper->dump(evt);
			}
		} while(res != SCAP_EOF);

		dumper->close();
		inspector.close();
	}

	{
		sinsp inspector;
		inspector.open_savefile(tmp_scap_file_name);
		ASSERT_EQ(inspector.m_thread_manager->get_thread_count(), 1);
	}

	std::filesystem::remove(tmp_scap_file_path);
}

int n_opens = 0;
int n_closes = 0;

void open_cb() {
	n_opens += 1;
}

void close_cb() {
	n_closes += 1;
}

TEST(scap_file, cycledumper_num_events) {
	int events_per_capture = 100;
	n_opens = 0;
	n_closes = 0;

	std::filesystem::path tmp_scap_file_path =
	        std::filesystem::temp_directory_path() / "tmp.XYZXXZZZZ.scap";
	std::string tmp_scap_file_name = tmp_scap_file_path.string();
	{
		sinsp inspector;
		inspector.open_savefile(LIBSINSP_TEST_SCAP_FILES_DIR "/sample.scap");

		auto dumper = std::make_unique<sinsp_cycledumper>(&inspector,
		                                                  tmp_scap_file_name,
		                                                  0,
		                                                  0,
		                                                  0,
		                                                  events_per_capture,
		                                                  true);

		std::vector<std::function<void()>> open_cbs = {std::bind(&open_cb)};
		std::vector<std::function<void()>> close_cbs = {std::bind(&close_cb)};

		dumper->set_callbacks(open_cbs, close_cbs);

		int32_t res;
		sinsp_evt* evt;
		do {
			res = inspector.next(&evt);
			EXPECT_NE(res, SCAP_FAILURE);
			if(res != SCAP_EOF) {
				dumper->dump(evt);
			}
		} while(res != SCAP_EOF);

		dumper->close();
		inspector.close();
	}

	ASSERT_EQ(n_opens, 5);
	ASSERT_EQ(n_closes, 6);  // a autodump_stop is called before starting.
	std::filesystem::remove(tmp_scap_file_path);
}

TEST(scap_file, cycledumper_seconds) {
	int seconds_per_capture = 1;
	n_opens = 0;
	n_closes = 0;

	std::filesystem::path tmp_scap_file_path =
	        std::filesystem::temp_directory_path() / "tmp.XYZXXZZZZ.scap";
	std::string tmp_scap_file_name = tmp_scap_file_path.string();
	{
		sinsp inspector;
		inspector.open_savefile(LIBSINSP_TEST_SCAP_FILES_DIR "/sample.scap");

		auto dumper = std::make_unique<sinsp_cycledumper>(&inspector,
		                                                  tmp_scap_file_name,
		                                                  0,
		                                                  seconds_per_capture,
		                                                  0,
		                                                  0,
		                                                  true);

		std::vector<std::function<void()>> open_cbs = {std::bind(&open_cb)};
		std::vector<std::function<void()>> close_cbs = {std::bind(&close_cb)};

		dumper->set_callbacks(open_cbs, close_cbs);

		int32_t res;
		sinsp_evt* evt;
		do {
			res = inspector.next(&evt);
			EXPECT_NE(res, SCAP_FAILURE);
			if(res != SCAP_EOF) {
				dumper->dump(evt);
			}
		} while(res != SCAP_EOF);

		dumper->close();
		inspector.close();
	}

	ASSERT_EQ(n_opens, 1);
	ASSERT_EQ(n_closes, 2);  // a autodump_stop is called before starting.
	std::filesystem::remove(tmp_scap_file_path);
}
