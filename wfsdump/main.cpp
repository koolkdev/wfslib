/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <cstdio>
#include <vector>
#include <fstream>
#include <memory>
#include <iostream>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include <wfslib/WfsLib.h>

void dumpdir(boost::filesystem::path target, std::shared_ptr<Directory>& dir, boost::filesystem::path path, bool verbos) {
	if (!boost::filesystem::exists(target / path)) {
		if (!boost::filesystem::create_directories(target / path)) {
			std::cerr << "Error: Failed to create directory " << (target / path) << std::endl;
			return;
		}
	}
	for (auto item : *dir) {
		boost::filesystem::path npath = path / item->GetName();
		if (verbos)
			std::cout << "Dumping " << npath << std::endl;
		if (item->IsDirectory()) dumpdir(target, std::dynamic_pointer_cast<Directory>(item), npath, verbos);
		else if (item->IsFile()) {
			auto file = std::dynamic_pointer_cast<File>(item);
			std::ofstream output_file((target / npath).string(), std::ios::binary | std::ios::out);
			size_t to_read = file->GetSize();
			File::stream stream(file);
			std::vector<uint8_t> data(0x2000);
			while (to_read > 0) {
				stream.read(&*data.begin(), std::min(data.size(), to_read));
				auto read = stream.gcount();
				if (read <= 0) {
					std::cerr << "Error: Failed to read " << npath << std::endl;
					break;
				}
				output_file.write((char*)&*data.begin(), read);
				to_read -= static_cast<size_t>(read);
			}
			output_file.close();
		}
	}
}

int main(int argc, char *argv[]) {
	try {
		boost::program_options::options_description desc("Allowed options");
		uint32_t sector_log2_size;
		std::string wfs_path;
		desc.add_options()
			("help", "produce help message")
			("input", boost::program_options::value<std::string>(), "input file")
			("output", boost::program_options::value<std::string>(), "ouput directory")
			("otp", boost::program_options::value<std::string>(), "otp file")
			("seeprom", boost::program_options::value<std::string>(), "seeprom file (required if usb)")
			("dump-path", boost::program_options::value<std::string>(&wfs_path)->default_value("/"), "directory to dump (default: \"/\")")
			("mlc", "device is mlc (default: device is usb)")
			("usb", "device is usb")
			("verbos", "verbos output")
			("sector-size", boost::program_options::value<uint32_t>(&sector_log2_size)->default_value(9), "sector log2 size of device. 9 for 512 bytes (default), 11 for 2048 bytes and 12 for 4096 bytes")
			;

		boost::program_options::variables_map vm;
		boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
		boost::program_options::notify(vm);

		bool bad = false;
		if (!vm.count("input")) { std::cerr << "Missing input file (--input)" << std::endl; bad = true; }
		if (!vm.count("output")) { std::cerr << "Missing output directory (--output)" << std::endl; bad = true; }
		if (!vm.count("otp")) { std::cerr << "Missing otp file (--otp)" << std::endl; bad = true; }
		if ((!vm.count("seeprom") && !vm.count("mlc"))) { std::cerr << "Missing seeprom file (--seeprom)" << std::endl; bad = true; }
		if (vm.count("mlc") + vm.count("usb") > 1) { std::cerr << "Can't specify both --mlc and --usb" << std::endl; bad = true; }
		if (vm.count("help") || bad) {
			std::cout << "Usage: wfsdump --input <input file> --output <output directory> --otp <opt path> [--seeprom <seeprom path>] [--mlc] [--usb] [--dump-path <directory to dump>] [--sector-size 9/11/12] [--verbos]" << std::endl;
			std::cout << desc << "\n";
			return 1;
		}

		std::vector<uint8_t> key;
		std::unique_ptr<OTP> otp;
		// open otp
		try {
			otp.reset(OTP::LoadFromFile(vm["otp"].as<std::string>()));
		}
		catch (std::exception& e) {
			std::cerr << "Failed to open OTP: " << e.what() << std::endl;
			return 1;
		}

		if (vm.count("mlc")) {
			// mlc
			key = std::move(otp->GetMLCKey());
		}
		else {
			// usb
			std::unique_ptr<SEEPROM> seeprom;
			try {
				seeprom.reset(std::move(SEEPROM::LoadFromFile(vm["seeprom"].as<std::string>())));
			}
			catch (std::exception& e) {
				std::cerr << "Failed to open SEEPROM: " << e.what() << std::endl;
				return 1;
			}
			key = std::move(seeprom->GetUSBKey(*otp));
		}
		auto device = std::make_shared<FileDevice>(vm["input"].as<std::string>(), vm["sector-size"].as<uint32_t>());
		Wfs::DetectSectorsCount(device, key);
		Wfs wfs(device, key);
		auto dir = wfs.GetDirectory(vm["dump-path"].as<std::string>());
		if (!dir) {
			std::cerr << "Error: Didn't find directory " << vm["dump-path"].as<std::string>() << " in wfs" << std::endl;
			return 1;
		}
		std::cout << "Dumping..." << std::endl;
		dumpdir(boost::filesystem::path(vm["output"].as<std::string>()), dir, boost::filesystem::path(""), !!vm.count("verbos"));
		std::cout << "Done!" << std::endl;
	}
	catch (std::exception & e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}