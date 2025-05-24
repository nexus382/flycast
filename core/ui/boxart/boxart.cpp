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

// Function to find custom boxart in content directories (following same pattern as findNaomiBios)
std::string findCustomBoxartInContentDirs(const std::string& baseName, const char* extension)
{
	for (const auto& path : config::ContentPath.get())
	{
		try {
			std::string customBoxartDir = hostfs::storage().getSubPath(path, "custom-boxart");
			std::string fullPath = hostfs::storage().getSubPath(customBoxartDir, baseName + extension);
			if (hostfs::storage().exists(fullPath))
				return fullPath;
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

	// First check in the custom boxart directory (DATA folder)
	std::string customDir = getCustomBoxartDirectory();
	DEBUG_LOG(COMMON, "Looking in custom boxart directory: %s", customDir.c_str());

	if (!file_exists(customDir))
	{
		DEBUG_LOG(COMMON, "Creating custom boxart directory: %s", customDir.c_str());
		make_directory(customDir);
	}

	for (const char* ext : extensions)
	{
		// Make sure we use the correct path separator for the OS
		std::string customPath = customDir;
		if (!customPath.empty() && customPath.back() != '/' && customPath.back() != '\\')
			customPath += '/';

		customPath += baseName + ext;

		if (file_exists(customPath))
		{
			NOTICE_LOG(COMMON, "Found custom boxart at: %s", customPath.c_str());
			boxart.setBoxartPath(customPath);
			boxart.parsed = true;
			return true;
		}
	}

	// Then check in content directories
	for (const char* ext : extensions)
	{
		std::string contentPath = findCustomBoxartInContentDirs(baseName, ext);
		if (!contentPath.empty())
		{
			NOTICE_LOG(COMMON, "Found custom boxart in content directory: %s", contentPath.c_str());
			boxart.setBoxartPath(contentPath);
			boxart.parsed = true;
			return true;
		}
	}

	// Also check in the FLYCAST_HOME paths (for Android content directory)
#ifdef __ANDROID__
	const char *home = nowide::getenv("FLYCAST_HOME");
	if (home != nullptr)
		DEBUG_LOG(COMMON, "Checking FLYCAST_HOME paths: %s", home);

	while (home != nullptr)
	{
		const char *pcolon = strchr(home, ':');
		std::string homePath;
		if (pcolon != nullptr)
		{
			homePath = std::string(home, pcolon - home);
			home = pcolon + 1;
		}
		else
		{
			homePath = home;
			home = nullptr;
		}

		DEBUG_LOG(COMMON, "Checking home path: %s", homePath.c_str());

		// Check in custom-boxart subdirectory of each FLYCAST_HOME path
		for (const char* ext : extensions)
		{
			// Try the direct custom-boxart folder first
			std::string contentPath = homePath + "/custom-boxart/";
			DEBUG_LOG(COMMON, "Checking direct path: %s", contentPath.c_str());

			if (!file_exists(contentPath))
			{
				// Then try the Flycast subdirectory
				contentPath = homePath + "/Flycast/custom-boxart/";
				DEBUG_LOG(COMMON, "Checking Flycast subdir: %s", contentPath.c_str());
			}

			if (file_exists(contentPath))
			{
				std::string fullPath = contentPath + baseName + ext;
				DEBUG_LOG(COMMON, "Checking for file: %s", fullPath.c_str());

				if (file_exists(fullPath))
				{
					NOTICE_LOG(COMMON, "Found custom boxart in content directory: %s", fullPath.c_str());
					boxart.setBoxartPath(fullPath);
					boxart.parsed = true;
					return true;
				}
			}
		}
	}
#endif

	DEBUG_LOG(COMMON, "No custom boxart found for %s", baseName.c_str());
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

			// Check for custom boxart first
			if (checkCustomBoxart(boxart))
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
