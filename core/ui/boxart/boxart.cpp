/*
	Copyright 2022 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "boxart.h"
#include "gamesdb.h"
#include "../game_scanner.h"
#include "oslib/oslib.h"
#include "oslib/storage.h"
#include "cfg/option.h"
#include <chrono>
#include "nowide/cstdlib.hpp"
#include "hostfs/storage.h"

Boxart& Boxart::get()
{
	static Boxart boxart;
	return boxart;
}

GameBoxart Boxart::getBoxart(const GameMedia& media)
{
	loadDatabase();
	GameBoxart boxart;
	{
		std::lock_guard<std::mutex> guard(mutex);
		auto it = games.find(media.fileName);
		if (it != games.end())
			boxart = it->second;
	}
	return boxart;
}

// Function to find custom boxart following same pattern as findNaomiBios
std::string findCustomBoxart(const std::string& filename)
{
	// First try DATA folder
	std::string fullpath = get_writable_data_path("custom-boxart/" + filename);
	if (hostfs::storage().exists(fullpath))
		return fullpath;
	
	// Then try content directories
	for (const auto& path : config::ContentPath.get())
	{
		try {
			fullpath = hostfs::storage().getSubPath(path, "custom-boxart/" + filename);
			if (hostfs::storage().exists(fullpath))
				return fullpath;
		} catch (const hostfs::StorageException& e) {
		}
	}
	return "";
}

bool Boxart::checkCustomBoxart(GameBoxart& boxart)
{
	std::string baseName = get_file_basename(boxart.fileName);
	DEBUG_LOG(COMMON, "Checking custom boxart for %s", baseName.c_str());

	// Check for common image formats
	const char* extensions[] = { ".png", ".jpg", ".jpeg", ".webp" };

	for (const char* ext : extensions)
	{
		std::string filename = baseName + ext;
		std::string foundPath = findCustomBoxart(filename);
		if (!foundPath.empty())
		{
			NOTICE_LOG(COMMON, "Found custom boxart: %s", foundPath.c_str());
			boxart.setBoxartPath(foundPath);
			boxart.parsed = true;
			return true;
		}
	}

	DEBUG_LOG(COMMON, "No custom boxart found for %s", baseName.c_str());
	// Mark as parsed even if no custom boxart found to avoid repeated checks
	boxart.parsed = true;
	return false;
}

GameBoxart Boxart::getBoxartAndLoad(const GameMedia& media)
{
	loadDatabase();
	GameBoxart boxart;
	{
		std::lock_guard<std::mutex> guard(mutex);
		auto it = games.find(media.fileName);
		if (it != games.end())
		{
			boxart = it->second;

			// Only check for custom boxart if not already parsed and no boxart found
			if (!boxart.parsed && boxart.boxartPath.empty() && checkCustomBoxart(boxart))
			{
				games[media.fileName] = boxart;
				databaseDirty = true;
				return boxart;
			}

			if (config::FetchBoxart && !boxart.busy && !boxart.scraped)
			{
				boxart.busy = it->second.busy = true;
				boxart.gamePath = media.path;
				toFetch.push_back(boxart);
			}
		}
		else
		{
			boxart.fileName = media.fileName;
			boxart.gamePath = media.path;
			boxart.name = media.name;
			boxart.searchName = media.gameName;	// for arcade games

			// Check for custom boxart
			if (checkCustomBoxart(boxart))
			{
				games[boxart.fileName] = boxart;
				databaseDirty = true;
				return boxart;
			}

			boxart.busy = true;
			games[boxart.fileName] = boxart;
			toFetch.push_back(boxart);
		}
	}
	fetchBoxart();
	return boxart;
}

void Boxart::fetchBoxart()
{
	if (fetching.valid() && fetching.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		fetching.get();
	if (fetching.valid())
		return;
	if (toFetch.empty())
		return;
	fetching = std::async(std::launch::async, [this]() {
		ThreadName _("BoxArt-scraper");
		if (offlineScraper == nullptr)
		{
			offlineScraper = std::unique_ptr<Scraper>(new OfflineScraper());
			offlineScraper->initialize(getSaveDirectory());
		}
		if (config::FetchBoxart && scraper == nullptr)
		{
			scraper = std::unique_ptr<Scraper>(new TheGamesDb());
			if (!scraper->initialize(getSaveDirectory()))
			{
				ERROR_LOG(COMMON, "thegamesdb scraper initialization failed");
				scraper.reset();
				return;
			}
		}
		std::vector<GameBoxart> boxart;
		{
			std::lock_guard<std::mutex> guard(mutex);
			size_t size = std::min(toFetch.size(), (size_t)10);
			boxart = std::vector<GameBoxart>(toFetch.begin(), toFetch.begin() + size);
			toFetch.erase(toFetch.begin(), toFetch.begin() + size);
		}
		DEBUG_LOG(COMMON, "Scraping %d games", (int)boxart.size());
		offlineScraper->scrape(boxart);
		{
			std::lock_guard<std::mutex> guard(mutex);
			for (GameBoxart& b : boxart)
				if (b.scraped || b.parsed)
				{
					if (!config::FetchBoxart || b.scraped)
						b.busy = false;
					games[b.fileName] = b;
					databaseDirty = true;
				}
		}
		if (config::FetchBoxart)
		{
			try {
				scraper->scrape(boxart);
				{
					std::lock_guard<std::mutex> guard(mutex);
					for (GameBoxart& b : boxart)
					{
						b.busy = false;
						games[b.fileName] = b;
					}
				}
				databaseDirty = true;
			} catch (const std::runtime_error& e) {
				if (*e.what() != '\0')
					INFO_LOG(COMMON, "thegamesdb error: %s", e.what());
				{
					// put back failed items into toFetch array
					std::lock_guard<std::mutex> guard(mutex);
					for (GameBoxart& b : boxart)
						if (b.scraped)
						{
							b.busy = false;
							games[b.fileName] = b;
							databaseDirty = true;
						}
						else
						{
							toFetch.push_back(b);
						}
				}
			}
		}
		saveDatabase();
	});
}

void Boxart::saveDatabase()
{
	if (!databaseDirty)
		return;
	std::string db_name = getSaveDirectory() + DB_NAME;
	FILE *file = nowide::fopen(db_name.c_str(), "wt");
	if (file == nullptr)
	{
		WARN_LOG(COMMON, "Can't save boxart database to %s: error %d", db_name.c_str(), errno);
		return;
	}
	DEBUG_LOG(COMMON, "Saving boxart database to %s", db_name.c_str());

	json array;
	{
		std::lock_guard<std::mutex> guard(mutex);
		for (const auto& game : games)
			if (game.second.scraped || game.second.parsed)
				array.push_back(game.second.to_json());
	}
	std::string serialized = array.dump(4);
	fwrite(serialized.c_str(), 1, serialized.size(), file);
	fclose(file);
	databaseDirty = false;
}

void Boxart::loadDatabase()
{
	if (databaseLoaded)
		return;
	databaseLoaded = true;
	databaseDirty = false;
	std::string save_dir = getSaveDirectory();
	if (!file_exists(save_dir))
		make_directory(save_dir);
	std::string db_name = save_dir + DB_NAME;
	FILE *f = nowide::fopen(db_name.c_str(), "rt");
	if (f == nullptr)
		return;

	DEBUG_LOG(COMMON, "Loading boxart database from %s", db_name.c_str());
	std::string all_data;
	char buf[4096];
	while (true)
	{
		int s = fread(buf, 1, sizeof(buf), f);
		if (s <= 0)
			break;
		all_data.append(buf, s);
	}
	fclose(f);
	try {
		std::lock_guard<std::mutex> guard(mutex);

		json v = json::parse(all_data);
		for (const auto& o : v)
		{
			GameBoxart game(o);
			games[game.fileName] = game;
		}
	} catch (const json::exception& e) {
		WARN_LOG(COMMON, "Corrupted database file: %s", e.what());
	}

	// Create custom boxart directory if it doesn't exist
	std::string customDir = getCustomBoxartDirectory();
	if (!file_exists(customDir))
		make_directory(customDir);
}

void Boxart::term()
{
	if (fetching.valid())
		fetching.get();
}
